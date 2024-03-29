/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */
#define GOOGLE_STRIP_LOG 1

#include "rellic/AST/StructGenerator.h"

#include <clang/AST/Attr.h>
#include <clang/AST/Expr.h>
#include <clang/AST/RecordLayout.h>
#include <clang/Basic/TargetInfo.h>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <llvm/BinaryFormat/Dwarf.h>

#include <string>
#include <unordered_set>

#include "rellic/BC/Util.h"

static std::string MakeValid(const std::string& name, unsigned id) {
  return name + "_" + std::to_string(id);
}

namespace rellic {
clang::QualType StructGenerator::BuildType(llvm::DIType* t, int sizeHint) {
  VLOG(2) << "BuildType: " << rellic::LLVMThingToString(t);
  if (!t) {
    return ast_ctx.VoidTy;
  }

  clang::QualType type{};
  if (auto comp = llvm::dyn_cast<llvm::DICompositeType>(t)) {
    type = BuildComposite(comp);
  } else if (auto der = llvm::dyn_cast<llvm::DIDerivedType>(t)) {
    type = BuildDerived(der, sizeHint);
  } else if (auto basic = llvm::dyn_cast<llvm::DIBasicType>(t)) {
    type = BuildBasic(basic, sizeHint);
  } else if (auto sub = llvm::dyn_cast<llvm::DISubroutineType>(t)) {
    type = BuildSubroutine(sub);
  } else {
    LOG(FATAL) << "Unknown DIType: " << rellic::LLVMThingToString(t);
  }
  CHECK(!type.isNull());
  return type;
}

// Walks down a chain of typedefs to get the ultimate base type for a class
static llvm::DICompositeType* GetBaseType(llvm::DIType* type) {
  if (auto composite = llvm::dyn_cast<llvm::DICompositeType>(type)) {
    return composite;
  } else if (auto derived = llvm::dyn_cast<llvm::DIDerivedType>(type)) {
    if (derived->getTag() == llvm::dwarf::DW_TAG_typedef) {
      return GetBaseType(derived->getBaseType());
    }
  }

  LOG(FATAL) << "Invalid type " << LLVMThingToString(type);
  return nullptr;
}

struct OffsetDIDerivedType {
  uint64_t offset;
  llvm::DIDerivedType* type;
};

static std::vector<OffsetDIDerivedType> GetFields(
    llvm::DICompositeType* composite) {
  std::vector<OffsetDIDerivedType> fields{};
  auto nodes{composite->getElements()};
  std::for_each(nodes.begin(), nodes.end(), [&](auto node) {
    if (auto type = llvm::dyn_cast<llvm::DIDerivedType>(node)) {
      auto tag{type->getTag()};

      if (tag != llvm::dwarf::DW_TAG_member &&
          tag != llvm::dwarf::DW_TAG_inheritance) {
        return;
      }

      if (type->getFlags() & llvm::DIDerivedType::DIFlags::FlagStaticMember) {
        return;
      }

      if (tag == llvm::dwarf::DW_TAG_inheritance) {
        auto sub_fields{GetFields(GetBaseType(type->getBaseType()))};
        for (auto sub_field : sub_fields) {
          fields.push_back(
              {type->getOffsetInBits() + sub_field.offset, sub_field.type});
        }
      } else {
        fields.push_back({type->getOffsetInBits(), type});
      }
    }
  });
  std::sort(fields.begin(), fields.end(),
            [](auto a, auto b) { return a.offset < b.offset; });

  return fields;
}

struct FieldInfo {
  std::string Name;
  clang::QualType Type;
  unsigned BitWidth;
};

static FieldInfo CreatePadding(clang::ASTContext& ast_ctx,
                               unsigned needed_padding, unsigned& count) {
  auto padding_type{ast_ctx.CharTy};
  auto type_size{ast_ctx.getTypeSize(padding_type)};
  auto name{"padding_" + std::to_string(count++)};
  if (needed_padding % type_size) {
    return {name, ast_ctx.LongLongTy, needed_padding};
  } else {
    auto padding_count{needed_padding / type_size};
    auto padding_arr_type{ast_ctx.getConstantArrayType(
        padding_type, llvm::APInt(64, padding_count), nullptr,
        clang::ArrayType::ArraySizeModifier::Normal, 0)};
    return {name, padding_arr_type, 0};
  }
}

static clang::FieldDecl* FieldInfoToFieldDecl(clang::ASTContext& ast_ctx,
                                              ASTBuilder& ast,
                                              clang::RecordDecl* decl,
                                              FieldInfo& field) {
  clang::FieldDecl* fdecl{};
  if (field.BitWidth) {
    fdecl = ast.CreateFieldDecl(decl, field.Type, field.Name, field.BitWidth);
  } else {
    fdecl = ast.CreateFieldDecl(decl, field.Type, field.Name);
  }
  return fdecl;
}

static unsigned GetStructSize(clang::ASTContext& ast_ctx, ASTBuilder& ast,
                              std::vector<FieldInfo>& fields) {
  if (!fields.size()) {
    return 0;
  }
  static auto count{0U};

  auto tudecl{ast_ctx.getTranslationUnitDecl()};
  auto decl{ast.CreateStructDecl(tudecl, "temp" + std::to_string(count++))};
  clang::AttributeCommonInfo info{clang::SourceLocation{}};
  decl->addAttr(clang::PackedAttr::Create(ast_ctx, info));
  for (auto& field : fields) {
    decl->addDecl(FieldInfoToFieldDecl(ast_ctx, ast, decl, field));
  }
  decl->completeDefinition();
  auto& layout{ast_ctx.getASTRecordLayout(decl)};
  auto last_field{fields.back()};
  auto last_size{last_field.BitWidth ? last_field.BitWidth
                                     : ast_ctx.getTypeSize(last_field.Type)};
  return layout.getFieldOffset(fields.size() - 1) + last_size;
}

void StructGenerator::DefineNonPackedStruct(
    clang::RecordDecl* decl, std::vector<OffsetDIDerivedType>& fields) {
  std::unordered_set<std::string> visible_field_names;
  for (auto& field : fields) {
    auto type{
        BuildType(field.type->getBaseType(), field.type->getSizeInBits())};
    auto name{GetUniqueName(field.type->getName().str(), visible_field_names)};
    clang::FieldDecl* fdecl;
    if (field.type->getFlags() & llvm::DINode::DIFlags::FlagBitField) {
      fdecl = ast.CreateFieldDecl(decl, type, name);
    } else {
      fdecl = ast.CreateFieldDecl(decl, type, name);
    }
    decl->addDecl(CHECK_NOTNULL(fdecl));
  }
}

static bool CheckOffsets(std::vector<OffsetDIDerivedType>& fields,
                         const clang::ASTRecordLayout& layout) {
  for (auto i{0U}; i < fields.size(); ++i) {
    if (fields[i].offset != layout.getFieldOffset(i)) {
      return false;
    }
  }
  return true;
}

uint64_t StructGenerator::GetLayoutSize(const clang::ASTRecordLayout& layout) {
  return ast_ctx.toBits(layout.getSize());
}

void StructGenerator::VisitFields(clang::RecordDecl* decl,
                                  llvm::DICompositeType* s, DeclToDbgInfo& map,
                                  bool isUnion) {
  auto elems{GetFields(s)};
  static auto test_count{0U};
  auto test_decl{
      ast.CreateStructDecl(ast_ctx.getTranslationUnitDecl(),
                           "test_packed_" + std::to_string(test_count++))};
  DefineNonPackedStruct(test_decl, elems);
  test_decl->completeDefinition();
  auto& test_layout{ast_ctx.getASTRecordLayout(test_decl)};
  auto layout_size{GetLayoutSize(test_layout)};

  CHECK_EQ(elems.size(), test_layout.getFieldCount())
      << "Field count doesn't match";

  // Check if the struct can be treated as non packed
  if (CheckOffsets(elems, test_layout) && layout_size == s->getSizeInBits()) {
    DefineNonPackedStruct(decl, elems);
    decl->completeDefinition();
    return;
  }

  // Struct needs to be packed
  auto field_count{0U};
  std::vector<FieldInfo> fields{};
  if (!isUnion) {
    clang::AttributeCommonInfo attrinfo{clang::SourceLocation{}};
    decl->addAttr(clang::PackedAttr::Create(ast_ctx, attrinfo));
  }

  std::unordered_set<std::string> visible_field_names;
  for (auto elem : elems) {
    auto curr_offset{isUnion ? 0 : GetStructSize(ast_ctx, ast, fields)};
    DLOG(INFO) << "Field " << elem.type->getName().str()
               << " offset: " << curr_offset << " in " << decl->getName().str();
    CHECK_LE(curr_offset, elem.offset)
        << "Field " << LLVMThingToString(elem.type)
        << " cannot be correctly aligned";
    if (curr_offset < elem.offset) {
      auto needed_padding{elem.offset - curr_offset};
      auto info{CreatePadding(ast_ctx, needed_padding, field_count)};
      fields.push_back(info);
      decl->addDecl(FieldInfoToFieldDecl(ast_ctx, ast, decl, info));
    }

    auto name{GetUniqueName(elem.type->getName().str(), visible_field_names)};
    ++field_count;
    auto type{BuildType(elem.type->getBaseType(), elem.type->getSizeInBits())};
    CHECK(!type.isNull());
    FieldInfo field{};
    if (elem.type->getFlags() & llvm::DINode::DIFlags::FlagBitField) {
      field = {name, type, (unsigned)elem.type->getSizeInBits()};
    } else {
      field = {name, type, 0};
    }
    auto fdecl{FieldInfoToFieldDecl(ast_ctx, ast, decl, field)};
    fields.push_back(field);

    map[fdecl] = elem;
    decl->addDecl(fdecl);
  }

  if (!isUnion) {
    auto cur_size{GetStructSize(ast_ctx, ast, fields)};
    auto expected_size{s->getSizeInBits()};
    CHECK_LE(cur_size, expected_size);
    if (cur_size < expected_size) {
      auto needed_padding{expected_size - cur_size};
      auto info{CreatePadding(ast_ctx, needed_padding, field_count)};
      fields.push_back(info);
      decl->addDecl(FieldInfoToFieldDecl(ast_ctx, ast, decl, info));
    }
  }

  decl->completeDefinition();
}

void StructGenerator::DefineStruct(llvm::DICompositeType* s) {
  VLOG(1) << "DefineStruct: " << rellic::LLVMThingToString(s);
  auto& fwd_decl{CHECK_NOTNULL(fwd_decl_records[s])};
  auto tudecl{ast_ctx.getTranslationUnitDecl()};
  auto decl{ast.CreateStructDecl(tudecl, fwd_decl->getName().str(), fwd_decl)};

  DeclToDbgInfo fmap{};
  if (s->getFlags() & llvm::DICompositeType::DIFlags::FlagFwdDecl) {
    auto size{s->getSizeInBits()};
    unsigned count{};
    auto info{CreatePadding(ast_ctx, size, count)};
    decl->addDecl(FieldInfoToFieldDecl(ast_ctx, ast, decl, info));
    decl->completeDefinition();
  } else {
    VisitFields(decl, s, fmap, /*isUnion=*/false);
  }

  auto& layout{ast_ctx.getASTRecordLayout(decl)};
  auto layout_size{GetLayoutSize(layout)};
  CHECK_EQ(layout_size, s->getSizeInBits())
      << "Struct " << s->getName().str() << " has incorrect size";
  for (auto field : decl->fields()) {
    auto elem{fmap.find(field)};
    if (elem != fmap.end()) {
      CHECK_EQ(layout.getFieldOffset(field->getFieldIndex()),
               elem->second.offset)
          << "Field " << field->getName().str() << " of struct "
          << decl->getName().str() << " is not correctly aligned";
    }
  }

  tudecl->addDecl(decl);
}

void StructGenerator::DefineUnion(llvm::DICompositeType* u) {
  VLOG(1) << "DefineUnion: " << rellic::LLVMThingToString(u);
  auto& fwd_decl{CHECK_NOTNULL(fwd_decl_records[u])};
  auto tudecl{ast_ctx.getTranslationUnitDecl()};
  auto decl{ast.CreateUnionDecl(tudecl, fwd_decl->getName().str(), fwd_decl)};

  DeclToDbgInfo fmap{};
  VisitFields(decl, u, fmap, /*isUnion=*/true);

  tudecl->addDecl(decl);
}

void StructGenerator::DefineComposite(llvm::DICompositeType* t) {
  VLOG(2) << "DefineComposite: " << LLVMThingToString(t);
  switch (t->getTag()) {
    case llvm::dwarf::DW_TAG_structure_type:
    case llvm::dwarf::DW_TAG_class_type:
      DefineStruct(t);
      break;
    case llvm::dwarf::DW_TAG_union_type:
      DefineUnion(t);
      break;
    default:
      LOG(FATAL) << "Invalid DICompositeType: " << LLVMThingToString(t);
  }
}

clang::QualType StructGenerator::BuildArray(llvm::DICompositeType* a) {
  VLOG(1) << "BuildArray: " << rellic::LLVMThingToString(a);
  auto base{BuildType(a->getBaseType())};
  auto subrange{llvm::cast<llvm::DISubrange>(a->getElements()[0])};
  auto* ci = subrange->getCount().get<llvm::ConstantInt*>();
  return ast_ctx.getConstantArrayType(
      base, llvm::APInt(64, ci->getZExtValue()), nullptr,
      clang::ArrayType::ArraySizeModifier::Normal, 0);
}

clang::QualType StructGenerator::BuildDerived(llvm::DIDerivedType* d,
                                              int sizeHint) {
  VLOG(2) << "BuildDerived: " << rellic::LLVMThingToString(d);
  switch (d->getTag()) {
    case llvm::dwarf::DW_TAG_const_type:
      return ast_ctx.getConstType(BuildType(d->getBaseType(), sizeHint));
    case llvm::dwarf::DW_TAG_volatile_type:
      return ast_ctx.getVolatileType(BuildType(d->getBaseType(), sizeHint));
    case llvm::dwarf::DW_TAG_restrict_type:
      return ast_ctx.getRestrictType(BuildType(d->getBaseType(), sizeHint));
    case llvm::dwarf::DW_TAG_atomic_type:
      return ast_ctx.getAtomicType(BuildType(d->getBaseType(), sizeHint));
    case llvm::dwarf::DW_TAG_pointer_type:
    case llvm::dwarf::DW_TAG_reference_type:
    case llvm::dwarf::DW_TAG_rvalue_reference_type:
    case llvm::dwarf::DW_TAG_ptr_to_member_type:
      return ast_ctx.getPointerType(BuildType(d->getBaseType(), sizeHint));
    case llvm::dwarf::DW_TAG_typedef: {
      auto& tdef_decl{typedef_decls[d]};
      if (!tdef_decl) {
        auto tudecl{ast_ctx.getTranslationUnitDecl()};
        auto name{GetUniqueName(d->getName().str(), visible_tdefs)};
        tdef_decl = ast.CreateTypedefDecl(
            tudecl, name, BuildType(d->getBaseType(), sizeHint));
        tudecl->addDecl(tdef_decl);
      }
      return ast_ctx.getTypedefType(tdef_decl);
    }
    default:
      LOG(FATAL) << "Unknown DIDerivedType: " << rellic::LLVMThingToString(d);
  }
  return {};
}

clang::QualType StructGenerator::BuildBasic(llvm::DIBasicType* b,
                                            int sizeHint) {
  VLOG(1) << "BuildBasic: " << rellic::LLVMThingToString(b);
  if (b->getTag() == llvm::dwarf::DW_TAG_unspecified_type) {
    if (sizeHint <= 0) {
      // TODO(frabert): this happens for e.g.
      // `typedef decltype(nullptr) nullptr_t;`
      // in which case the debug info doesn't specify the size of
      // the type. Best guess in this case is the size of void*
      return ast_ctx.VoidPtrTy;
    } else {
      return ast_ctx.getIntTypeForBitwidth(sizeHint, 0);
    }
  }

  auto char_size{ast_ctx.getTypeSize(ast_ctx.CharTy)};
  auto char_signed{ast_ctx.CharTy->isSignedIntegerType()};
  auto b_size{b->getSizeInBits()};
  auto b_signed{b->getSignedness() == llvm::DIBasicType::Signedness::Signed};

  if (!b->getSizeInBits()) {
    if (sizeHint > 0) {
      b_size = sizeHint;
    } else {
      b_size = char_size;
    }
  }

  if (b->getEncoding() == llvm::dwarf::DW_ATE_float) {
    return ast_ctx.getRealTypeForBitwidth(b->getSizeInBits(),
                                          clang::FloatModeKind::Float);
  } else {
    if (char_size == b_size && char_signed == b_signed) {
      return ast_ctx.CharTy;
    } else {
      return ast_ctx.getIntTypeForBitwidth(b_size, b_signed);
    }
  }
}

clang::QualType StructGenerator::BuildSubroutine(llvm::DISubroutineType* s) {
  VLOG(1) << "BuildSubroutine: " << rellic::LLVMThingToString(s);
  auto type_array{s->getTypeArray()};
  auto epi{clang::FunctionProtoType::ExtProtoInfo()};
  if (type_array.size() == 0) {
    // TODO(frabert): I'm not sure why this happens or how to best handle this.
    // I'm going to guess it's a "void(void)"
    return ast_ctx.getFunctionType(ast_ctx.VoidTy, {}, epi);
  }

  auto ret_type{BuildType(type_array[0])};
  std::vector<clang::QualType> params{};
  for (auto i{1U}; i < type_array.size(); ++i) {
    auto t{type_array[i]};
    if (!t) {
      CHECK_EQ(i, type_array.size() - 1)
          << "Only last argument type can be null";
      epi.Variadic = true;
    } else {
      params.push_back(BuildType(t));
    }
  }
  return ast_ctx.getFunctionType(ret_type, params, epi);
}

clang::RecordDecl* StructGenerator::GetRecordDecl(llvm::DICompositeType* t) {
  auto& decl{fwd_decl_records[t]};
  if (!decl) {
    auto tudecl{ast_ctx.getTranslationUnitDecl()};
    switch (t->getTag()) {
      case llvm::dwarf::DW_TAG_class_type:
      case llvm::dwarf::DW_TAG_structure_type:
        decl = ast.CreateStructDecl(
            tudecl, GetUniqueName(t->getName().str(), visible_structs));
        break;
      case llvm::dwarf::DW_TAG_union_type:
        decl = ast.CreateUnionDecl(
            tudecl, GetUniqueName(t->getName().str(), visible_unions));
        break;
      default:
        LOG(FATAL) << "Invalid DICompositeType: " << LLVMThingToString(t);
    }
    tudecl->addDecl(decl);
  }
  return decl;
}

clang::QualType StructGenerator::GetEnumDecl(llvm::DICompositeType* t) {
  auto& type{enum_types[t]};
  if (!type.isNull()) {
    return type;
  }

  auto tudecl{ast_ctx.getTranslationUnitDecl()};
  auto base{BuildType(t->getBaseType())};
  auto name{GetUniqueName(t->getName().str(), visible_enums)};
  if (base == ast_ctx.IntTy || base == ast_ctx.UnsignedIntTy) {
    auto decl{ast.CreateEnumDecl(tudecl, name)};
    for (auto elem : t->getElements()) {
      if (auto enumerator = llvm::dyn_cast<llvm::DIEnumerator>(elem)) {
        auto elem_name{
            GetUniqueName(enumerator->getName().str(), visible_values)};
        auto cdecl{ast.CreateEnumConstantDecl(
            decl, elem_name, ast.CreateIntLit(enumerator->getValue()))};
        decl->addDecl(cdecl);
      }
    }
    decl->completeDefinition(base, base, 0, 0);

    tudecl->addDecl(decl);
    type = ast_ctx.getEnumType(decl);
  } else {
    auto tdef{ast.CreateTypedefDecl(tudecl, name, base)};
    tudecl->addDecl(tdef);
    type = ast_ctx.getTypedefType(tdef);

    auto i{0U};
    for (auto elem : t->getElements()) {
      if (auto enumerator = llvm::dyn_cast<llvm::DIEnumerator>(elem)) {
        auto elem_name{
            GetUniqueName(enumerator->getName().str(), visible_values)};
        auto vdecl{
            ast.CreateVarDecl(tudecl, type, elem_name, clang::SC_Static)};
        vdecl->setInit(ast.CreateIntLit(enumerator->getValue()));
        tudecl->addDecl(vdecl);
      }
    }
  }
  return type;
}

clang::QualType StructGenerator::BuildComposite(llvm::DICompositeType* type) {
  VLOG(2) << "BuildComposite: " << rellic::LLVMThingToString(type);
  switch (type->getTag()) {
    case llvm::dwarf::DW_TAG_class_type:
      DLOG(INFO) << "Treating class declaration as struct";
      [[fallthrough]];
    case llvm::dwarf::DW_TAG_structure_type:
    case llvm::dwarf::DW_TAG_union_type:
      return ast_ctx.getRecordType(GetRecordDecl(type));
    case llvm::dwarf::DW_TAG_array_type:
      return BuildArray(type);
    case llvm::dwarf::DW_TAG_enumeration_type:
      return GetEnumDecl(type);
    default:
      LOG(FATAL) << "Invalid DICompositeType tag: " << type->getTag();
  }
  return {};
}

std::string StructGenerator::GetUniqueName(
    const std::string& base, std::unordered_set<std::string>& names) {
  auto name{base == "" ? "anon" : base};
  for (unsigned i{2}; names.find(name) != names.end(); ++i) {
    name = MakeValid(base, i);
  }
  names.insert(name);
  return name;
}

void StructGenerator::VisitType(llvm::DIType* t,
                                std::vector<llvm::DICompositeType*>& list,
                                std::unordered_set<llvm::DIType*>& visited) {
  VLOG(1) << "VisitType: " << rellic::LLVMThingToString(t);
  if (!t || visited.count(t)) {
    return;
  }
  visited.insert(t);

  if (auto comp = llvm::dyn_cast<llvm::DICompositeType>(t)) {
    auto tag{comp->getTag()};
    switch (tag) {
      case llvm::dwarf::DW_TAG_class_type:
      case llvm::dwarf::DW_TAG_structure_type:
      case llvm::dwarf::DW_TAG_union_type: {
        GetRecordDecl(comp);
        for (auto field : GetFields(comp)) {
          VisitType(field.type->getBaseType(), list, visited);
        }
      } break;
      case llvm::dwarf::DW_TAG_array_type:
        VisitType(comp->getBaseType(), list, visited);
        break;
      case llvm::dwarf::DW_TAG_enumeration_type: {
        GetEnumDecl(comp);
        VisitType(comp->getBaseType(), list, visited);
      } break;
      default:
        LOG(FATAL) << "Invalid DICompositeType tag: " << comp->getTag();
    }

    if (tag != llvm::dwarf::DW_TAG_array_type &&
        tag != llvm::dwarf::DW_TAG_enumeration_type) {
      VLOG(3) << "Adding " << comp->getName().str() << " to list";
      list.push_back(comp);
    }
  } else if (auto der = llvm::dyn_cast<llvm::DIDerivedType>(t)) {
    auto base{der->getBaseType()};
    switch (der->getTag()) {
      case llvm::dwarf::DW_TAG_pointer_type:
      case llvm::dwarf::DW_TAG_reference_type:
      case llvm::dwarf::DW_TAG_ptr_to_member_type:
      case llvm::dwarf::DW_TAG_rvalue_reference_type:
        /* skip */
        break;
      default:
        VisitType(base, list, visited);
        break;
    }
  } else if (auto basic = llvm::dyn_cast<llvm::DIBasicType>(t)) {
    /* skip */
  } else if (auto sub = llvm::dyn_cast<llvm::DISubroutineType>(t)) {
    for (auto type : sub->getTypeArray()) {
      VisitType(type, list, visited);
    }
  } else {
    LOG(FATAL) << "Unknown DIType: " << rellic::LLVMThingToString(t);
  }
}

clang::QualType StructGenerator::GetType(llvm::DIType* t) {
  return BuildType(t);
}

std::vector<clang::Expr*> StructGenerator::GetAccessor(clang::Expr* base,
                                                       clang::RecordDecl* decl,
                                                       unsigned int offset,
                                                       unsigned int length) {
  std::vector<clang::Expr*> res{};
  auto& layout{ast_ctx.getASTRecordLayout(decl)};

  for (auto field : decl->fields()) {
    auto idx{field->getFieldIndex()};
    auto type{field->getType().getDesugaredType(ast_ctx)};
    auto field_offset{layout.getFieldOffset(idx)};
    auto field_size{field->isBitField() ? field->getBitWidthValue(ast_ctx)
                                        : ast_ctx.getTypeSize(type)};
    if (offset >= field_offset &&
        offset + length <= field_offset + field_size) {
      if (type->isAggregateType()) {
        auto subdecl{type->getAsRecordDecl()};
        auto suboffset{offset - field_offset};
        auto new_base{ast.CreateDot(base, field)};
        for (auto r : GetAccessor(new_base, subdecl, suboffset, length)) {
          res.push_back(r);
        }
      } else if (offset == field_offset && length == field_size) {
        res.push_back(ast.CreateDot(base, field));
      }
    }
  }

  return res;
}

StructGenerator::StructGenerator(clang::ASTUnit& ast_unit)
    : ast_ctx(ast_unit.getASTContext()), ast(ast_unit) {}

}  // namespace rellic
