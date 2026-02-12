/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include "rellic/Compiler.h"

#include <clang/AST/ASTContext.h>
#include <clang/Frontend/ASTUnit.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/IR/Module.h>

#include "Util.h"

TEST_SUITE("Compiler") {
  SCENARIO("Create compilation pipeline") {
    GIVEN("A simple C function") {
      const char* code = R"(
        int add(int a, int b) {
          return a + b;
        }
      )";
      auto unit = GetASTUnit(code);

      WHEN("Compiling AST to IR with metadata") {
        rellic::CompilationOptions opts;
        opts.emit_type_metadata = true;

        auto result = rellic::Compile(std::move(unit), opts);

        THEN("Compilation succeeds") {
          CHECK(result.Succeeded());

          AND_THEN("Module is not null") {
            auto value = result.TakeValue();
            CHECK(value.module != nullptr);

            AND_THEN("Module has expected metadata schema") {
              auto* schema = value.module->getNamedMetadata("rellic.pointee.schema");
              CHECK(schema != nullptr);
              if (schema) {
                CHECK(schema->getNumOperands() > 0);
              }
            }
          }
        }
      }
    }
  }

  SCENARIO("Compile without metadata") {
    GIVEN("A simple function") {
      const char* code = R"(
        void func() {
          int x = 42;
        }
      )";
      auto unit = GetASTUnit(code);

      WHEN("Compiling without metadata") {
        rellic::CompilationOptions opts;
        opts.emit_type_metadata = false;

        auto result = rellic::Compile(std::move(unit), opts);

        THEN("Compilation succeeds") {
          CHECK(result.Succeeded());

          AND_THEN("No metadata schema is present") {
            auto value = result.TakeValue();
            auto* schema = value.module->getNamedMetadata("rellic.pointee.schema");
            CHECK(schema == nullptr);
          }
        }
      }
    }
  }

  SCENARIO("Compile with null AST") {
    GIVEN("A null AST unit") {
      std::unique_ptr<clang::ASTUnit> null_unit = nullptr;

      WHEN("Attempting to compile") {
        auto result = rellic::Compile(std::move(null_unit));

        THEN("Compilation fails with error") {
          CHECK(!result.Succeeded());

          AND_THEN("Error message is meaningful") {
            auto error = result.TakeError();
            CHECK(error.message.find("null") != std::string::npos);
          }
        }
      }
    }
  }
}
