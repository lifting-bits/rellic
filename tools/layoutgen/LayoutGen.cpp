/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */
#include <clang/AST/ASTContext.h>
#include <clang/AST/Attr.h>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclBase.h>
#include <clang/AST/RecordLayout.h>
#include <clang/AST/Type.h>
#include <clang/Basic/AttributeCommonInfo.h>
#include <clang/Basic/SourceLocation.h>
#include <clang/Frontend/ASTUnit.h>
#include <clang/Tooling/Tooling.h>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <llvm/BinaryFormat/Dwarf.h>
#include <llvm/IR/DebugInfoMetadata.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/Support/Host.h>
#include <llvm/Support/JSON.h>
#include <llvm/Support/raw_ostream.h>
#include <rellic/AST/Compat/ASTContext.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <streambuf>
#include <string>
#include <unordered_map>

#include "rellic/AST/ASTBuilder.h"
#include "rellic/AST/DebugInfoCollector.h"
#include "rellic/BC/Util.h"
#include "rellic/Version/Version.h"

#ifndef LLVM_VERSION_STRING
#define LLVM_VERSION_STRING LLVM_VERSION_MAJOR << "." << LLVM_VERSION_MINOR
#endif

DEFINE_string(input, "", "Input file.");
DEFINE_string(output, "", "Output file.");

DECLARE_bool(version);

static void SetVersion(void) {
  std::stringstream version;

  auto vs = rellic::Version::GetVersionString();
  if (0 == vs.size()) {
    vs = "unknown";
  }
  version << vs << "\n";
  if (!rellic::Version::HasVersionData()) {
    version << "No extended version information found!\n";
  } else {
    version << "Commit Hash: " << rellic::Version::GetCommitHash() << "\n";
    version << "Commit Date: " << rellic::Version::GetCommitDate() << "\n";
    version << "Last commit by: " << rellic::Version::GetAuthorName() << " ["
            << rellic::Version::GetAuthorEmail() << "]\n";
    version << "Commit Subject: [" << rellic::Version::GetCommitSubject()
            << "]\n";
    version << "\n";
    if (rellic::Version::HasUncommittedChanges()) {
      version << "Uncommitted changes were present during build.\n";
    } else {
      version << "All changes were committed prior to building.\n";
    }
  }
  version << "Using LLVM " << LLVM_VERSION_STRING << std::endl;

  google::SetVersionString(version.str());
}

class StructGenerator {
  clang::ASTContext& ast_ctx;
  rellic::ASTBuilder ast;
  std::unordered_map<llvm::DICompositeType*, clang::RecordDecl*> decls{};
  std::unordered_map<llvm::DIDerivedType*, clang::TypedefNameDecl*>
      typedef_decls{};
  unsigned anon_count{0};

  clang::QualType VisitType(llvm::DIType* t) {
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

  void VisitFields(
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
        auto padding_count{needed_padding / type_size};
        auto padding_arr_type{
            rellic::GetConstantArrayType(ast_ctx, padding_type, padding_count)};
        auto padding_decl{ast.CreateFieldDecl(
            decl, padding_arr_type, "padding_" + std::to_string(count++))};
        decl->addDecl(padding_decl);
        curr_offset += padding_count * type_size;
      }

      std::string name{elem->getName().str()};
      if (name == "") {
        name = "anon";
      }
      name = name + "_" + std::to_string(count++);
      CHECK_EQ(curr_offset, elem->getOffsetInBits())
          << "Couldn't correctly offset field " << name;
      auto type{VisitType(elem->getBaseType())};
      auto fdecl{ast.CreateFieldDecl(decl, type, name)};
      map[fdecl] = elem;
      decl->addDecl(fdecl);
      if (!isUnion) {
        curr_offset += ast_ctx.getTypeSize(type);
      }
    }
    decl->completeDefinition();
  }

  clang::QualType VisitStruct(llvm::DICompositeType* s) {
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

  clang::QualType VisitUnion(llvm::DICompositeType* u) {
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

  clang::QualType VisitArray(llvm::DICompositeType* a) {
    DLOG(INFO) << "VisitArray: " << rellic::LLVMThingToString(a);
    auto base{VisitType(a->getBaseType())};
    auto subrange{llvm::cast<llvm::DISubrange>(a->getElements()[0])};
    auto* ci = subrange->getCount().dyn_cast<llvm::ConstantInt*>();
    return rellic::GetConstantArrayType(ast_ctx, base, ci->getSExtValue());
  }

  clang::QualType VisitDerived(llvm::DIDerivedType* d) {
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

  clang::QualType VisitBasic(llvm::DIBasicType* b) {
    DLOG(INFO) << "VisitBasic: " << rellic::LLVMThingToString(b);
    if (b->getEncoding() == llvm::dwarf::DW_ATE_float) {
      return ast_ctx.getRealTypeForBitwidth(b->getSizeInBits(), false);
    } else {
      return ast_ctx.getIntTypeForBitwidth(
          b->getSizeInBits(),
          b->getSignedness() == llvm::DIBasicType::Signedness::Signed);
    }
  }

  clang::QualType VisitSubroutine(llvm::DISubroutineType* s) {
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

  clang::QualType VisitComposite(llvm::DICompositeType* type) {
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

 public:
  StructGenerator(clang::ASTUnit& ast_unit)
      : ast_ctx(ast_unit.getASTContext()), ast(ast_unit) {}

  void Visit(const std::vector<llvm::DICompositeType*>& coll) {
    for (auto type : coll) {
      VisitComposite(type);
    }
  }
};

int main(int argc, char* argv[]) {
  std::stringstream usage;
  usage << std::endl
        << std::endl
        << "  " << argv[0] << " \\" << std::endl
        << "    --input INPUT_FILE \\" << std::endl
        << "    --output OUTPUT_FILE \\" << std::endl
        << std::endl

        // Print the version and exit.
        << "    [--version]" << std::endl
        << std::endl;

  google::InitGoogleLogging(argv[0]);
  google::InstallFailureSignalHandler();
  google::SetUsageMessage(usage.str());
  SetVersion();
  google::ParseCommandLineFlags(&argc, &argv, true);

  LOG_IF(ERROR, FLAGS_input.empty())
      << "Must specify the path to an input file.";

  LOG_IF(ERROR, FLAGS_output.empty())
      << "Must specify the path to an output file.";

  if (FLAGS_input.empty() || FLAGS_output.empty()) {
    std::cerr << google::ProgramUsage();
    return EXIT_FAILURE;
  }

  auto llvm_ctx{std::make_unique<llvm::LLVMContext>()};
  auto module{rellic::LoadModuleFromFile(llvm_ctx.get(), FLAGS_input)};
  auto dic{std::make_unique<rellic::DebugInfoCollector>()};
  dic->visit(module);
  std::vector<std::string> args{"-Wno-pointer-to-int-cast", "-target",
                                module->getTargetTriple()};
  auto ast_unit{clang::tooling::buildASTFromCodeWithArgs("", args, "out.c")};
  StructGenerator gen(*ast_unit);
  gen.Visit(dic->GetStructs());

  std::error_code ec;
  // FIXME(surovic): Figure out if the fix below works.
  // llvm::raw_fd_ostream output(FLAGS_output, ec, llvm::sys::fs::F_Text);
  llvm::raw_fd_ostream output(FLAGS_output, ec);
  ast_unit->getASTContext().getTranslationUnitDecl()->print(output);
  CHECK(!ec) << "Failed to create output file: " << ec.message();

  google::ShutDownCommandLineFlags();
  google::ShutdownGoogleLogging();

  return EXIT_SUCCESS;
}
