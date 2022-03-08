/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include "rellic/Decompiler.h"

#include <clang/AST/Decl.h>
#include <clang/Basic/TargetInfo.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/PassManager.h>
#include <llvm/InitializePasses.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/Utils.h>
#include <llvm/Transforms/Utils/Local.h>
#include <llvm/Transforms/Utils/LowerSwitch.h>

#include <iostream>
#include <memory>
#include <system_error>

#include "rellic/AST/CondBasedRefine.h"
#include "rellic/AST/DeadStmtElim.h"
#include "rellic/AST/DebugInfoCollector.h"
#include "rellic/AST/EGraph.h"
#include "rellic/AST/ExprCombine.h"
#include "rellic/AST/GenerateAST.h"
#include "rellic/AST/IRToASTVisitor.h"
#include "rellic/AST/LocalDeclRenamer.h"
#include "rellic/AST/LoopRefine.h"
#include "rellic/AST/NestedCondProp.h"
#include "rellic/AST/NestedScopeCombine.h"
#include "rellic/AST/NormalizeCond.h"
#include "rellic/AST/ReachBasedRefine.h"
#include "rellic/AST/StructFieldRenamer.h"
#include "rellic/AST/Z3CondSimplify.h"
#include "rellic/BC/Util.h"
#include "rellic/Exception.h"

namespace {

static void InitOptPasses(void) {
  auto& pr{*llvm::PassRegistry::getPassRegistry()};
  initializeCore(pr);
  initializeAnalysis(pr);
}
};  // namespace

template <typename TKey, typename TValue>
static void CopyMap(const std::unordered_map<TKey*, TValue*>& from,
                    std::unordered_map<const TKey*, const TValue*>& to,
                    std::unordered_map<const TValue*, const TKey*>& inverse) {
  for (auto [key, value] : from) {
    if (value) {
      to[key] = value;
      inverse[value] = key;
    }
  }
}

template <typename TKey, typename TValue>
static void CopyMap(
    const std::unordered_multimap<TKey*, TValue*>& from,
    std::unordered_multimap<const TKey*, const TValue*>& to,
    std::unordered_multimap<const TValue*, const TKey*>& inverse) {
  for (auto [key, value] : from) {
    if (value) {
      to.insert({key, value});
      inverse.insert({value, key});
    }
  }
}

namespace rellic {
class DeclToEGraph : public clang::RecursiveASTVisitor<DeclToEGraph> {
  EGraph& eg;

 public:
  DeclToEGraph(EGraph& eg) : eg(eg) {}

  bool VisitStmt(clang::Stmt* stmt) {
    eg.AddNode(stmt);
    return true;
  }
};

class EGraphToDecl : public clang::RecursiveASTVisitor<EGraphToDecl> {
  EGraph& eg;

 public:
  EGraphToDecl(EGraph& eg) : eg(eg) {}

  bool TraverseFunctionDecl(clang::FunctionDecl* decl) {
    if (decl->hasBody()) {
      decl->setBody(eg.Schedule(eg.AddNode(decl->getBody())));
    }
    return true;
  }
};

Result<DecompilationResult, DecompilationError> Decompile(
    std::unique_ptr<llvm::Module> module, DecompilationOptions options) {
  try {
    if (options.remove_phi_nodes) {
      RemovePHINodes(*module);
    }

    if (options.lower_switches) {
      LowerSwitches(*module);
    }

    ConvertArrayArguments(*module);
    RemoveInsertValues(*module);

    InitOptPasses();
    rellic::DebugInfoCollector dic;
    dic.visit(*module);

    std::vector<std::string> args{"-Wno-pointer-to-int-cast", "-target",
                                  module->getTargetTriple()};
    auto ast_unit{clang::tooling::buildASTFromCodeWithArgs("", args, "out.c")};
    auto tu_decl{ast_unit->getASTContext().getTranslationUnitDecl()};

    rellic::StmtToIRMap provenance;
    rellic::IRToTypeDeclMap type_decls;
    rellic::IRToValDeclMap value_decls;
    rellic::IRToStmtMap stmts;
    rellic::ArgToTempMap temp_decls;
    rellic::GenerateAST::run(*module, provenance, *ast_unit, type_decls,
                             value_decls, stmts, temp_decls);
    rellic::LocalDeclRenamer ldr(*ast_unit, dic.GetIRToNameMap(), value_decls);
    ldr.TraverseDecl(tu_decl);

    rellic::StructFieldRenamer sfr(*ast_unit, dic.GetIRTypeToDITypeMap(),
                                   type_decls);
    sfr.TraverseDecl(tu_decl);
    // TODO(surovic): Add llvm::Value* -> clang::Decl* map
    // Especially for llvm::Argument* and llvm::Function*.

    rellic::Substitutions substitutions;
    rellic::DeadStmtElim dse(provenance, *ast_unit, substitutions);
    rellic::Z3CondSimplify zcs(provenance, *ast_unit, substitutions);
    rellic::NestedCondProp ncp(provenance, *ast_unit, substitutions);
    rellic::NestedScopeCombine nsc(provenance, *ast_unit, substitutions);
    rellic::CondBasedRefine cbr(provenance, *ast_unit, substitutions);
    rellic::ReachBasedRefine rbr(provenance, *ast_unit, substitutions);
    rellic::LoopRefine lr(provenance, *ast_unit, substitutions);
    rellic::ExprCombine ec(provenance, *ast_unit, substitutions);
    rellic::EGraph eg;
    DeclToEGraph dteg(eg);
    dteg.TraverseDecl(tu_decl);
    eg.Rebuild();
    std::string s;
    llvm::raw_string_ostream os(s);
    eg.PrintGraph(os);
    std::cout << s << std::endl;

    auto Run = [&](clang::Stmt* stmt) {
      dse.Run(stmt);
      zcs.Run(stmt);
      ncp.Run(stmt);
      nsc.Run(stmt);
      cbr.Run(stmt);
      rbr.Run(stmt);
      lr.Run(stmt);
      ec.Run(stmt);
    };

    int max{0};
    while (max < 3) {
      auto version{eg.GetVersion()};
      auto classes{eg.GetClasses()};
      for (auto& [id, nodes] : classes) {
        for (auto& node : nodes) {
          Run(node.stmt);
        }
      }
      for (auto& sub : substitutions) {
        auto a{eg.AddNode(CHECK_NOTNULL(sub.before))};
        auto b{eg.AddNode(CHECK_NOTNULL(sub.after))};
        eg.Merge(a, b);
      }
      eg.Rebuild();
      std::string s;
      llvm::raw_string_ostream os(s);
      eg.PrintGraph(os);
      std::cout << s << std::endl;
      if (eg.GetVersion() == version) {
        LOG(INFO) << "Stopping";
        break;
      }
      substitutions.clear();
      max++;
    }

    EGraphToDecl egtd(eg);
    egtd.TraverseDecl(tu_decl);

    DecompilationResult result{};
    result.ast = std::move(ast_unit);
    result.module = std::move(module);
    CopyMap(provenance, result.stmt_provenance_map, result.value_to_stmt_map);
    CopyMap(value_decls, result.value_to_decl_map, result.decl_provenance_map);
    CopyMap(type_decls, result.type_to_decl_map, result.type_provenance_map);

    return Result<DecompilationResult, DecompilationError>(std::move(result));
  } catch (Exception& ex) {
    DecompilationError error{};
    error.message = ex.what();
    error.module = std::move(module);
    return Result<DecompilationResult, DecompilationError>(std::move(error));
  }
}
}  // namespace rellic