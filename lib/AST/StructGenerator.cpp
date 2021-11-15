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
clang::QualType StructGenerator::VisitType(llvm::DIType* t) {
  DLOG(INFO) << "VisitType: " << rellic::LLVMThingToString(t);
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
      type = VisitBasic(basic);
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
    CHECK_LE(curr_offset, elem->getOffsetInBits())
        << "Field " << LLVMThingToString(elem)
        << " cannot be correctly aligned";
    if (curr_offset < elem->getOffsetInBits()) {
      auto padding_type{ast_ctx.CharTy};
      auto type_size{ast_ctx.getTypeSize(padding_type)};
      auto needed_padding{elem->getOffsetInBits() - curr_offset};
      clang::FieldDecl* padding_decl{};
      if (needed_padding % type_size) {
        padding_decl = ast.CreateFieldDecl(decl, ast_ctx.IntTy,
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

    auto type{VisitType(elem->getBaseType())};
    clang::FieldDecl* fdecl{};
    if (elem->getFlags() & llvm::DINode::DIFlags::FlagBitField) {
      fdecl = ast.CreateFieldDecl(decl, type, name, elem->getSizeInBits());
      if (!isUnion) {
        curr_offset += elem->getSizeInBits();
      }
    } else {
      fdecl = ast.CreateFieldDecl(decl, type, name);
      if (!isUnion) {
        curr_offset += ast_ctx.getTypeSize(type);
      }
    }
    map[fdecl] = elem;
    decl->addDecl(fdecl);
  }
  decl->completeDefinition();
}

clang::QualType StructGenerator::VisitStruct(llvm::DICompositeType* s) {
  DLOG(INFO) << "VisitStruct: " << rellic::LLVMThingToString(s);
  auto& decl{decls[s]};
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
  auto& layout{ast_ctx.getASTRecordLayout(decl)};
  auto i{0U};
  // TODO(frabert): Ideally we'd also check that the size as declared in the
  // debug data is the same as the one computed from the declaration, but
  // bitfields create issues e.g. in the `bitcode` test: the debug info size
  // for the struct is 32, but in reality it's 8
  for (auto field : decl->fields()) {
    auto type{fmap[field]};
    if (type) {
      CHECK_EQ(layout.getFieldOffset(i), type->getOffsetInBits())
          << "Field " << field->getName().str() << " of struct "
          << decl->getName().str() << " is not correctly aligned";
    }
    ++i;
  }
  return ast_ctx.getRecordType(decl);
}

clang::QualType StructGenerator::VisitUnion(llvm::DICompositeType* u) {
  DLOG(INFO) << "VisitUnion: " << rellic::LLVMThingToString(u);
  auto& decl{decls[u]};
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

clang::QualType StructGenerator::VisitArray(llvm::DICompositeType* a) {
  DLOG(INFO) << "VisitArray: " << rellic::LLVMThingToString(a);
  auto base{VisitType(a->getBaseType())};
  auto subrange{llvm::cast<llvm::DISubrange>(a->getElements()[0])};
  auto* ci = subrange->getCount().dyn_cast<llvm::ConstantInt*>();
  return rellic::GetConstantArrayType(ast_ctx, base, ci->getSExtValue());
}

clang::QualType StructGenerator::VisitDerived(llvm::DIDerivedType* d) {
  DLOG(INFO) << "VisitDerived: " << rellic::LLVMThingToString(d);
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

clang::QualType StructGenerator::VisitBasic(llvm::DIBasicType* b) {
  DLOG(INFO) << "VisitBasic: " << rellic::LLVMThingToString(b);
  if (b->getEncoding() == llvm::dwarf::DW_ATE_float) {
    return ast_ctx.getRealTypeForBitwidth(b->getSizeInBits(), false);
  } else {
    return ast_ctx.getIntTypeForBitwidth(
        b->getSizeInBits(),
        b->getSignedness() == llvm::DIBasicType::Signedness::Signed);
  }
}

clang::QualType StructGenerator::VisitSubroutine(llvm::DISubroutineType* s) {
  DLOG(INFO) << "VisitSubroutine: " << rellic::LLVMThingToString(s);
  auto type_array{s->getTypeArray()};
  auto ret_type{VisitType(type_array[0])};
  std::vector<clang::QualType> params{};
  auto epi{clang::FunctionProtoType::ExtProtoInfo()};
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
  DLOG(INFO) << "VisitComposite: " << rellic::LLVMThingToString(type);
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
    auto field_size{ast_ctx.getTypeSize(type)};
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
