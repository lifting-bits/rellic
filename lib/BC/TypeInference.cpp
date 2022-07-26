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
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/InstVisitor.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Metadata.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Operator.h>
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
namespace {
namespace AbstractTypes {
struct Top {};
constexpr bool operator<(const Top&, const Top&) { return false; }
constexpr bool operator>(const Top&, const Top&) { return false; }
constexpr bool operator<=(const Top&, const Top&) { return true; }
constexpr bool operator>=(const Top&, const Top&) { return true; }
constexpr bool operator==(const Top&, const Top&) { return true; }
constexpr bool operator!=(const Top&, const Top&) { return false; }

struct Integer {
  unsigned size;
};
constexpr bool operator<(const Integer& a, const Integer& b) {
  return a.size < b.size;
}
constexpr bool operator>(const Integer& a, const Integer& b) {
  return a.size > b.size;
}
constexpr bool operator<=(const Integer& a, const Integer& b) {
  return a.size <= b.size;
}
constexpr bool operator>=(const Integer& a, const Integer& b) {
  return a.size >= b.size;
}
constexpr bool operator==(const Integer& a, const Integer& b) {
  return a.size == b.size;
}
constexpr bool operator!=(const Integer& a, const Integer& b) {
  return a.size != b.size;
}

struct Signed {
  unsigned size;
};
constexpr bool operator<(const Signed& a, const Signed& b) {
  return a.size < b.size;
}
constexpr bool operator>(const Signed& a, const Signed& b) {
  return a.size > b.size;
}
constexpr bool operator<=(const Signed& a, const Signed& b) {
  return a.size <= b.size;
}
constexpr bool operator>=(const Signed& a, const Signed& b) {
  return a.size >= b.size;
}
constexpr bool operator==(const Signed& a, const Signed& b) {
  return a.size == b.size;
}
constexpr bool operator!=(const Signed& a, const Signed& b) {
  return a.size != b.size;
}

struct Unsigned {
  unsigned size;
};
constexpr bool operator<(const Unsigned& a, const Unsigned& b) {
  return a.size < b.size;
}
constexpr bool operator>(const Unsigned& a, const Unsigned& b) {
  return a.size > b.size;
}
constexpr bool operator<=(const Unsigned& a, const Unsigned& b) {
  return a.size <= b.size;
}
constexpr bool operator>=(const Unsigned& a, const Unsigned& b) {
  return a.size >= b.size;
}
constexpr bool operator==(const Unsigned& a, const Unsigned& b) {
  return a.size == b.size;
}
constexpr bool operator!=(const Unsigned& a, const Unsigned& b) {
  return a.size != b.size;
}

struct BiSigned {
  unsigned size;
};
constexpr bool operator<(const BiSigned& a, const BiSigned& b) {
  return a.size < b.size;
}
constexpr bool operator>(const BiSigned& a, const BiSigned& b) {
  return a.size > b.size;
}
constexpr bool operator<=(const BiSigned& a, const BiSigned& b) {
  return a.size <= b.size;
}
constexpr bool operator>=(const BiSigned& a, const BiSigned& b) {
  return a.size >= b.size;
}
constexpr bool operator==(const BiSigned& a, const BiSigned& b) {
  return a.size == b.size;
}
constexpr bool operator!=(const BiSigned& a, const BiSigned& b) {
  return a.size != b.size;
}

struct Bottom {};
constexpr bool operator<(const Bottom&, const Bottom&) { return false; }
constexpr bool operator>(const Bottom&, const Bottom&) { return false; }
constexpr bool operator<=(const Bottom&, const Bottom&) { return true; }
constexpr bool operator>=(const Bottom&, const Bottom&) { return true; }
constexpr bool operator==(const Bottom&, const Bottom&) { return true; }
constexpr bool operator!=(const Bottom&, const Bottom&) { return false; }

template <typename Var>
struct Pointer;

template <typename Var>
using Type = std::variant<Bottom, Integer, Pointer<Var>, Signed, Unsigned,
                          BiSigned, Top, Var>;

template <typename Var>
struct Pointer {
  std::shared_ptr<Type<Var>> pointed;

