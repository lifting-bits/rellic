/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include "rellic/Compiler.h"

#include <clang/AST/ASTContext.h>
#include <clang/Basic/TargetInfo.h>
#include <clang/Frontend/ASTUnit.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Metadata.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/raw_ostream.h>

#include <glog/logging.h>

namespace rellic {

namespace {

// Helper class to attach type metadata to IR after CodeGen
class MetadataAttacher {
 public:
  MetadataAttacher(llvm::Module& module, clang::ASTContext& ast_ctx)
      : module_(module), ast_ctx_(ast_ctx), ctx_(module.getContext()) {
    metadata_kind_id_ = ctx_.getMDKindID("rellic.pointee.type");
  }

  void AttachMetadata() {
    DLOG(INFO) << "Attaching type preservation metadata";

    // Create schema version
    CreateSchemaMetadata();

    // Walk all functions and attach metadata to pointer instructions
    for (auto& func : module_) {
      if (!func.isDeclaration()) {
        ProcessFunction(func);
      }
    }

    DLOG(INFO) << "Metadata attachment complete";
  }

 private:
  void CreateSchemaMetadata() {
    llvm::SmallVector<llvm::Metadata*, 2> vals;
    vals.push_back(llvm::ConstantAsMetadata::get(
        llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx_), 1)));
    vals.push_back(llvm::MDString::get(ctx_, "version"));

    auto* node = llvm::MDNode::get(ctx_, vals);
    llvm::NamedMDNode* schema =
        module_.getOrInsertNamedMetadata("rellic.pointee.schema");
    schema->addOperand(node);
  }

  void ProcessFunction(llvm::Function& func) {
    for (auto& bb : func) {
      for (auto& inst : bb) {
        if (auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(&inst)) {
          AttachToAlloca(*alloca);
        } else if (auto* gep = llvm::dyn_cast<llvm::GetElementPtrInst>(&inst)) {
          AttachToGEP(*gep);
        }
      }
    }
  }

  void AttachToAlloca(llvm::AllocaInst& inst) {
    llvm::Type* allocated_type = inst.getAllocatedType();
    if (auto* md = EncodeType(allocated_type)) {
      inst.setMetadata(metadata_kind_id_, md);
    }
  }

  void AttachToGEP(llvm::GetElementPtrInst& inst) {
    llvm::Type* source_type = inst.getSourceElementType();
    if (source_type) {
      if (auto* md = EncodeType(source_type)) {
        inst.setMetadata(metadata_kind_id_, md);
      }
    }
  }

  llvm::MDNode* EncodeType(llvm::Type* type) {
    if (!type) {
      return nullptr;
    }

    // Check cache
    auto it = type_cache_.find(type);
    if (it != type_cache_.end()) {
      return it->second;
    }

    // Get type string representation
    std::string type_str;
    llvm::raw_string_ostream stream(type_str);
    type->print(stream);
    stream.flush();

    // Get type size
    const llvm::DataLayout& DL = module_.getDataLayout();
    uint64_t size = 0;
    if (type->isSized()) {
      size = DL.getTypeAllocSize(type).getFixedValue();
    }

    // Create metadata: !{!"type_string", i64 size}
    llvm::SmallVector<llvm::Metadata*, 2> vals;
    vals.push_back(llvm::MDString::get(ctx_, type_str));
    vals.push_back(llvm::ConstantAsMetadata::get(
        llvm::ConstantInt::get(llvm::Type::getInt64Ty(ctx_), size)));

    auto* node = llvm::MDNode::get(ctx_, vals);
    type_cache_[type] = node;
    return node;
  }

  llvm::Module& module_;
  clang::ASTContext& ast_ctx_;
  llvm::LLVMContext& ctx_;
  unsigned metadata_kind_id_;
  std::unordered_map<llvm::Type*, llvm::MDNode*> type_cache_;
};

}  // anonymous namespace

Result<CompilationResult, CompilationError> Compile(
    std::unique_ptr<clang::ASTUnit> ast_unit, CompilationOptions options) {
  if (!ast_unit) {
    CompilationError error{nullptr, "AST unit is null"};
    return Result<CompilationResult, CompilationError>(std::move(error));
  }

  DLOG(INFO) << "Starting AST to IR compilation";

  // Create LLVM context for the new module
  auto llvm_ctx = std::make_unique<llvm::LLVMContext>();

  // Get AST context and target info
  auto& ast_ctx = ast_unit->getASTContext();
  const clang::TargetInfo& target_info = ast_ctx.getTargetInfo();

  // Create a simple empty module as placeholder
  // TODO: Properly implement AST to IR compilation using Clang's CodeGen API
  std::string module_name = "compiled_ast";
  auto module = std::make_unique<llvm::Module>(module_name, *llvm_ctx);

  // Set up target information
  module->setTargetTriple(target_info.getTriple().str());
  module->setDataLayout(target_info.getDataLayoutString());

  DLOG(INFO) << "Created IR module: " << module_name;

  LOG(WARNING) << "AST to IR compilation not fully implemented yet";
  LOG(WARNING) << "This is a placeholder - use for metadata attachment testing only";

  // Attach metadata if requested
  if (options.emit_type_metadata && module) {
    try {
      MetadataAttacher attacher(*module, ast_ctx);
      attacher.AttachMetadata();
    } catch (const std::exception& e) {
      LOG(WARNING) << "Failed to attach metadata: " << e.what();
    }
  }

  // Verify module is valid
  if (!module) {
    CompilationError error{std::move(ast_unit), "Failed to create LLVM module"};
    return Result<CompilationResult, CompilationError>(std::move(error));
  }

  DLOG(INFO) << "Module created successfully: " << module->getName().str();

  // Prepare result
  CompilationResult result{std::move(module), std::move(ast_unit)};
  return Result<CompilationResult, CompilationError>(std::move(result));
}

}  // namespace rellic
