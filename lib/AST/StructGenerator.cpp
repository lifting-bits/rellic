/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */
#include "rellic/AST/StructGenerator.h"

#include <clang/AST/Attr.h>
#include <clang/AST/RecordLayout.h>
#include <gflags/gflags.h>
#include <glog/logging.h>

#include <unordered_set>

#include "rellic/AST/Compat/ASTContext.h"
#include "rellic/BC/Util.h"

namespace rellic {
clang::QualType StructGenerator::VisitType(llvm::DIType* t) {
  DLOG(INFO) << "VisitType: " << rellic::LLVMThingToString(t);
  if (!t) {
    return ast_ctx.VoidTy;
  }

  if (auto comp = llvm::dyn_cast<llvm::DICompositeType>(t)) {
    return VisitComposite(comp);
  } else if (auto der = llvm::dyn_cast<llvm::DIDerivedType>(t)) {
    return VisitDerived(der);
  } else if (auto basic = llvm::dyn_cast<llvm::DIBasicType>(t)) {
    return VisitBasic(basic);
  } else if (auto sub = llvm::dyn_cast<llvm::DISubroutineType>(t)) {
    return VisitSubroutine(sub);
  } else {
    LOG(FATAL) << "Unknown DIType: " << rellic::LLVMThingToString(t);
  }
  return {};
}

void StructGenerator::VisitFields(
    clang::RecordDecl* decl, llvm::DICompositeType* s,
    std::unordered_map<clang::FieldDecl*, llvm::DIDerivedType*>& map,
    bool isUnion) {
  std::vector<llvm::DIDerivedType*> elems{};
  auto nodes{s->getElements()};
  std::for_each(nodes.begin(), nodes.end(), [&](auto node) {
    if (auto type = llvm::dyn_cast<llvm::DIDerivedType>(node)) {
      elems.push_back(type);
    }
  });
  std::sort(elems.begin(), elems.end(), [](auto a, auto b) {
    return a->getOffsetInBits() < b->getOffsetInBits();
  });

  auto count{0U};
  auto curr_offset{0U};
  for (auto elem : elems) {
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
    name = "anon_struct_" + std::to_string(anon_count++);
  } else {
    name = "struct_" + name;
  }

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
    name = "anon_union_" + std::to_string(anon_count++);
  } else {
    name = "union_" + name;
  }

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
        tdef_decl = ast.CreateTypedefDecl(tudecl, d->getName().str(), base);
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

StructGenerator::StructGenerator(clang::ASTUnit& ast_unit)
    : ast_ctx(ast_unit.getASTContext()), ast(ast_unit) {}

}  // namespace rellic
