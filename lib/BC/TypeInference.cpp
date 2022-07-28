/*
 * Copyright (c) 2022-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include <gflags/gflags.h>
#include <glog/logging.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/IR/Argument.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constant.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/InstVisitor.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Metadata.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Operator.h>
#include <llvm/IR/Use.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/Utils.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <llvm/Transforms/Utils/Local.h>
#include <llvm/Transforms/Utils/LowerSwitch.h>

#include <functional>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>

#include "rellic/BC/Util.h"

namespace rellic {
struct Integer;

struct Signed {
  constexpr bool operator==(const Signed&) const { return true; }
  constexpr bool operator!=(const Signed&) const { return false; }
};

struct Unsigned {
  constexpr bool operator==(const Unsigned&) const { return true; }
  constexpr bool operator!=(const Unsigned&) const { return false; }
};

struct Pointer;

using Term = std::variant<std::string, Integer, Pointer, Signed, Unsigned>;

struct Pointer {
  std::shared_ptr<Term> pointed;
  Pointer(const Term& t) : pointed(std::make_shared<Term>(t)) {}
};

struct Integer {
  unsigned size;
  std::shared_ptr<Term> kind;
  Integer(unsigned size, const Term& t)
      : size(size), kind(std::make_shared<Term>(t)) {}
};

constexpr bool operator==(const Pointer& a, const Pointer& b) {
  return a.pointed == b.pointed;
}

constexpr bool operator!=(const Pointer& a, const Pointer& b) {
  return !(a == b);
}

constexpr bool operator==(const Integer& a, const Integer& b) {
  return a.size == b.size && a.kind == b.kind;
}

constexpr bool operator!=(const Integer& a, const Integer& b) {
  return !(a == b);
}

llvm::MDNode* MakeNode(llvm::LLVMContext& ctx, const Term& t) {
  auto GetConstant = [&ctx](auto value) {
    return llvm::ConstantAsMetadata::get(llvm::ConstantInt::getIntegerValue(
        llvm::Type::getInt32Ty(ctx),
        llvm::APInt(32, static_cast<unsigned>(value))));
  };

  if (std::holds_alternative<Integer>(t)) {
    auto& integer{std::get<Integer>(t)};
    if (std::holds_alternative<Signed>(*integer.kind)) {
      return llvm::MDNode::get(
          ctx, {GetConstant(AbstractType::Signed), GetConstant(integer.size)});
    }

    if (std::holds_alternative<Unsigned>(*integer.kind)) {
      return llvm::MDNode::get(ctx, {GetConstant(AbstractType::Unsigned),
                                     GetConstant(integer.size)});
    }

    return llvm::MDNode::get(
        ctx, {GetConstant(AbstractType::Unsigned), GetConstant(integer.size)});
  }

  if (std::holds_alternative<Pointer>(t)) {
    auto& ptr{std::get<Pointer>(t)};
    auto sub{MakeNode(ctx, *ptr.pointed)};
    if (sub) {
      return llvm::MDNode::get(ctx, {GetConstant(AbstractType::Pointer), sub});
    }

    return llvm::MDNode::get(ctx, {GetConstant(AbstractType::Pointer)});
  }

  return nullptr;
}

bool ContainsVar(const Term& t, const std::string& v) {
  if (std::holds_alternative<std::string>(t)) {
    return std::get<std::string>(t) == v;
  }

  if (std::holds_alternative<Pointer>(t)) {
    auto& ptr{std::get<Pointer>(t)};
    return ContainsVar(*ptr.pointed, v);
  }

  return false;
}

bool ContainsFreeVariables(const Term& t) {
  if (std::holds_alternative<std::string>(t)) {
    return true;
  }

  if (std::holds_alternative<Pointer>(t)) {
    auto& ptr{std::get<Pointer>(t)};
    return ContainsFreeVariables(*ptr.pointed);
  }

  return false;
}

std::string ToString(const Term& t) {
  if (std::holds_alternative<std::string>(t)) {
    return std::get<std::string>(t);
  }

  if (std::holds_alternative<Pointer>(t)) {
    return "ptr(" + ToString(*std::get<Pointer>(t).pointed) + ")";
  }

  if (std::holds_alternative<Integer>(t)) {
    auto& integer{std::get<Integer>(t)};
    return "integer(" + ToString(*integer.kind) + ", " +
           std::to_string(integer.size) + ")";
  }

  if (std::holds_alternative<Signed>(t)) {
    return "signed";
  }

  if (std::holds_alternative<Unsigned>(t)) {
    return "unsigned";
  }

  CHECK(false);
  return "bottom";
}

class Equalities {
 private:
  using Eq = std::pair<Term, Term>;

  std::vector<Eq> eqs;
  std::unordered_map<std::string, Term> assignments;
  unsigned num_vars{};

  static Term Substitute(const Term& t, const std::string& v, const Term& sub) {
    if (std::holds_alternative<std::string>(t)) {
      auto var{std::get<std::string>(t)};
      if (var == v) {
        return sub;
      }
    }

    if (std::holds_alternative<Pointer>(t)) {
      auto& ptr{std::get<Pointer>(t)};
      return Pointer{Substitute(*ptr.pointed, v, sub)};
    }

    return t;
  }

  static bool TryUnify(std::vector<Eq>& eqs,
                       std::unordered_map<std::string, Term>& assignments) {
    auto remove = [](std::vector<Eq>& vec, size_t i) {
      if (vec.size() > 1) {
        std::iter_swap(vec.begin() + i, vec.end() - 1);
        vec.pop_back();
      } else {
        vec.clear();
      }
    };

    size_t i{0};
    while (i < eqs.size()) {
      auto [lhs, rhs] = eqs[i];

      if (lhs == rhs) {
        // Delete
        remove(eqs, i);
        i = 0;
        continue;
      }

      if (std::holds_alternative<Integer>(lhs) &&
          std::holds_alternative<Integer>(rhs)) {
        auto int_l{std::get<Integer>(lhs)};
        auto int_r{std::get<Integer>(rhs)};
        if (int_l.size != int_r.size) {
          return false;
        }

        // Decompose
        remove(eqs, i);
        eqs.push_back({*int_l.kind, *int_r.kind});
        i = 0;
        continue;
      }

      if (std::holds_alternative<Pointer>(lhs) &&
          std::holds_alternative<Pointer>(rhs)) {
        // Decompose
        auto ptr_l{std::get<Pointer>(lhs)};
        auto ptr_r{std::get<Pointer>(rhs)};
        remove(eqs, i);
        eqs.push_back({*ptr_l.pointed, *ptr_l.pointed});
        i = 0;
        continue;
      }

      if (std::holds_alternative<std::string>(lhs)) {
        auto var{std::get<std::string>(lhs)};
        if (ContainsVar(rhs, var)) {
          // Occurs-check
          return false;
        }

        // Eliminate
        remove(eqs, i);

        std::vector<Eq> new_eqs;
        auto new_assignments = assignments;
        for (auto [eq_l, eq_r] : eqs) {
          new_eqs.push_back(
              {Substitute(eq_l, var, rhs), Substitute(eq_r, var, rhs)});
        }

        new_assignments[var] = rhs;
        for (auto& [k, v] : new_assignments) {
          v = Substitute(v, var, rhs);
        }
        if (TryUnify(new_eqs, new_assignments)) {
          eqs = new_eqs;
          assignments = new_assignments;
        }

        i = 0;
        continue;
      }

      if (std::holds_alternative<std::string>(rhs)) {
        // Swap
        remove(eqs, i);
        eqs.push_back({rhs, lhs});
        i = 0;
        continue;
      }

      return false;

      ++i;
    }
    return true;
  }

 public:
  Term FreshVar() { return "T" + std::to_string(num_vars++); }

  const std::vector<std::pair<Term, Term>>& GetEqualities() const {
    return eqs;
  }
  const std::unordered_map<std::string, Term>& GetAssignments() const {
    return assignments;
  }

  void Add(std::pair<Term, Term> eq) { eqs.push_back(eq); }

  std::string CreateTerm(const llvm::Value* var) {
    return "H" + std::to_string((unsigned long long)var);
  }

  void Unify() { TryUnify(eqs, assignments); }
};

class EqualitiesGenerator : public llvm::InstVisitor<EqualitiesGenerator> {
 private:
  Equalities& equalities;

  Term MakeVar(llvm::Value* val) { return equalities.CreateTerm(val); }

  Term MakeVar() { return equalities.FreshVar(); }

  Term AbstractTypeFromLLVM(llvm::Type* type) {
    if (type->isIntegerTy()) {
      return Integer{type->getScalarSizeInBits(), MakeVar()};
    }

    if (type->isPointerTy()) {
      return Pointer{MakeVar()};
    }

    return MakeVar();
  }

 public:
  EqualitiesGenerator(Equalities& equalities) : equalities(equalities) {}

  void visitFPToSIInst(llvm::FPToSIInst& inst) {
    equalities.Add({MakeVar(&inst),
                    Integer{inst.getType()->getScalarSizeInBits(), Signed{}}});
  }

  void visitFPToUIInst(llvm::FPToUIInst& inst) {
    equalities.Add(
        {MakeVar(&inst),
         Integer{inst.getType()->getScalarSizeInBits(), Unsigned{}}});
  }

  void visitSIToFPInst(llvm::SIToFPInst& inst) {
    auto opnd0{inst.getOperand(0)};
    if (!llvm::isa<llvm::Constant>(opnd0)) {
      equalities.Add(
          {MakeVar(opnd0),
           Integer{opnd0->getType()->getScalarSizeInBits(), Signed{}}});
    }
  }

  void visitUIToFPInst(llvm::UIToFPInst& inst) {
    auto opnd0{inst.getOperand(0)};
    equalities.Add(
        {MakeVar(opnd0),
         Integer{opnd0->getType()->getScalarSizeInBits(), Unsigned{}}});
  }

  void visitICmpInst(llvm::ICmpInst& inst) {
    if (inst.isSigned()) {
      auto opnd0{inst.getOperand(0)};
      if (!llvm::isa<llvm::Constant>(opnd0)) {
        equalities.Add(
            {MakeVar(opnd0),
             Integer{opnd0->getType()->getScalarSizeInBits(), Signed{}}});
      }
      auto opnd1{inst.getOperand(1)};
      if (!llvm::isa<llvm::Constant>(opnd1)) {
        equalities.Add(
            {MakeVar(opnd1),
             Integer{opnd1->getType()->getScalarSizeInBits(), Signed{}}});
      }
    }
  }

  void visitSExtInst(llvm::SExtInst& inst) {
    auto opnd0{inst.getOperand(0)};
    if (!llvm::isa<llvm::Constant>(opnd0)) {
      equalities.Add(
          {MakeVar(opnd0),
           Integer{opnd0->getType()->getScalarSizeInBits(), Signed{}}});
    }
  }

  void visitBinaryOperator(llvm::BinaryOperator& inst) {
    switch (inst.getOpcode()) {
      case llvm::BinaryOperator::SRem:
      case llvm::BinaryOperator::SDiv: {
        equalities.Add(
            {MakeVar(&inst),
             Integer{inst.getType()->getScalarSizeInBits(), Signed{}}});
        auto opnd0{inst.getOperand(0)};
        if (!llvm::isa<llvm::Constant>(opnd0)) {
          equalities.Add(
              {MakeVar(opnd0),
               Integer{opnd0->getType()->getScalarSizeInBits(), Signed{}}});
        }
        auto opnd1{inst.getOperand(1)};
        if (!llvm::isa<llvm::Constant>(opnd1)) {
          equalities.Add(
              {MakeVar(opnd1),
               Integer{opnd1->getType()->getScalarSizeInBits(), Signed{}}});
        }
      } break;
      case llvm::BinaryOperator::URem:
      case llvm::BinaryOperator::UDiv: {
        equalities.Add(
            {MakeVar(&inst),
             Integer{inst.getType()->getScalarSizeInBits(), Unsigned{}}});
        auto opnd0{inst.getOperand(0)};
        if (!llvm::isa<llvm::Constant>(opnd0)) {
          equalities.Add(
              {MakeVar(opnd0),
               Integer{opnd0->getType()->getScalarSizeInBits(), MakeVar()}});
        }
        auto opnd1{inst.getOperand(1)};
        if (!llvm::isa<llvm::Constant>(opnd1)) {
          equalities.Add(
              {MakeVar(opnd1),
               Integer{opnd1->getType()->getScalarSizeInBits(), MakeVar()}});
        }
      } break;
      case llvm::BinaryOperator::Add:
      case llvm::BinaryOperator::Sub:
      case llvm::BinaryOperator::Mul: {
        auto opnd0{inst.getOperand(0)};
        if (!llvm::isa<llvm::Constant>(opnd0)) {
          equalities.Add({MakeVar(&inst), MakeVar(opnd0)});
        }

        auto opnd1{inst.getOperand(1)};
        if (!llvm::isa<llvm::Constant>(opnd1)) {
          equalities.Add({MakeVar(&inst), MakeVar(opnd1)});
        }
      }
      default:
        break;
    }
  }

  void visitCallInst(llvm::CallInst& inst) {
    equalities.Add({MakeVar(&inst), MakeVar(inst.getCalledFunction())});
  }

  void visitReturnInst(llvm::ReturnInst& inst) {
    auto retval{inst.getReturnValue()};
    if (retval && !llvm::isa<llvm::Constant>(retval)) {
      equalities.Add({MakeVar(retval), MakeVar(inst.getFunction())});
    }
  }

  void visitAllocaInst(llvm::AllocaInst& inst) {
    equalities.Add({MakeVar(&inst),
                    Pointer{AbstractTypeFromLLVM(inst.getAllocatedType())}});
  }

  void visitLoadInst(llvm::LoadInst& inst) {
    auto type{AbstractTypeFromLLVM(inst.getType())};
    equalities.Add({MakeVar(&inst), type});
    equalities.Add({MakeVar(inst.getPointerOperand()), Pointer{type}});
    equalities.Add(
        {Pointer{MakeVar(&inst)}, MakeVar(inst.getPointerOperand())});
  }

  void visitStoreInst(llvm::StoreInst& inst) {
    auto value_opnd{inst.getValueOperand()};
    if (!llvm::isa<llvm::Constant>(value_opnd)) {
      equalities.Add(
          {MakeVar(value_opnd), AbstractTypeFromLLVM(value_opnd->getType())});
      equalities.Add(
          {MakeVar(inst.getPointerOperand()), Pointer{MakeVar(value_opnd)}});
    }
  }

  void visitGetElementPtrInst(llvm::GetElementPtrInst& inst) {
    auto type{AbstractTypeFromLLVM(inst.getResultElementType())};
    equalities.Add({MakeVar(&inst), Pointer{type}});
  }
};

void PerformTypeInference(llvm::Module& module) {
  Equalities equalities;

  EqualitiesGenerator gen{equalities};
  gen.visit(module);
  equalities.Unify();

  auto assignments{equalities.GetAssignments()};

  for (auto& gvar : module.globals()) {
    gvar.setMetadata("rellic.type",
                     MakeNode(module.getContext(),
                              assignments[equalities.CreateTerm(&gvar)]));
  }

  for (auto& func : module.functions()) {
    func.setMetadata("rellic.type",
                     MakeNode(module.getContext(),
                              assignments[equalities.CreateTerm(&func)]));
    for (auto& inst : llvm::instructions(func)) {
      inst.setMetadata("rellic.type",
                       MakeNode(module.getContext(),
                                assignments[equalities.CreateTerm(&inst)]));
    }
  }
}
}  // namespace rellic