  constexpr bool operator<(const Pointer& b) const {
    return std::less<Type<Var>>()(*pointed, *b.pointed);
  }
  constexpr bool operator>(const Pointer& b) const {
    return std::greater<Type<Var>>()(*pointed, *b.pointed);
  }
  constexpr bool operator<=(const Pointer& b) const {
    return std::greater_equal<Type<Var>>()(*pointed, *b.pointed);
  }
  constexpr bool operator>=(const Pointer& b) const {
    return std::less_equal<Type<Var>>()(*pointed, *b.pointed);
  }
  constexpr bool operator==(const Pointer& b) const {
    return std::equal_to<Type<Var>>()(*pointed, *b.pointed);
  }
  constexpr bool operator!=(const Pointer& b) const {
    return std::not_equal_to<Type<Var>>()(*pointed, *b.pointed);
  }
};

template <typename Var>
Type<Var> LeastUpperBound(Type<Var> a, Type<Var> b) {
  if (std::holds_alternative<Top>(a) || std::holds_alternative<Top>(b)) {
    return Top{};
  }

  if (std::holds_alternative<Bottom>(a)) {
    return b;
  }

  if (std::holds_alternative<Bottom>(b)) {
    return a;
  }

  if (std::holds_alternative<Integer>(a)) {
    auto int_a{std::get<Integer>(a)};
    if (std::holds_alternative<Signed>(b)) {
      auto int_b{std::get<Signed>(b)};
      return Signed{std::max({int_a.size, int_b.size})};
    }

    if (std::holds_alternative<Unsigned>(b)) {
      auto int_b{std::get<Unsigned>(b)};
      return Unsigned{std::max({int_a.size, int_b.size})};
    }

    if (std::holds_alternative<BiSigned>(b)) {
      auto int_b{std::get<BiSigned>(b)};
      return BiSigned{std::max({int_a.size, int_b.size})};
    }

    if (std::holds_alternative<Integer>(b)) {
      auto int_b{std::get<Integer>(b)};
      return Integer{std::max({int_a.size, int_b.size})};
    }
  }

  if (std::holds_alternative<Signed>(a)) {
    auto int_a{std::get<Signed>(a)};
    if (std::holds_alternative<Signed>(b)) {
      auto int_b{std::get<Signed>(b)};
      return Signed{std::max({int_a.size, int_b.size})};
    }

    if (std::holds_alternative<Unsigned>(b)) {
      auto int_b{std::get<Unsigned>(b)};
      return BiSigned{std::max({int_a.size, int_b.size})};
    }

    if (std::holds_alternative<BiSigned>(b)) {
      auto int_b{std::get<BiSigned>(b)};
      return BiSigned{std::max({int_a.size, int_b.size})};
    }

    if (std::holds_alternative<Integer>(b)) {
      auto int_b{std::get<Integer>(b)};
      return Signed{std::max({int_a.size, int_b.size})};
    }
  }

  if (std::holds_alternative<Unsigned>(a)) {
    auto int_a{std::get<Unsigned>(a)};
    if (std::holds_alternative<Signed>(b)) {
      auto int_b{std::get<Signed>(b)};
      return BiSigned{std::max({int_a.size, int_b.size})};
    }

    if (std::holds_alternative<Unsigned>(b)) {
      auto int_b{std::get<Unsigned>(b)};
      return Unsigned{std::max({int_a.size, int_b.size})};
    }

    if (std::holds_alternative<BiSigned>(b)) {
      auto int_b{std::get<BiSigned>(b)};
      return BiSigned{std::max({int_a.size, int_b.size})};
    }

    if (std::holds_alternative<Integer>(b)) {
      auto int_b{std::get<Integer>(b)};
      return Unsigned{std::max({int_a.size, int_b.size})};
    }
  }

  if (std::holds_alternative<BiSigned>(a)) {
    auto int_a{std::get<BiSigned>(a)};
    if (std::holds_alternative<Signed>(b)) {
      auto int_b{std::get<Signed>(b)};
      return BiSigned{std::max({int_a.size, int_b.size})};
    }

    if (std::holds_alternative<Unsigned>(b)) {
      auto int_b{std::get<Unsigned>(b)};
      return BiSigned{std::max({int_a.size, int_b.size})};
    }

    if (std::holds_alternative<BiSigned>(b)) {
      auto int_b{std::get<BiSigned>(b)};
      return BiSigned{std::max({int_a.size, int_b.size})};
    }

    if (std::holds_alternative<Integer>(b)) {
      auto int_b{std::get<Integer>(b)};
      return BiSigned{std::max({int_a.size, int_b.size})};
    }
  }

  if (std::holds_alternative<Pointer<Var>>(a) &&
      std::holds_alternative<Pointer<Var>>(b)) {
    auto ptr_a{std::get<Pointer<Var>>(a)};
    auto ptr_b{std::get<Pointer<Var>>(b)};
    return Pointer<Var>{std::make_shared<Type<Var>>(
        LeastUpperBound(*ptr_a.pointed, *ptr_b.pointed))};
  }

  if (std::holds_alternative<Var>(a) && std::holds_alternative<Var>(b)) {
    auto var_a{std::get<Var>(a)};
    auto var_b{std::get<Var>(b)};
    if (std::equal_to<Var>()(var_a, var_b)) {
      return var_a;
    }
  }

  return Top{};
}

template <typename Var>
llvm::MDNode* MakeNode(llvm::LLVMContext& ctx, Type<Var>& t) {
  auto GetConstant = [&ctx](unsigned value) {
    return llvm::ConstantAsMetadata::get(llvm::ConstantInt::getIntegerValue(
        llvm::Type::getInt32Ty(ctx), llvm::APInt(32, value)));
  };

  if (std::holds_alternative<Top>(t)) {
    return nullptr;
  }

  if (std::holds_alternative<Bottom>(t)) {
    return llvm::MDNode::get(
        ctx, {GetConstant(static_cast<unsigned>(AbstractType::Bottom))});
  }

  if (std::holds_alternative<Integer>(t)) {
    auto integer{std::get<Integer>(t)};
    return llvm::MDNode::get(
        ctx, {GetConstant(static_cast<unsigned>(AbstractType::Unsigned)),
              GetConstant(integer.size)});
  }

  if (std::holds_alternative<Signed>(t)) {
    auto integer{std::get<Signed>(t)};
    return llvm::MDNode::get(
        ctx, {GetConstant(static_cast<unsigned>(AbstractType::Signed)),
              GetConstant(integer.size)});
  }

  if (std::holds_alternative<Unsigned>(t)) {
    auto integer{std::get<Unsigned>(t)};
    return llvm::MDNode::get(
        ctx, {GetConstant(static_cast<unsigned>(AbstractType::Unsigned)),
              GetConstant(integer.size)});
  }

  if (std::holds_alternative<BiSigned>(t)) {
    auto integer{std::get<BiSigned>(t)};
    return llvm::MDNode::get(
        ctx, {GetConstant(static_cast<unsigned>(AbstractType::Unsigned)),
              GetConstant(integer.size)});
  }

  CHECK(!std::holds_alternative<Var>(t)) << "Cannot create node for variable";

  auto ptr{std::get<Pointer<Var>>(t)};
  auto sub{MakeNode(ctx, *ptr.pointed)};
  if (sub) {
    return llvm::MDNode::get(
        ctx, {GetConstant(static_cast<unsigned>(AbstractType::Pointer)), sub});
  }

  return nullptr;
}

template <typename Var>
bool ContainsVar(const Type<Var>& t, const Var& v) {
  if (std::holds_alternative<Var>(t)) {
    return std::equal_to<Var>()(std::get<Var>(t), v);
  }

  if (std::holds_alternative<Pointer<Var>>(t)) {
    auto ptr{std::get<Pointer<Var>>(t)};
    return ContainsVar(*ptr.pointed, v);
  }

  return false;
}

template <typename Var>
bool IsFree(const Type<Var>& t) {
  if (std::holds_alternative<Var>(t)) {
    return false;
  }

  if (std::holds_alternative<Pointer<Var>>(t)) {
    auto ptr{std::get<Pointer<Var>>(t)};
    return IsFree(*ptr.pointed);
  }

  return true;
}

template <typename Var>
std::string ToString(const Type<Var>& t) {
  if (std::holds_alternative<Var>(t)) {
    return "var";
  }

  if (std::holds_alternative<Pointer<Var>>(t)) {
    return "ptr(" + ToString<Var>(*std::get<Pointer<Var>>(t).pointed) + ")";
  }

  if (std::holds_alternative<Integer>(t)) {
    return "integer";
  }

  if (std::holds_alternative<Signed>(t)) {
    return "signed";
  }

  if (std::holds_alternative<Unsigned>(t)) {
    return "unsigned";
  }

  if (std::holds_alternative<Top>(t)) {
    return "top";
  }

  return "bottom";
}
}  // namespace AbstractTypes

template <typename Var>
AbstractTypes::Type<Var> AbstractTypeFromLLVM(llvm::Type* type) {
  if (type->isIntegerTy()) {
    return AbstractTypes::Integer{type->getScalarSizeInBits()};
  }

  if (type->isPointerTy()) {
    return AbstractTypes::Pointer<Var>{
        std::make_shared<AbstractTypes::Type<Var>>(AbstractTypes::Bottom{})};
  }

  /*if (type->isArrayTy()) {
    return AbstractTypeFromLLVM<Var>(type->getArrayElementType());
  }

  if (type->isVectorTy()) {
    return AbstractTypeFromLLVM<Var>(type->getScalarType());
  }*/

  return AbstractTypes::Top{};
}
}  // namespace

