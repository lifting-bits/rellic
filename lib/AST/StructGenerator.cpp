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
#include <gflags/gflags.h>
#include <glog/logging.h>

#include <string>
#include <unordered_set>

#include "rellic/AST/Compat/ASTContext.h"
#include "rellic/BC/Util.h"

namespace rellic {
clang::QualType StructGenerator::VisitType(llvm::DIType* t, int sizeHint) {
  VLOG(2) << "VisitType: " << rellic::LLVMThingToString(t);
  if (!t) {
    return ast_ctx.VoidTy;
  }
  auto& type{types[t]};
  if (type.isNull()) {
    if (auto comp = llvm::dyn_cast<llvm::DICompositeType>(t)) {
      type = VisitComposite(comp);
    } else if (auto der = llvm::dyn_cast<llvm::DIDerivedType>(t)) {
      type = VisitDerived(der);
    } else if (auto basic = llvm::dyn_cast<llvm::DIBasicType>(t)) {
      type = VisitBasic(basic, sizeHint);
    } else if (auto sub = llvm::dyn_cast<llvm::DISubroutineType>(t)) {
      type = VisitSubroutine(sub);
    } else {
      LOG(FATAL) << "Unknown DIType: " << rellic::LLVMThingToString(t);
    }
  }
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

static std::vector<llvm::DIDerivedType*> GetFields(
    llvm::DICompositeType* composite) {
  std::vector<llvm::DIDerivedType*> fields{};
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
        if (sub_fields.size() == 0) {
          // Ignore empty base types
          return;
        }
      }

      fields.push_back(type);
    }
  });
  std::sort(fields.begin(), fields.end(), [](auto a, auto b) {
    return a->getOffsetInBits() < b->getOffsetInBits();
  });

  return fields;
}

void StructGenerator::VisitFields(
    clang::RecordDecl* decl, llvm::DICompositeType* s,
    std::unordered_map<clang::FieldDecl*, llvm::DIDerivedType*>& map,
    bool isUnion) {
  auto elems{GetFields(s)};

  auto count{0U};
  auto curr_offset{0U};
  for (auto elem : elems) {
    DLOG(INFO) << "Field " << elem->getName().str()
               << " offset: " << curr_offset;
    CHECK_LE(curr_offset, elem->getOffsetInBits())
        << "Field " << LLVMThingToString(elem)
        << " cannot be correctly aligned";
    if (curr_offset < elem->getOffsetInBits()) {
      auto padding_type{ast_ctx.CharTy};
      auto type_size{ast_ctx.getTypeSize(padding_type)};
      auto needed_padding{elem->getOffsetInBits() - curr_offset};
      clang::FieldDecl* padding_decl{};
      if (needed_padding % type_size) {
        padding_decl = ast.CreateFieldDecl(decl, ast_ctx.LongLongTy,
                                           "padding_" + std::to_string(count++),
                                           needed_padding);
      } else {
        auto padding_count{needed_padding / type_size};
        auto padding_arr_type{
            rellic::GetConstantArrayType(ast_ctx, padding_type, padding_count)};
        padding_decl = ast.CreateFieldDecl(
            decl, padding_arr_type, "padding_" + std::to_string(count++));
      }
      decl->addDecl(padding_decl);
      curr_offset = elem->getOffsetInBits();
    }

    std::string name{elem->getName().str()};
    if (name == "") {
      name = "anon";
    }
    name = name + "_" + std::to_string(count++);

    auto type{VisitType(elem->getBaseType(), elem->getSizeInBits())};
    CHECK(!type.isNull());
    clang::FieldDecl* fdecl{};
    if (elem->getFlags() & llvm::DINode::DIFlags::FlagBitField) {
      fdecl = ast.CreateFieldDecl(decl, type, name, elem->getSizeInBits());
    } else {
      fdecl = ast.CreateFieldDecl(decl, type, name);
    }

    if (!isUnion) {
      if (elem->getSizeInBits()) {
        curr_offset += elem->getSizeInBits();
      } else {
        curr_offset += ast_ctx.getTypeSize(type);
      }
    }
    map[fdecl] = elem;
    decl->addDecl(fdecl);
  }
  decl->completeDefinition();
}

