/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include "rellic/Decompiler.h"

#include <clang/AST/Decl.h>
#include <clang/AST/Expr.h>
#include <clang/AST/OperationKinds.h>
#include <clang/AST/Stmt.h>
#include <clang/AST/StmtVisitor.h>
#include <clang/Basic/TargetInfo.h>
#include <clang/Frontend/ASTUnit.h>
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

#include "rellic/AST/DebugInfoCollector.h"
#include "rellic/AST/EGraph.h"
#include "rellic/AST/GenerateAST.h"
#include "rellic/AST/IRToASTVisitor.h"
#include "rellic/AST/LocalDeclRenamer.h"
#include "rellic/AST/StructFieldRenamer.h"
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
class DeclToEGraph : public clang::StmtVisitor<DeclToEGraph> {
  EGraph& eg;

 public:
  DeclToEGraph(EGraph& eg) : eg(eg) {}

  void VisitStmt(clang::Stmt* stmt) { eg.AddNode(stmt, "StmtGen"); }
};

using namespace rellic::matchers;

static auto deadStmt(ClassMap& classes, EClass id)
    -> decltype(either(any())(classes, id)) {
  auto terminal = stmtClass([](auto c) {
    switch (c) {
      default:
        return false;
      case clang::Stmt::DeclRefExprClass:
      case clang::Stmt::CharacterLiteralClass:
      case clang::Stmt::IntegerLiteralClass:
      case clang::Stmt::FloatingLiteralClass:
        return true;
    }
  });
  auto deadCast = cast(deadStmt) >> [](std::tuple<EClass, EClass> tup) {
    return std::get<0>(tup);
  };
  auto deadUnop =
      unop(
          [](auto opcode) {
            return !clang::UnaryOperator::isIncrementDecrementOp(opcode);
          },
          deadStmt) >>
      [](std::tuple<EClass, clang::UnaryOperatorKind, EClass> tup) {
        return std::get<0>(tup);
      };
  auto deadBinop =
      binop(
          [](auto opcode) {
            return !clang::BinaryOperator::isAssignmentOp(opcode);
          },
          deadStmt, deadStmt) >>
      [](std::tuple<EClass, clang::BinaryOperatorKind, EClass, EClass> tup) {
        return std::get<0>(tup);
      };
  auto deadParen = paren(deadStmt) >> [](std::tuple<EClass, EClass> tup) {
    return std::get<0>(tup);
  };
  return either(null(), nullStmt(), terminal, deadCast, deadParen, deadUnop,
                deadBinop)(classes, id);
}

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

    rellic::EGraph eg;
    std::unordered_map<clang::FunctionDecl*, EClass> bodies;
    for (auto decl : tu_decl->decls()) {
      if (auto fdecl = clang::dyn_cast<clang::FunctionDecl>(decl)) {
        if (fdecl->hasBody() && fdecl->isThisDeclarationADefinition()) {
          auto node{eg.AddNode(fdecl->getBody(), "StmtGen")};
          bodies[fdecl] = node;
        }
      }
    }
    eg.Rebuild();
    std::string s;
    llvm::raw_string_ostream os(s);
    eg.PrintGraph(os);
    std::cout << s << std::endl;

    int max{0};
    while (max < 10) {
      auto version{eg.GetVersion()};
      auto classes{eg.GetClasses()};
      std::vector<std::pair<EClass, EClass>> merges;
      for (auto& [id, nodes] : classes) {
        using namespace rellic::matchers;
        {
          // if(1) { a } -> a
          auto ifMatch =
              ifStmt(literalTrue(), any(), either(literalTrue(), null()));
          auto res{ifMatch(classes, id)};
          if (res) {
            auto val{res.GetValue()};
            merges.emplace_back(std::get<0>(val), std::get<2>(val));
          }
        }

        {
          // { {a} } -> { a }
          auto inner = compound(filter(any())) >>
                       [](std::tuple<EClass, std::vector<EClass>> tup) {
                         return std::get<1>(tup);
                       };
          auto other =
              any() >> [](EClass id) { return std::vector<EClass>({id}); };

          auto outer = compound(filter(either(inner, other)));
          auto res{outer(classes, id)};
          if (res) {
            auto old_class{std::get<0>(res.GetValue())};
            auto bodies{std::get<1>(res.GetValue())};
            std::vector<EClass> body;
            for (auto& sub : bodies) {
              body.insert(body.end(), sub.begin(), sub.end());
            }
            ENode node(old_class.id, clang::Stmt::CompoundStmtClass, body);
            node.label = "NSC";
            auto new_class{eg.AddENode(node)};
            merges.emplace_back(old_class, new_class);
          }
        }

        {
          auto comp = binop(
              [](auto opc) {
                return clang::BinaryOperator::isComparisonOp(opc);
              },
              any(), any());
          auto neg = unop([](auto opc) { return opc == clang::UO_LNot; }, comp);
          auto res{neg(classes, id)};
          if (res) {
            auto val{res.GetValue()};
            auto old_class{std::get<0>(val)};
            auto bin{std::get<2>(val)};
            auto old_opc{std::get<1>(bin)};
            auto lhs{std::get<2>(bin)};
            auto rhs{std::get<3>(bin)};

            auto node{ENode(old_class.id,
                            clang::BinaryOperator::negateComparisonOp(old_opc),
                            lhs, rhs)};
            node.label = "EC";
            merges.emplace_back(old_class, eg.AddENode(node));
          }
        }

        {
          // *&a -> a
          auto addrof = unop(
              [](auto opcode) { return opcode == clang::UO_AddrOf; }, any());
          auto deref = unop(
              [](auto opcode) { return opcode == clang::UO_Deref; }, addrof);
          auto res{deref(classes, id)};
          if (res) {
            auto val{res.GetValue()};
            merges.emplace_back(std::get<0>(val),
                                std::get<2>(std::get<2>(val)));
          }
        }

        {
          // (a) -> a
          auto p = paren(any());
          auto res{p(classes, id)};
          if (res) {
            auto val{res.GetValue()};
            auto old_class{std::get<0>(val)};
            auto new_class{std::get<1>(val)};
            merges.emplace_back(old_class, new_class);
          }
        }

        {
          auto comp = compound(filter(notMatch(deadStmt)));

          auto res{comp(classes, id)};
          if (res) {
            auto val{res.GetValue()};
            auto old_class{std::get<0>(val)};
            auto body{std::get<1>(val)};
            ENode node(old_class.id, clang::Stmt::CompoundStmtClass, body);
            node.label = "DSE";
            auto new_class{eg.AddENode(node)};
            merges.emplace_back(old_class, new_class);
          }
        }
      }
      for (auto merge : merges) {
        eg.Merge(merge.first, merge.second);
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
      max++;
    }

    for (auto decl : tu_decl->decls()) {
      if (auto fdecl = clang::dyn_cast<clang::FunctionDecl>(decl)) {
        if (fdecl->hasBody() && fdecl->isThisDeclarationADefinition()) {
          auto node{bodies[fdecl]};
          auto stmt{eg.Schedule(node, *ast_unit, provenance)};
          fdecl->setBody(stmt);
        }
      }
    }

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