using Term = AbstractTypes::Type<llvm::Value*>;

class Equalities {
 private:
  std::set<std::pair<Term, Term>> eqs;
  std::unordered_map<llvm::Value*, Term> assignments;

  Term ApplyAssignments(const Term& t) {
    using namespace AbstractTypes;

    if (std::holds_alternative<llvm::Value*>(t)) {
      auto var{std::get<llvm::Value*>(t)};
      return assignments[var];
    }

    if (std::holds_alternative<Pointer<llvm::Value*>>(t)) {
      auto ptr{std::get<Pointer<llvm::Value*>>(t)};
      return Pointer<llvm::Value*>{
          std::make_shared<Term>(ApplyAssignments(*ptr.pointed))};
    }

    return t;
  }

 public:
  const std::set<std::pair<Term, Term>>& GetEqualities() const { return eqs; }
  const std::unordered_map<llvm::Value*, Term>& GetAssignments() const {
    return assignments;
  }

  void Add(std::pair<Term, Term> eq) { eqs.insert(eq); }

  void Unify() {
    using namespace AbstractTypes;
    while (!eqs.empty()) {
      // Delete
      auto it{eqs.begin()};
      while (it != eqs.end()) {
        if (it->first == it->second) {
          eqs.erase(it);
          it = eqs.begin();
          continue;
        }
        ++it;
      }

      // Decompose
      it = eqs.begin();
      while (it != eqs.end()) {
        if (std::holds_alternative<Pointer<llvm::Value*>>(it->first) &&
            std::holds_alternative<Pointer<llvm::Value*>>(it->second)) {
          auto ptr_a{std::get<Pointer<llvm::Value*>>(it->first)};
          auto ptr_b{std::get<Pointer<llvm::Value*>>(it->second)};
          eqs.erase(it);
          eqs.insert({ptr_a, ptr_b});
          it = eqs.begin();
          continue;
        }
        ++it;
      }

      // Swap
      it = eqs.begin();
      while (it != eqs.end()) {
        if (IsFree(it->first) && !IsFree(it->second)) {
          auto a{it->first};
          auto b{it->second};
          eqs.erase(it);
          eqs.insert({b, a});
          it = eqs.begin();
          continue;
        }
        ++it;
      }

      // Eliminate
      it = eqs.begin();
      while (it != eqs.end()) {
        if (std::holds_alternative<llvm::Value*>(it->first) &&
            IsFree(it->second)) {
          auto var{std::get<llvm::Value*>(it->first)};
          assignments[var] = LeastUpperBound(assignments[var], it->second);
          eqs.erase(it);
          it = eqs.begin();
          continue;
        }
        ++it;
      }

      // Occurs-check
      it = eqs.begin();
      while (it != eqs.end()) {
        if (std::holds_alternative<llvm::Value*>(it->first)) {
          auto var{std::get<llvm::Value*>(it->first)};
          if (ContainsVar(it->second, var)) {
            assignments[var] = Top{};
            eqs.erase(it);
            it = eqs.begin();
            continue;
          }
        }
        ++it;
      }

      // Substitution
      it = eqs.begin();
      while (it != eqs.end()) {
        if (std::holds_alternative<llvm::Value*>(it->first) &&
            !IsFree(it->second)) {
          auto var{std::get<llvm::Value*>(it->first)};
          auto new_term{ApplyAssignments(it->second)};
          eqs.erase(it);
          eqs.insert({var, new_term});
          it = eqs.begin();
          continue;
        }
        ++it;
      }
    }
  }
};