clang::QualType StructGenerator::VisitStruct(llvm::DICompositeType* s) {
  VLOG(1) << "VisitStruct: " << rellic::LLVMThingToString(s);
  auto& decl{record_decls[s]};
  if (decl) {
    return ast_ctx.getRecordType(decl);
  }

  std::string name{s->getName().str()};
  if (name == "") {
    name = "anon";
  }
  name += "_" + std::to_string(decl_count++);

  decl = ast.CreateStructDecl(ast_ctx.getTranslationUnitDecl(), name);
  clang::AttributeCommonInfo info{clang::SourceLocation{}};
  decl->addAttr(clang::PackedAttr::Create(ast_ctx, info));
  std::unordered_map<clang::FieldDecl*, llvm::DIDerivedType*> fmap{};
  VisitFields(decl, s, fmap, /*isUnion=*/false);
  ast_ctx.getTranslationUnitDecl()->addDecl(decl);
  // TODO(frabert): Ideally we'd also check that the size as declared in the
  // debug data is the same as the one computed from the declaration, but
  // bitfields create issues e.g. in the `bitcode` test: the debug info size
  // for the struct is 32, but in reality it's 8

  // auto& layout{ast_ctx.getASTRecordLayout(decl)};
  // auto i{0U};
  // for (auto field : decl->fields()) {
  //  auto type{fmap[field]};
  //  if (type) {
  //    CHECK_EQ(layout.getFieldOffset(i), type->getOffsetInBits())
  //        << "Field " << field->getName().str() << " of struct "
  //        << decl->getName().str() << " is not correctly aligned";
  //  }
  //  ++i;
  //}
  return ast_ctx.getRecordType(decl);
}

clang::QualType StructGenerator::VisitUnion(llvm::DICompositeType* u) {
  VLOG(1) << "VisitUnion: " << rellic::LLVMThingToString(u);
  auto& decl{record_decls[u]};
  if (decl) {
    return ast_ctx.getRecordType(decl);
  }

  std::string name{u->getName().str()};
  if (name == "") {
    name = "anon";
  }
  name += "_" + std::to_string(decl_count++);

  decl = ast.CreateUnionDecl(ast_ctx.getTranslationUnitDecl(), name);
  std::unordered_map<clang::FieldDecl*, llvm::DIDerivedType*> fmap{};
  VisitFields(decl, u, fmap, /*isUnion=*/true);
  ast_ctx.getTranslationUnitDecl()->addDecl(decl);
  return ast_ctx.getRecordType(decl);
}

clang::QualType StructGenerator::VisitEnum(llvm::DICompositeType* e) {
  VLOG(1) << "VisitEnum: " << rellic::LLVMThingToString(e);
  auto& decl{enum_decls[e]};
  if (decl) {
    return ast_ctx.getEnumType(decl);
  }

  std::string name{e->getName().str()};
  if (name == "") {
    name = "anon";
  }
  name += "_" + std::to_string(decl_count++);
  auto tudecl{ast_ctx.getTranslationUnitDecl()};
  decl = ast.CreateEnumDecl(tudecl, name);

  auto base{VisitType(e->getBaseType())};
  auto i{0U};
  for (auto elem : e->getElements()) {
    if (auto enumerator = llvm::dyn_cast<llvm::DIEnumerator>(elem)) {
      auto elem_name{enumerator->getName().str()};
      if (elem_name == "") {
        elem_name = "anon";
      }
      elem_name += "_" + std::to_string(i++);

      auto cdecl{ast.CreateEnumConstantDecl(decl, elem_name, base,
                                            enumerator->getValue())};
      decl->addDecl(cdecl);
    }
  }
  decl->completeDefinition(base, base, 0, 0);
  tudecl->addDecl(decl);

  return ast_ctx.getEnumType(decl);
}

clang::QualType StructGenerator::VisitArray(llvm::DICompositeType* a) {
  VLOG(1) << "VisitArray: " << rellic::LLVMThingToString(a);
  auto base{VisitType(a->getBaseType())};
  auto subrange{llvm::cast<llvm::DISubrange>(a->getElements()[0])};
  auto* ci = subrange->getCount().dyn_cast<llvm::ConstantInt*>();
  return rellic::GetConstantArrayType(ast_ctx, base, ci->getSExtValue());
}

