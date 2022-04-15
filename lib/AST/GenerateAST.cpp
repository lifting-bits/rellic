/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include "rellic/AST/GenerateAST.h"

#include <clang/AST/Expr.h>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <llvm/ADT/DepthFirstIterator.h>
#include <llvm/ADT/PostOrderIterator.h>
#include <llvm/Analysis/CFG.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/Analysis/RegionInfo.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/PassManager.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/raw_ostream.h>

#include <algorithm>
#include <vector>

#include "rellic/AST/ASTBuilder.h"
#include "rellic/BC/Util.h"
#include "rellic/Exception.h"

namespace rellic {

namespace {

using BBEdge = std::pair<llvm::BasicBlock *, llvm::BasicBlock *>;
using StmtVec = std::vector<clang::Stmt *>;
// using BBGraph =
//     std::unordered_map<llvm::BasicBlock *, std::vector<llvm::BasicBlock *>>;

// static void CFGSlice(llvm::BasicBlock *source, llvm::BasicBlock *sink,
//                      BBGraph &result) {
//   // Clear the output container
//   result.clear();
//   // Adds a path to the result slice BBGraph
//   auto AddPath = [&result](std::vector<llvm::BasicBlock *> &path) {
//     for (unsigned i = 1; i < path.size(); ++i) {
//       result[path[i - 1]].push_back(path[i]);
//       result[path[i]];
//     }
//   };
//   // DFS walk the CFG from `source` to `sink`
//   for (auto it = llvm::df_begin(source); it != llvm::df_end(source); ++it) {
//     for (auto succ : llvm::successors(*it)) {
//       // Construct the path up to this node while
//       // checking if `succ` is already on the path
//       std::vector<llvm::BasicBlock *> path;
//       bool on_path = false;
//       for (unsigned i = 0; i < it.getPathLength(); ++i) {
//         auto node = it.getPath(i);
//         on_path = node == succ;
//         path.push_back(node);
//       }
//       // Check if the path leads to `sink`
//       path.push_back(succ);
//       if (!it.nodeVisited(succ)) {
//         if (succ == sink) {
//           AddPath(path);
//         }
//       } else if (result.count(succ) && !on_path) {
//         AddPath(path);
//       }
//     }
//   }
// }

static bool IsRegionBlock(llvm::Region *region, llvm::BasicBlock *block) {
  return region->getRegionInfo()->getRegionFor(block) == region;
}

static bool IsSubregionEntry(llvm::Region *region, llvm::BasicBlock *block) {
  for (auto &subregion : *region) {
    if (subregion->getEntry() == block) {
      return true;
    }
  }
  return false;
}

// static bool IsSubregionExit(llvm::Region *region, llvm::BasicBlock *block) {
//   for (auto &subregion : *region) {
//     if (subregion->getExit() == block) {
//       return true;
//     }
//   }
//   return false;
// }

static llvm::Region *GetSubregion(llvm::Region *region,
                                  llvm::BasicBlock *block) {
  if (!region->contains(block)) {
    return nullptr;
  } else {
    return region->getSubRegionNode(block);
  }
}

std::string GetRegionNameStr(llvm::Region *region) {
  std::string exit_name;
  std::string entry_name;

  if (region->getEntry()->getName().empty()) {
    llvm::raw_string_ostream os(entry_name);
    region->getEntry()->printAsOperand(os, false);
  } else
    entry_name = region->getEntry()->getName().str();

  if (region->getExit()) {
    if (region->getExit()->getName().empty()) {
      llvm::raw_string_ostream os(exit_name);
      region->getExit()->printAsOperand(os, false);
    } else
      exit_name = region->getExit()->getName().str();
  } else
    exit_name = "<Function Return>";

  return entry_name + " => " + exit_name;
}

}  // namespace

static std::string GetName(llvm::Value *v) {
  std::string s{"h"};
  llvm::raw_string_ostream os(s);
  os.write_hex((unsigned long long)v);
  return s;
}

z3::expr GenerateAST::GetOrCreateEdgeForBranch(llvm::BranchInst *inst,
                                               bool cond) {
  if (z_br_edges.find({inst, cond}) == z_br_edges.end()) {
    if (cond) {
      auto name{GetName(inst)};
      auto edge{z_ctx->bool_const(name.c_str())};
      z_br_edges[{inst, cond}] = z_exprs.size();
      z_exprs.push_back(edge);
      z_br_edges_inv[edge.id()] = {inst, true};
    } else {
      auto edge{!(GetOrCreateEdgeForBranch(inst, true))};
      z_br_edges[{inst, cond}] = z_exprs.size();
      z_exprs.push_back(edge);
    }
  }

  return z_exprs[z_br_edges[{inst, cond}]];
}

z3::expr GenerateAST::GetOrCreateEdgeForSwitch(llvm::SwitchInst *inst,
                                               llvm::ConstantInt *c) {
  if (z_sw_edges.find({inst, c}) == z_sw_edges.end()) {
    if (c) {
      auto name{GetName(inst) +
                GetName(inst->findCaseValue(c)->getCaseSuccessor())};
      auto edge{z_ctx->bool_const(name.c_str())};
      z_sw_edges_inv[edge.id()] = {inst, c};

      z_sw_edges[{inst, c}] = z_exprs.size();
      z_exprs.push_back(edge);
    } else {
      // Default case
      auto edge{z_ctx->bool_val(true)};
      for (auto sw_case : inst->cases()) {
        edge = edge && !GetOrCreateEdgeForSwitch(inst, sw_case.getCaseValue());
      }
      edge = edge.simplify();
      z_sw_edges[{inst, c}] = z_exprs.size();
      z_exprs.push_back(edge);
    }
  }

  return z_exprs[z_sw_edges[{inst, c}]];
}

z3::expr GenerateAST::GetOrCreateEdgeCond(llvm::BasicBlock *from,
                                          llvm::BasicBlock *to) {
  if (z_edges.find({from, to}) == z_edges.end()) {
    // Construct the edge condition for CFG edge `(from, to)`
    auto result{z_ctx->bool_val(true)};
    auto term = from->getTerminator();
    switch (term->getOpcode()) {
      // Conditional branches
      case llvm::Instruction::Br: {
        auto br = llvm::cast<llvm::BranchInst>(term);
        if (br->isConditional()) {
          result = GetOrCreateEdgeForBranch(br, to == br->getSuccessor(0));
        }
      } break;
      // Switches
      case llvm::Instruction::Switch: {
        auto sw{llvm::cast<llvm::SwitchInst>(term)};
        if (to == sw->getDefaultDest()) {
          result = GetOrCreateEdgeForSwitch(sw, nullptr);
        } else {
          result = z_ctx->bool_val(false);
          for (auto sw_case : sw->cases()) {
            if (sw_case.getCaseSuccessor() == to) {
              result = result ||
                       GetOrCreateEdgeForSwitch(sw, sw_case.getCaseValue());
            }
          }
        }
      } break;
      // Returns
      case llvm::Instruction::Ret:
        break;
      // Exceptions
      case llvm::Instruction::Invoke:
      case llvm::Instruction::Resume:
      case llvm::Instruction::CatchSwitch:
      case llvm::Instruction::CatchRet:
      case llvm::Instruction::CleanupRet:
        THROW() << "Exception terminator '" << term->getOpcodeName()
                << "' is not supported yet";
        break;
      // Unknown
      default:
        THROW() << "Unsupported terminator instruction: "
                << term->getOpcodeName();
        break;
    }

    z_edges[{from, to}] = z_exprs.size();
    z_exprs.push_back(result.simplify());
  }

  return z_exprs[z_edges[{from, to}]];
}

z3::expr GenerateAST::GetOrCreateReachingCond(llvm::BasicBlock *block) {
  if (reaching_conds.find(block) == reaching_conds.end()) {
    if (block->hasNPredecessorsOrMore(1)) {
      auto cond{z_ctx->bool_val(false)};
      // Gather reaching conditions from predecessors of the block
      for (auto pred : llvm::predecessors(block)) {
        auto has_pred_cond{reaching_conds.find(pred) != reaching_conds.end()};
        auto edge_cond{GetOrCreateEdgeCond(pred, block)};
        // Construct reaching condition from `pred` to `block` as
        // `reach_cond[pred] && edge_cond(pred, block)` or one of
        // the two if the other one is missing.
        auto conj_cond{has_pred_cond
                           ? (z_exprs[reaching_conds[pred]] && edge_cond)
                           : edge_cond};
        // Append `conj_cond` to reaching conditions of other
        // predecessors via an `||`. Use `conj_cond` if there
        // is no `cond` yet.
        cond = cond || conj_cond;
      }

      reaching_conds[block] = z_exprs.size();
      z_exprs.push_back(cond.simplify());
    } else {
      auto cond{z_ctx->bool_val(true)};
      reaching_conds[block] = z_exprs.size();
      z_exprs.push_back(cond);
    }
  }

  return z_exprs[reaching_conds[block]];
}

StmtVec GenerateAST::CreateBasicBlockStmts(llvm::BasicBlock *block) {
  StmtVec result;
  ast_gen.VisitBasicBlock(*block, result);
  return result;
}

clang::Expr *GenerateAST::ConvertExpr(z3::expr expr) {
  auto hash{expr.id()};
  if (z_br_edges_inv.find(hash) != z_br_edges_inv.end()) {
    auto edge{z_br_edges_inv[hash]};
    CHECK(edge.second) << "Inverse map should only be populated for branches "
                          "taken when condition is true";
    return ast_gen.CreateOperandExpr(*(edge.first->op_end() - 3));
  }

  if (z_sw_edges_inv.find(hash) != z_sw_edges_inv.end()) {
    auto edge{z_sw_edges_inv[hash]};
    CHECK(edge.second)
        << "Inverse map should only be populated for not-default switch cases";

    auto opnd{ast_gen.CreateOperandExpr(edge.first->getOperandUse(0))};
    return ast.CreateEQ(opnd, ast_gen.CreateConstantExpr(edge.second));
  }

  std::vector<clang::Expr *> args;
  for (auto i{0U}; i < expr.num_args(); ++i) {
    args.push_back(ConvertExpr(expr.arg(i)));
  }

  switch (expr.decl().decl_kind()) {
    case Z3_OP_TRUE:
      CHECK_EQ(args.size(), 0) << "True cannot have arguments";
      return ast.CreateTrue();
    case Z3_OP_FALSE:
      CHECK_EQ(args.size(), 0) << "False cannot have arguments";
      return ast.CreateFalse();
    case Z3_OP_AND: {
      CHECK_GE(args.size(), 2) << "And must have at least 2 arguments";
      clang::Expr *res{args[0]};
      for (auto i{1U}; i < args.size(); ++i) {
        res = ast.CreateLAnd(res, args[i]);
      }
      return res;
    }
    case Z3_OP_OR: {
      CHECK_GE(args.size(), 2) << "Or must have at least 2 arguments";
      clang::Expr *res{args[0]};
      for (auto i{1U}; i < args.size(); ++i) {
        res = ast.CreateLOr(res, args[i]);
      }
      return res;
    }
    case Z3_OP_NOT:
      CHECK_EQ(args.size(), 1) << "Not must have one argument";
      return ast.CreateLNot(args[0]);
    default:
      LOG(FATAL) << "Invalid z3 op";
  }
  return nullptr;
}

StmtVec GenerateAST::CreateRegionStmts(llvm::Region *region) {
  StmtVec result;
  for (auto block : rpo_walk) {
    // Check if the block is a subregion entry
    auto subregion = GetSubregion(region, block);
    // Ignore blocks that are neither a subregion or a region block
    if (!subregion && !IsRegionBlock(region, block)) {
      continue;
    }
    // If the block is a head of a subregion, get the compound statement of
    // the subregion otherwise create a new compound and gate it behind a
    // reaching condition.
    clang::CompoundStmt *compound = nullptr;
    StmtVec epi_body;
    if (subregion) {
      CHECK(compound = region_stmts[subregion]);
    } else {
      // Create a compound, wrapping the block
      auto block_body = CreateBasicBlockStmts(block);
      compound = ast.CreateCompoundStmt(block_body);
      ast_gen.PopulateEpilogue(*block, epi_body);
    }
    // Gate the compound behind a reaching condition
    auto z_expr{GetOrCreateReachingCond(block)};
    block_stmts[block] = ast.CreateIf(ConvertExpr(z_expr), compound);
    block_epilogue[block] =
        ast.CreateIf(ConvertExpr(z_expr), ast.CreateCompoundStmt(epi_body));
    // Store the compound
    result.push_back(block_stmts[block]);
    result.push_back(block_epilogue[block]);
  }
  return result;
}

void GenerateAST::RefineLoopSuccessors(llvm::Loop *loop, BBSet &members,
                                       BBSet &successors) {
  // Initialize loop members
  members.insert(loop->block_begin(), loop->block_end());
  // Initialize loop successors
  llvm::SmallVector<llvm::BasicBlock *, 1> exits;
  loop->getExitBlocks(exits);
  successors.insert(exits.begin(), exits.end());
  auto header = loop->getHeader();
  auto region = regions->getRegionFor(header);
  auto exit = region->getExit();
  // Loop membership test
  auto IsLoopMember = [&members](llvm::BasicBlock *block) {
    return members.count(block) > 0;
  };
  // Refinement
  auto new_blocks = successors;
  while (successors.size() > 1 && !new_blocks.empty()) {
    new_blocks.clear();
    for (auto block : BBSet(successors)) {
      if (block == exit) {
        // Don't remove this block from the list of successors if it is the
        // direct exit of the region
        continue;
      }

      // Check if all predecessors of `block` are loop members
      if (std::all_of(llvm::pred_begin(block), llvm::pred_end(block),
                      IsLoopMember)) {
        // Add `block` as a loop member
        members.insert(block);
        // Remove it as a loop successor
        successors.erase(block);
        // Add a successor of `block` to the set of discovered blocks if
        // if it is a region member, if it is NOT a loop member and if
        // the loop header dominates it.
        for (auto succ : llvm::successors(block)) {
          if (IsRegionBlock(region, succ) && !IsLoopMember(succ) &&
              domtree->dominates(header, succ)) {
            new_blocks.insert(succ);
          }
        }
      }
    }
    successors.insert(new_blocks.begin(), new_blocks.end());
  }
}

clang::CompoundStmt *GenerateAST::StructureAcyclicRegion(llvm::Region *region) {
  DLOG(INFO) << "Region " << GetRegionNameStr(region) << " is acyclic";
  auto region_body = CreateRegionStmts(region);
  return ast.CreateCompoundStmt(region_body);
}

clang::CompoundStmt *GenerateAST::StructureCyclicRegion(llvm::Region *region) {
  DLOG(INFO) << "Region " << GetRegionNameStr(region) << " is cyclic";
  auto region_body = CreateRegionStmts(region);
  // Get the loop for which the entry block of the region is a header
  // loops->getLoopFor(region->getEntry())->print(llvm::errs());
  auto loop = region->outermostLoopInRegion(loops, region->getEntry());
  // Only add loop specific control-flow to regions which contain
  // a recognized natural loop. Cyclic regions may only be fragments
  // of a larger loop structure.
  if (!loop) {
    return ast.CreateCompoundStmt(region_body);
  }
  // Refine loop members and successors without invalidating LoopInfo
  BBSet members, successors;
  RefineLoopSuccessors(loop, members, successors);
  // Construct the initial loop body
  StmtVec loop_body;
  for (auto block : rpo_walk) {
    if (members.count(block)) {
      if (IsRegionBlock(region, block) || IsSubregionEntry(region, block)) {
        auto stmt = block_stmts[block];
        auto it = std::find(region_body.begin(), region_body.end(), stmt);
        region_body.erase(it);
        loop_body.push_back(stmt);

        auto epilogue = block_epilogue[block];
        it = std::find(region_body.begin(), region_body.end(), epilogue);
        region_body.erase(it);
        loop_body.push_back(epilogue);
      }
    }
  }
  // Get loop exit edges
  std::vector<BBEdge> exits;
  for (auto succ : successors) {
    for (auto pred : llvm::predecessors(succ)) {
      if (members.count(pred)) {
        exits.push_back({pred, succ});
      }
    }
  }
  // Insert `break` statements
  for (auto edge : exits) {
    auto from = edge.first;
    auto to = edge.second;
    // Create edge condition
    auto z_expr{GetOrCreateReachingCond(from) && GetOrCreateEdgeCond(from, to)};
    auto cond{ConvertExpr(z_expr.simplify())};
    // Find the statement corresponding to the exiting block
    auto it = std::find(loop_body.begin(), loop_body.end(), block_stmts[from]);
    CHECK(it != loop_body.end());
    // Create a loop exiting `break` statement
    StmtVec break_stmt;
    for (auto block : rpo_walk) {
      // Check if the block is a subregion entry
      auto subregion = GetSubregion(region, block);
      // Ignore blocks that are neither a subregion or a region block
      if (!subregion && !IsRegionBlock(region, block)) {
        continue;
      }

      if (!subregion) {
        ast_gen.PopulateEpilogue(*block, break_stmt);
      }
    }
    break_stmt.push_back(ast.CreateBreak());
    auto exit_stmt = ast.CreateIf(cond, ast.CreateCompoundStmt(break_stmt));
    // Insert it after the exiting block statement
    loop_body.insert(std::next(it), exit_stmt);
  }
  // Create the loop statement
  auto loop_stmt =
      ast.CreateWhile(ast.CreateTrue(), ast.CreateCompoundStmt(loop_body));
  // Insert it at the beginning of the region body
  region_body.insert(region_body.begin(), loop_stmt);
  // Structure the rest of the loop body as a acyclic region
  return ast.CreateCompoundStmt(region_body);
}

clang::CompoundStmt *GenerateAST::StructureSwitchRegion(llvm::Region *region) {
  DLOG(INFO) << "Region " << GetRegionNameStr(region)
             << " has a switch instruction";
  // TODO(frabert): find a way to do this in a refinement pass.
  // See "No More Gotos": Condition-aware refinement
  auto body{CreateBasicBlockStmts(region->getEntry())};
  ast_gen.PopulateEpilogue(*region->getEntry(), body);
  auto sw_inst{
      llvm::cast<llvm::SwitchInst>(region->getEntry()->getTerminator())};
  auto cond{ast_gen.CreateOperandExpr(sw_inst->getOperandUse(0))};
  auto sw_stmt{ast.CreateSwitchStmt(cond)};
  StmtVec sw_body;

  auto default_dest{sw_inst->getDefaultDest()};
  if (default_dest != region->getExit()) {
    auto default_body{CreateBasicBlockStmts(default_dest)};
    ast_gen.PopulateEpilogue(*default_dest, default_body);
    sw_body.push_back(
        ast.CreateDefaultStmt(ast.CreateCompoundStmt(default_body)));
    sw_body.push_back(ast.CreateBreak());
  }

  std::vector<llvm::SwitchInst::CaseHandle> cases{sw_inst->case_begin(),
                                                  sw_inst->case_end()};
  for (auto i{0U}; i < cases.size(); ++i) {
    auto sw_case{cases[i]};
    auto successor{sw_case.getCaseSuccessor()};
    if (i + 1 < cases.size() && cases[i + 1].getCaseSuccessor() == successor) {
      continue;
    }

    auto value{ast_gen.CreateConstantExpr(sw_case.getCaseValue())};
    auto case_stmt{ast.CreateCaseStmt(value)};
    if (successor != region->getExit()) {
      auto case_body{CreateBasicBlockStmts(successor)};
      ast_gen.PopulateEpilogue(*successor, case_body);
      case_stmt->setSubStmt(ast.CreateCompoundStmt(case_body));
      sw_body.push_back(case_stmt);
      sw_body.push_back(ast.CreateBreak());
    } else {
      case_stmt->setSubStmt(ast.CreateBreak());
      sw_body.push_back(case_stmt);
    }
  }
  sw_stmt->setBody(ast.CreateCompoundStmt(sw_body));
  body.push_back(sw_stmt);
  return ast.CreateCompoundStmt(body);
}

clang::CompoundStmt *GenerateAST::StructureRegion(llvm::Region *region) {
  DLOG(INFO) << "Structuring region " << GetRegionNameStr(region);
  auto &region_stmt = region_stmts[region];
  if (region_stmt) {
    LOG(WARNING) << "Asking to re-structure region: "
                 << GetRegionNameStr(region)
                 << "; returning current region instead";
    return region_stmt;
  }
  bool is_cyclic{loops->isLoopHeader(region->getEntry())};
  if (llvm::isa<llvm::SwitchInst>(region->getEntry()->getTerminator()) &&
      !GetSubregion(region, region->getEntry()) && !is_cyclic) {
    region_stmt = StructureSwitchRegion(region);
    return region_stmt;
  }

  // Compute reaching conditions
  for (auto block : rpo_walk) {
    if (IsRegionBlock(region, block)) {
      GetOrCreateReachingCond(block);
    }
  }
  // Structure
  region_stmt = is_cyclic ? StructureCyclicRegion(region)
                          : StructureAcyclicRegion(region);
  return region_stmt;
}

llvm::AnalysisKey GenerateAST::Key;

GenerateAST::GenerateAST(Provenance &provenance, clang::ASTUnit &unit)
    : ast_ctx(&unit.getASTContext()),
      unit(unit),
      provenance(provenance),
      ast_gen(unit, provenance),
      ast(unit),
      z_ctx(new z3::context()),
      z_exprs(*z_ctx) {}

GenerateAST::Result GenerateAST::run(llvm::Module &module,
                                     llvm::ModuleAnalysisManager &MAM) {
  for (auto &func : module.functions()) {
    ast_gen.VisitFunctionDecl(func);
  }

  for (auto &var : module.globals()) {
    ast_gen.VisitGlobalVar(var);
  }

  return llvm::PreservedAnalyses::all();
}

GenerateAST::Result GenerateAST::run(llvm::Function &func,
                                     llvm::FunctionAnalysisManager &FAM) {
  if (func.isDeclaration()) {
    return llvm::PreservedAnalyses::all();
  }

  // Clear the region statements from previous functions
  region_stmts.clear();
  // Get dominator tree
  domtree = &FAM.getResult<llvm::DominatorTreeAnalysis>(func);
  // Get single-entry, single-exit regions
  regions = &FAM.getResult<llvm::RegionInfoAnalysis>(func);
  // Get loops
  loops = &FAM.getResult<llvm::LoopAnalysis>(func);
  // Get a reverse post-order walk for iterating over region blocks in
  // structurization
  llvm::ReversePostOrderTraversal<llvm::Function *> rpo(&func);
  rpo_walk.assign(rpo.begin(), rpo.end());
  GetOrCreateReachingCond(rpo_walk[0]);
  // Recursively walk regions in post-order and structure
  std::function<void(llvm::Region *)> POWalkSubRegions;
  POWalkSubRegions = [&](llvm::Region *region) {
    for (auto &subregion : *region) {
      POWalkSubRegions(&*subregion);
    }
    StructureRegion(region);
  };
  // Call the above declared bad boy
  POWalkSubRegions(regions->getTopLevelRegion());
  // Get the function declaration AST node for `func`
  auto fdecl = clang::cast<clang::FunctionDecl>(provenance.value_decls[&func]);
  // Create a redeclaration of `fdecl` that will serve as a definition
  auto tudecl = ast_ctx->getTranslationUnitDecl();
  auto fdefn =
      ast.CreateFunctionDecl(tudecl, fdecl->getType(), fdecl->getIdentifier());
  fdefn->setPreviousDecl(fdecl);
  provenance.value_decls[&func] = fdefn;
  tudecl->addDecl(fdefn);
  // Set parameters to the same as the previous declaration
  fdefn->setParams(fdecl->parameters());
  // Create body of the function
  StmtVec fbody;
  // Add declarations of local variables
  for (auto decl : fdecl->decls()) {
    if (clang::isa<clang::VarDecl>(decl)) {
      fbody.push_back(ast.CreateDeclStmt(decl));
    }
  }
  // Add statements of the top-level region compound
  for (auto stmt : region_stmts[regions->getTopLevelRegion()]->body()) {
    fbody.push_back(stmt);
  }
  // Set body to a new compound
  fdefn->setBody(ast.CreateCompoundStmt(fbody));

  return llvm::PreservedAnalyses::all();
}

void GenerateAST::run(llvm::Module &module, Provenance &provenance,
                      clang::ASTUnit &unit) {
  llvm::ModulePassManager mpm;
  llvm::ModuleAnalysisManager mam;
  llvm::PassBuilder pb;
  mam.registerPass([&] { return rellic::GenerateAST(provenance, unit); });
  mpm.addPass(rellic::GenerateAST(provenance, unit));
  pb.registerModuleAnalyses(mam);
  mpm.run(module, mam);

  llvm::FunctionPassManager fpm;
  llvm::FunctionAnalysisManager fam;
  fam.registerPass([&] { return rellic::GenerateAST(provenance, unit); });
  fpm.addPass(rellic::GenerateAST(provenance, unit));
  pb.registerFunctionAnalyses(fam);
  for (auto &func : module.functions()) {
    fpm.run(func, fam);
  }
}

}  // namespace rellic