class EqualitiesGenerator : public llvm::InstVisitor<EqualitiesGenerator> {
 private:
  Equalities& equalities;

 public:
  EqualitiesGenerator(Equalities& equalities) : equalities(equalities) {}

  void visitFPToSIInst(llvm::FPToSIInst& inst) {
    equalities.Add(
        {&inst, AbstractTypes::Signed{inst.getType()->getScalarSizeInBits()}});
  }

  void visitFPToUIInst(llvm::FPToUIInst& inst) {
    equalities.Add({&inst, AbstractTypes::Unsigned{
                               inst.getType()->getScalarSizeInBits()}});
  }

  void visitSIToFPInst(llvm::SIToFPInst& inst) {
    equalities.Add({inst.getOperand(0), AbstractTypes::Signed{}});
  }

  void visitUIToFPInst(llvm::UIToFPInst& inst) {
    equalities.Add({inst.getOperand(0), AbstractTypes::Unsigned{}});
  }

  void visitICmpInst(llvm::ICmpInst& inst) {
    if (inst.isSigned()) {
      equalities.Add({inst.getOperand(0), AbstractTypes::Signed{}});
      equalities.Add({inst.getOperand(1), AbstractTypes::Signed{}});
    }
  }

  void visitSExtInst(llvm::SExtInst& inst) {
    equalities.Add({inst.getOperand(0), AbstractTypes::Signed{}});
  }

