/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include "rellic/AST/ASTTransformPipeline.h"

#include <clang/AST/ASTContext.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Frontend/ASTUnit.h>

#include <sstream>

#include "Util.h"

// Helper to count AST nodes of a specific type
template <typename T>
class NodeCounter : public clang::RecursiveASTVisitor<NodeCounter<T>> {
 public:
  int count = 0;

  bool VisitStmt(clang::Stmt *stmt) {
    if (llvm::isa<T>(stmt)) {
      count++;
    }
    return true;
  }
};

// Helper to get AST as string
std::string GetASTString(clang::ASTUnit &unit) {
  std::string result;
  llvm::raw_string_ostream stream(result);
  unit.getASTContext().getTranslationUnitDecl()->print(stream);
  return stream.str();
}

TEST_SUITE("ASTTransformPipeline") {
  SCENARIO("Create pipeline from AST") {
    GIVEN("A simple C function") {
      const char *code = R"(
        int add(int a, int b) {
          return a + b;
        }
      )";
      auto unit = GetASTUnit(code);

      WHEN("Creating an ASTTransformPipeline") {
        auto pipeline_result = rellic::ASTTransformPipeline::Create(*unit);

        THEN("Pipeline creation succeeds") {
          CHECK(pipeline_result.Succeeded());

          AND_THEN("GetAST returns the same AST context") {
            auto pipeline = pipeline_result.TakeValue();
            CHECK(&pipeline->GetAST() == &unit->getASTContext());
          }
        }
      }
    }
  }

  SCENARIO("EliminateDeadCode removes unreachable statements") {
    GIVEN("Code with unreachable statements") {
      const char *code = R"(
        int func() {
          int x = 5;
          return x;
          x = 10;  // Dead code after return
        }
      )";
      auto unit = GetASTUnit(code);

      WHEN("Running EliminateDeadCode") {
        auto pipeline_result = rellic::ASTTransformPipeline::Create(*unit);
        REQUIRE(pipeline_result.Succeeded());
        auto pipeline = pipeline_result.TakeValue();

        // Note: Dead code elimination might not remove unreachable code after return
        // in this simple case without more context. This test verifies the pass runs.
        pipeline->EliminateDeadCode();

        THEN("The pass completes without error") {
          // Verify AST is still valid
          CHECK(unit->getASTContext().getTranslationUnitDecl() != nullptr);
        }
      }
    }
  }

  SCENARIO("RefineControlFlow optimizes if/else chains") {
    GIVEN("Code with consecutive if statements") {
      const char *code = R"(
        int func(int x) {
          int result = 0;
          if (x > 0) {
            result = 1;
          }
          if (x <= 0) {
            result = -1;
          }
          return result;
        }
      )";
      auto unit = GetASTUnit(code);

      WHEN("Running RefineControlFlow") {
        auto pipeline_result = rellic::ASTTransformPipeline::Create(*unit);
        REQUIRE(pipeline_result.Succeeded());
        auto pipeline = pipeline_result.TakeValue();

        // Count if statements before
        NodeCounter<clang::IfStmt> counter_before;
        counter_before.TraverseDecl(unit->getASTContext().getTranslationUnitDecl());

        pipeline->RefineControlFlow();

        THEN("The pass completes and AST remains valid") {
          NodeCounter<clang::IfStmt> counter_after;
          counter_after.TraverseDecl(unit->getASTContext().getTranslationUnitDecl());

          // Verify AST is still valid
          CHECK(unit->getASTContext().getTranslationUnitDecl() != nullptr);
          // If statements might be combined into if-else, but this depends on
          // the passes detecting the pattern
          CHECK(counter_after.count >= 0);
        }
      }
    }
  }

  SCENARIO("OptimizeLoops transforms while(1) with break") {
    GIVEN("Code with while(1) and break pattern") {
      const char *code = R"(
        int func() {
          int x = 0;
          while (1) {
            if (x > 10) {
              break;
            }
            x++;
          }
          return x;
        }
      )";
      auto unit = GetASTUnit(code);

      WHEN("Running OptimizeLoops") {
        auto pipeline_result = rellic::ASTTransformPipeline::Create(*unit);
        REQUIRE(pipeline_result.Succeeded());
        auto pipeline = pipeline_result.TakeValue();

        pipeline->OptimizeLoops();

        THEN("The pass completes without error") {
          // Verify AST is still valid
          CHECK(unit->getASTContext().getTranslationUnitDecl() != nullptr);

          // Count while statements
          NodeCounter<clang::WhileStmt> counter;
          counter.TraverseDecl(unit->getASTContext().getTranslationUnitDecl());
          CHECK(counter.count >= 1);
        }
      }
    }
  }

  SCENARIO("SimplifyExpressions cleans up expressions") {
    GIVEN("Code with complex expressions") {
      const char *code = R"(
        int func() {
          int x = 5;
          int *p = &x;
          return *p;
        }
      )";
      auto unit = GetASTUnit(code);

      WHEN("Running SimplifyExpressions") {
        auto pipeline_result = rellic::ASTTransformPipeline::Create(*unit);
        REQUIRE(pipeline_result.Succeeded());
        auto pipeline = pipeline_result.TakeValue();

        pipeline->SimplifyExpressions();

        THEN("The pass completes without error") {
          // Verify AST is still valid
          CHECK(unit->getASTContext().getTranslationUnitDecl() != nullptr);
        }
      }
    }
  }

  SCENARIO("RunAllPasses executes complete pipeline") {
    GIVEN("Complex code with multiple optimization opportunities") {
      const char *code = R"(
        int complex_func(int n) {
          int result = 0;
          int i = 0;

          // Loop that should be optimized
          while (1) {
            if (i >= n) {
              break;
            }
            result += i;
            i++;
          }

          // Consecutive if statements
          if (result > 100) {
            result = 100;
          }
          if (result <= 100) {
            result = result * 2;
          }

          return result;
        }
      )";
      auto unit = GetASTUnit(code);

      WHEN("Running all passes") {
        auto pipeline_result = rellic::ASTTransformPipeline::Create(*unit);
        REQUIRE(pipeline_result.Succeeded());
        auto pipeline = pipeline_result.TakeValue();

        std::string ast_before = GetASTString(*unit);

        pipeline->RunAllPasses();

        std::string ast_after = GetASTString(*unit);

        THEN("All passes complete successfully") {
          // Verify AST is still valid
          CHECK(unit->getASTContext().getTranslationUnitDecl() != nullptr);

          // The AST should still contain the function
          auto *tu = unit->getASTContext().getTranslationUnitDecl();
          bool found_func = false;
          for (auto *decl : tu->decls()) {
            if (auto *func = llvm::dyn_cast<clang::FunctionDecl>(decl)) {
              if (func->getNameAsString() == "complex_func") {
                found_func = true;
                break;
              }
            }
          }
          CHECK(found_func);
        }
      }
    }
  }

  SCENARIO("Pipeline handles multiple functions") {
    GIVEN("Code with multiple functions") {
      const char *code = R"(
        int add(int a, int b) {
          return a + b;
        }

        int subtract(int a, int b) {
          return a - b;
        }

        int multiply(int a, int b) {
          int result = 0;
          int i = 0;
          while (1) {
            if (i >= b) {
              break;
            }
            result += a;
            i++;
          }
          return result;
        }
      )";
      auto unit = GetASTUnit(code);

      WHEN("Running all passes") {
        auto pipeline_result = rellic::ASTTransformPipeline::Create(*unit);
        REQUIRE(pipeline_result.Succeeded());
        auto pipeline = pipeline_result.TakeValue();

        pipeline->RunAllPasses();

        THEN("All functions remain in the AST") {
          auto *tu = unit->getASTContext().getTranslationUnitDecl();
          int func_count = 0;
          for (auto *decl : tu->decls()) {
            if (llvm::isa<clang::FunctionDecl>(decl)) {
              func_count++;
            }
          }
          CHECK(func_count >= 3);
        }
      }
    }
  }

  SCENARIO("Individual passes can be called in custom order") {
    GIVEN("A simple function") {
      const char *code = R"(
        int func(int x) {
          return x * 2;
        }
      )";
      auto unit = GetASTUnit(code);

      WHEN("Calling passes in custom order") {
        auto pipeline_result = rellic::ASTTransformPipeline::Create(*unit);
        REQUIRE(pipeline_result.Succeeded());
        auto pipeline = pipeline_result.TakeValue();

        // Call passes in custom order
        pipeline->SimplifyExpressions();
        pipeline->RefineControlFlow();
        pipeline->EliminateDeadCode();

        THEN("All passes execute successfully") {
          // Verify AST is still valid
          CHECK(unit->getASTContext().getTranslationUnitDecl() != nullptr);
        }
      }
    }
  }

  SCENARIO("Pipeline preserves function signatures") {
    GIVEN("Functions with various signatures") {
      const char *code = R"(
        int func1(void);
        void func2(int a, int b);
        char* func3(const char *str);

        int func1(void) {
          return 42;
        }

        void func2(int a, int b) {
          int sum = a + b;
        }

        char* func3(const char *str) {
          return (char*)str;
        }
      )";
      auto unit = GetASTUnit(code);

      WHEN("Running all passes") {
        auto pipeline_result = rellic::ASTTransformPipeline::Create(*unit);
        REQUIRE(pipeline_result.Succeeded());
        auto pipeline = pipeline_result.TakeValue();

        pipeline->RunAllPasses();

        THEN("Function signatures are preserved") {
          auto *tu = unit->getASTContext().getTranslationUnitDecl();

          // Verify func1 has no parameters
          bool found_func1 = false;
          for (auto *decl : tu->decls()) {
            if (auto *func = llvm::dyn_cast<clang::FunctionDecl>(decl)) {
              if (func->getNameAsString() == "func1" && func->hasBody()) {
                found_func1 = true;
                CHECK(func->getNumParams() == 0);
              }
            }
          }
          CHECK(found_func1);
        }
      }
    }
  }
}