clang::QualType StructGenerator::VisitDerived(llvm::DIDerivedType* d) {
  VLOG(2) << "VisitDerived: " << rellic::LLVMThingToString(d);
  auto base{VisitType(d->getBaseType())};
  switch (d->getTag()) {
    case llvm::dwarf::DW_TAG_const_type:
      return ast_ctx.getConstType(base);
    case llvm::dwarf::DW_TAG_volatile_type:
      return ast_ctx.getVolatileType(base);
    case llvm::dwarf::DW_TAG_restrict_type:
      return ast_ctx.getRestrictType(base);
    case llvm::dwarf::DW_TAG_pointer_type:
    case llvm::dwarf::DW_TAG_reference_type:
    case llvm::dwarf::DW_TAG_rvalue_reference_type:
    case llvm::dwarf::DW_TAG_ptr_to_member_type:
      return ast_ctx.getPointerType(base);
    case llvm::dwarf::DW_TAG_typedef: {
      auto& tdef_decl{typedef_decls[d]};
      if (!tdef_decl) {
        auto tudecl{ast_ctx.getTranslationUnitDecl()};
        tdef_decl = ast.CreateTypedefDecl(
            tudecl, d->getName().str() + "_" + std::to_string(decl_count++),
            base);
        tudecl->addDecl(tdef_decl);
      }
      return ast_ctx.getTypedefType(tdef_decl);
    }
    default:
      LOG(FATAL) << "Unknown DIDerivedType: " << rellic::LLVMThingToString(d);
  }
  return {};
}

clang::QualType StructGenerator::VisitBasic(llvm::DIBasicType* b,
                                            int sizeHint) {
  VLOG(1) << "VisitBasic: " << rellic::LLVMThingToString(b);
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

  if (b->getEncoding() == llvm::dwarf::DW_ATE_float) {
    return ast_ctx.getRealTypeForBitwidth(b->getSizeInBits(), false);
  } else {
    auto char_size{ast_ctx.getTypeSize(ast_ctx.CharTy)};
    auto char_signed{ast_ctx.CharTy->isSignedIntegerType()};
    auto b_size{b->getSizeInBits()};
    auto b_signed{b->getSignedness() == llvm::DIBasicType::Signedness::Signed};
    if (char_size == b_size && char_signed == b_signed) {
      return ast_ctx.CharTy;
    } else {
      return ast_ctx.getIntTypeForBitwidth(b_size, b_signed);
    }
  }
}

clang::QualType StructGenerator::VisitSubroutine(llvm::DISubroutineType* s) {
  VLOG(1) << "VisitSubroutine: " << rellic::LLVMThingToString(s);
  auto type_array{s->getTypeArray()};
  auto epi{clang::FunctionProtoType::ExtProtoInfo()};
  if (type_array.size() == 0) {
    // TODO(frabert): I'm not sure why this happens or how to best handle this.
    // I'm going to guess it's a "void(void)"
    return ast_ctx.getFunctionType(ast_ctx.VoidTy, {}, epi);
  }

  auto ret_type{VisitType(type_array[0])};
  std::vector<clang::QualType> params{};
  for (auto i{1U}; i < type_array.size(); ++i) {
    auto t{type_array[i]};
    if (!t) {
      CHECK_EQ(i, type_array.size() - 1)
          << "Only last argument type can be null";
      epi.Variadic = true;
    } else {
      params.push_back(VisitType(t));
    }
  }
  return ast_ctx.getFunctionType(ret_type, params, epi);
}

clang::QualType StructGenerator::VisitComposite(llvm::DICompositeType* type) {
  VLOG(2) << "VisitComposite: " << rellic::LLVMThingToString(type);
  switch (type->getTag()) {
    case llvm::dwarf::DW_TAG_class_type:
      DLOG(INFO) << "Treating class declaration as struct";
      [[fallthrough]];
    case llvm::dwarf::DW_TAG_structure_type:
      return VisitStruct(type);
    case llvm::dwarf::DW_TAG_union_type:
      return VisitUnion(type);
    case llvm::dwarf::DW_TAG_array_type:
      return VisitArray(type);
    case llvm::dwarf::DW_TAG_enumeration_type:
      return VisitEnum(type);
    default:
      LOG(FATAL) << "Invalid DICompositeType tag: " << type->getTag();
  }
  return {};
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