  void visitBinaryOperator(llvm::BinaryOperator& inst) {
    switch (inst.getOpcode()) {
      case llvm::BinaryOperator::SRem:
      case llvm::BinaryOperator::SDiv:
        equalities.Add({&inst, AbstractTypes::Signed{
                                   inst.getType()->getScalarSizeInBits()}});
        equalities.Add({inst.getOperand(0), AbstractTypes::Signed{}});
        equalities.Add({inst.getOperand(1), AbstractTypes::Signed{}});
        break;
      case llvm::BinaryOperator::URem:
      case llvm::BinaryOperator::UDiv:
        equalities.Add({&inst, AbstractTypes::Unsigned{
                                   inst.getType()->getScalarSizeInBits()}});
        equalities.Add({inst.getOperand(0), AbstractTypes::Integer{}});
        equalities.Add({inst.getOperand(1), AbstractTypes::Integer{}});
        break;
      case llvm::BinaryOperator::Add:
      case llvm::BinaryOperator::Sub:
      case llvm::BinaryOperator::Mul:
        equalities.Add({&inst, inst.getOperand(0)});
        equalities.Add({&inst, inst.getOperand(1)});
      default:
        break;
    }
  }

  void visitCallInst(llvm::CallInst& inst) {
    equalities.Add({&inst, inst.getCalledFunction()});
  }

  void visitAllocaInst(llvm::AllocaInst& inst) {
    equalities.Add(
        {&inst,
         AbstractTypes::Pointer<llvm::Value*>{std::make_unique<Term>(
             AbstractTypeFromLLVM<llvm::Value*>(inst.getAllocatedType()))}});
  }

  void visitLoadInst(llvm::LoadInst& inst) {
    auto type{AbstractTypeFromLLVM<llvm::Value*>(inst.getType())};
    equalities.Add({&inst, type});
    equalities.Add(
        {inst.getPointerOperand(),
         AbstractTypes::Pointer<llvm::Value*>{std::make_unique<Term>(type)}});
  }

  void visitStoreInst(llvm::StoreInst& inst) {
    equalities.Add({inst.getPointerOperand(),
                    AbstractTypes::Pointer<llvm::Value*>{
                        std::make_unique<Term>(inst.getValueOperand())}});
    equalities.Add(
        {inst.getValueOperand(), AbstractTypeFromLLVM<llvm::Value*>(
                                     inst.getValueOperand()->getType())});
  }

  void visitGetElementPtrInst(llvm::GetElementPtrInst& inst) {
    auto type{AbstractTypeFromLLVM<llvm::Value*>(inst.getResultElementType())};
    equalities.Add({&inst, AbstractTypes::Pointer<llvm::Value*>{
                               std::make_unique<Term>(type)}});
  }
};

void PerformTypeInference(llvm::Module& module) {
  using namespace AbstractTypes;

  Equalities equalities;

  EqualitiesGenerator gen{equalities};
  gen.visit(module);
  equalities.Unify();
  for (auto assign : equalities.GetAssignments()) {
    if (!assign.first) {
      continue;
    }

    if (auto gobj = llvm::dyn_cast<llvm::GlobalObject>(assign.first)) {
      gobj->setMetadata("rellic.type",
                        MakeNode(module.getContext(), assign.second));
    }

    if (auto inst = llvm::dyn_cast<llvm::Instruction>(assign.first)) {
      inst->setMetadata("rellic.type",
                        MakeNode(module.getContext(), assign.second));
    }
  }
}
}  // namespace rellic