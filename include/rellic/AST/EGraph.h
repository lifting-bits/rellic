/*
 * Copyright (c) 2022-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once
#include <clang/AST/Decl.h>
#include <clang/AST/DeclGroup.h>
#include <clang/AST/Expr.h>
#include <clang/AST/OperationKinds.h>
#include <clang/AST/PrettyPrinter.h>
#include <clang/AST/Stmt.h>
#include <clang/Basic/LangOptions.h>
#include <glog/logging.h>
#include <llvm/ADT/APFloat.h>
#include <llvm/Support/raw_ostream.h>

#include <algorithm>
#include <functional>
#include <iterator>
#include <limits>
#include <numeric>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "rellic/AST/ASTBuilder.h"
#include "rellic/AST/Util.h"

namespace rellic {

struct EClass {
  size_t id{static_cast<size_t>(-1)};

  bool operator==(const EClass& b) const { return id == b.id; }
  bool operator!=(const EClass& b) const { return id != b.id; }
};

struct FieldAccess {
  clang::FieldDecl* field_decl;
  bool is_arrow;
};

struct ENode {
  size_t id;
  clang::Stmt::StmtClass node_class;

  // FIXME(frabert): These really need to be a union
  llvm::APInt int_value;
  unsigned char_value;
  llvm::APFloat float_value{0.f};
  std::string string_value;
  clang::ValueDecl* val_decl;
  FieldAccess field_access;
  clang::DeclGroupRef decl_group;
  clang::QualType type;
  clang::UnaryOperatorKind unop;
  clang::BinaryOperatorKind binop;

  std::vector<EClass> children;

  // Label for debugging purposes. Indicates what produced this node.
  const char* label;

  ENode() : node_class(clang::Stmt::NullStmtClass) {}
  ENode(size_t id, llvm::APInt int_value)
      : id(id),
        node_class(clang::Stmt::IntegerLiteralClass),
        int_value(int_value) {}
  ENode(size_t id, unsigned char_value)
      : id(id),
        node_class(clang::Stmt::CharacterLiteralClass),
        char_value(char_value) {}
  ENode(size_t id, llvm::APFloat apf)
      : id(id),
        node_class(clang::Stmt::FloatingLiteralClass),
        float_value(apf) {}
  ENode(size_t id, const std::string& s)
      : id(id), node_class(clang::Stmt::StringLiteralClass) {
    new (&string_value) std::string(s);
  }
  ENode(size_t id, clang::ValueDecl* decl)
      : id(id), node_class(clang::Stmt::DeclRefExprClass), val_decl(decl) {}
  ENode(size_t id, clang::FieldDecl* decl, bool is_arrow, EClass child)
      : id(id),
        node_class(clang::Stmt::MemberExprClass),
        field_access{decl, is_arrow},
        children({child}) {}
  ENode(size_t id, clang::DeclGroupRef group)
      : id(id), node_class(clang::Stmt::DeclStmtClass) {
    new (&decl_group) clang::DeclGroupRef(group);
  }
  ENode(size_t id, clang::Stmt::StmtClass cl, clang::QualType type,
        EClass child)
      : id(id), node_class(cl), type(type), children({child}) {
    new (&type) clang::QualType(type);
  }
  ENode(size_t id, clang::UnaryOperatorKind unop, EClass child)
      : id(id),
        node_class(clang::Stmt::UnaryOperatorClass),
        unop(unop),
        children({child}) {}
  ENode(size_t id, clang::BinaryOperatorKind binop, EClass left, EClass right)
      : id(id),
        node_class(clang::Stmt::BinaryOperatorClass),
        binop(binop),
        children({left, right}) {}
  ENode(size_t id, clang::Stmt::StmtClass node_class,
        const std::vector<EClass> children)
      : id(id), node_class(node_class), children(children) {}
  ENode(size_t id, EClass base, EClass idx)
      : id(id),
        node_class(clang::Stmt::ArraySubscriptExprClass),
        children({base, idx}) {}

  bool operator==(const ENode& b) const {
    if (id != b.id) {
      return false;
    }

    if (node_class != b.node_class) {
      return false;
    }

    switch (node_class) {
      default:
        break;
      case clang::Stmt::IntegerLiteralClass:
        return int_value == b.int_value;
      case clang::Stmt::CharacterLiteralClass:
        return char_value == b.char_value;
      case clang::Stmt::FloatingLiteralClass:
        return float_value == b.float_value;
      case clang::Stmt::StringLiteralClass:
        return string_value == b.string_value;
      case clang::Stmt::DeclRefExprClass:
        return val_decl == b.val_decl;
      case clang::Stmt::MemberExprClass:
        if (!(field_access.is_arrow == b.field_access.is_arrow &&
              field_access.field_decl == b.field_access.field_decl)) {
          return false;
        }
        break;
      case clang::Stmt::DeclStmtClass:
        return decl_group.getAsOpaquePtr() == b.decl_group.getAsOpaquePtr();
      case clang::Stmt::CStyleCastExprClass:
        if (type != b.type) {
          return false;
        }
        break;
      case clang::Stmt::UnaryOperatorClass:
        if (unop != b.unop) {
          return false;
        }
        break;
      case clang::Stmt::BinaryOperatorClass:
        if (binop != b.binop) {
          return false;
        }
        break;
    }

    if (children.size() != b.children.size()) {
      return false;
    }

    for (auto i{0UL}; i < children.size(); ++i) {
      if (children[i] != b.children[i]) {
        return false;
      }
    }
    return true;
  }

  bool operator!=(const ENode& b) const { return !(*this == b); }
};

static constexpr EClass invalid_class = {static_cast<size_t>(-1)};
static constexpr EClass null_class = {0};

using Use = std::pair<ENode, EClass>;
}  // namespace rellic

template <>
struct ::std::hash<rellic::EClass> {
  constexpr std::size_t operator()(const rellic::EClass& e) const {
    return ::std::hash<size_t>()(e.id);
  }
};

template <>
struct ::std::hash<rellic::ENode> {
  constexpr std::size_t operator()(const rellic::ENode& n) const {
    return std::hash<size_t>()(n.id);
    auto h{::std::hash<clang::Stmt::StmtClass>()(n.node_class)};
    switch (n.node_class) {
      default:
        break;
      case clang::Stmt::IntegerLiteralClass:
        h ^= llvm::hash_value(n.int_value) << 1;
        break;
      case clang::Stmt::CharacterLiteralClass:
        h ^= ::std::hash<unsigned>()(n.char_value) << 1;
        break;
      case clang::Stmt::FloatingLiteralClass:
        h ^= llvm::hash_value(n.float_value) << 1;
        break;
      case clang::Stmt::StringLiteralClass:
        h ^= ::std::hash<std::string>()(n.string_value) << 1;
        break;
      case clang::Stmt::DeclRefExprClass:
        h ^= ::std::hash<clang::Decl*>()(n.val_decl) << 1;
        break;
      case clang::Stmt::MemberExprClass:
        if (n.field_access.is_arrow) {
          h ^= ~::std::hash<clang::ValueDecl*>()(n.field_access.field_decl)
               << 1;
        } else {
          h ^= ::std::hash<clang::ValueDecl*>()(n.field_access.field_decl) << 1;
        }
        break;
      case clang::Stmt::DeclStmtClass:
        h ^= ::std::hash<void*>()(n.decl_group.getAsOpaquePtr()) << 1;
        break;
      case clang::Stmt::CStyleCastExprClass:
        // FIXME(frabert): No hash for QualType?
        break;
      case clang::Stmt::UnaryOperatorClass:
        h ^= ::std::hash<clang::UnaryOperatorKind>()(n.unop) << 1;
        break;
      case clang::Stmt::BinaryOperatorClass:
        h ^= ::std::hash<clang::BinaryOperatorKind>()(n.binop) << 1;
        break;
    }
    for (auto i{0U}; i < n.children.size(); ++i) {
      h ^= ::std::hash<rellic::EClass>()(n.children[i]) << (2 + i);
    }
    return h;
  }
};

namespace rellic {

using EqSet = std::unordered_set<ENode>;
using ClassMap = std::unordered_map<EClass, EqSet>;
using CostMap = std::unordered_map<EClass, std::pair<float, ENode>>;

class EGraph {
  size_t version{0};

  std::vector<EClass> class_parents;
  std::vector<std::vector<Use>> class_uses;

  std::unordered_map<ENode, EClass> hashcons;
  std::vector<EClass> worklist;

  EClass NewSingletonEClass() {
    CHECK_EQ(class_parents.size(), class_uses.size());
    auto new_class{class_parents.size()};
    class_parents.push_back(invalid_class);
    class_uses.emplace_back();
    return {new_class};
  }

  EClass FindCanonical(EClass cid) {
    CHECK(cid != invalid_class);
    auto& parent{class_parents[cid.id]};
    if (parent == invalid_class) {
      return cid;
    }
    parent = FindCanonical(parent);
    return parent;
  }

  ENode Canonicalize(const ENode& enode) {
    ENode res = enode;
    res.id = enode.id;
    for (auto& child : res.children) {
      child = FindCanonical(child);
    }
    return res;
  }

  void Repair(EClass cid) {
    // CHECK_EQ(id->parent, nullptr);
    auto uses = class_uses[cid.id];
    class_uses[cid.id].clear();
    for (auto& [node, eclass] : uses) {
      auto hash = hashcons.find(node);
      if (hash != hashcons.end()) {
        hashcons.erase(hash);
      }
      auto can_node{Canonicalize(node)};
      hashcons[can_node] = FindCanonical(eclass);
    }

    std::unordered_map<ENode, EClass> new_uses;
    for (auto& [node, eclass] : uses) {
      auto can_node{Canonicalize(node)};
      if (new_uses.find(can_node) != new_uses.end()) {
        Merge(eclass, new_uses[can_node]);
      }
      new_uses[can_node] = FindCanonical(eclass);
    }
    auto eclass{FindCanonical(cid)};
    for (auto use : new_uses) {
      class_uses[eclass.id].push_back(use);
    }
  }

  const char* ClassToString(clang::Stmt::StmtClass c) {
    switch (c) {
      default:
        return "Unknown class";
#define STMT(CLASS, PARENT)       \
  case clang::Stmt::CLASS##Class: \
    return #CLASS;
#define ABSTRACT_STMT(STMT)
#include <clang/AST/StmtNodes.inc>
    }
  }

  clang::Stmt* Extract(EClass cid, CostMap& costs, clang::ASTUnit& unit,
                       StmtToIRMap& provenance) {
    ASTBuilder ast(unit);
    CHECK(cid != invalid_class);
    if (cid == null_class) {
      return nullptr;
    }
    auto node{costs[cid].second};
    auto& children{node.children};
    auto ext = [&](size_t idx) {
      return Extract(children[idx], costs, unit, provenance);
    };
    auto extExpr = [&](size_t idx) {
      return clang::cast<clang::Expr>(
          Extract(children[idx], costs, unit, provenance));
    };
    switch (node.node_class) {
      default:
        LOG(FATAL) << "Unknown class " << ClassToString(node.node_class);
        return nullptr;
      case clang::Stmt::IntegerLiteralClass:
        return ast.CreateIntLit(node.int_value);
      case clang::Stmt::CharacterLiteralClass:
        return ast.CreateCharLit(node.char_value);
      case clang::Stmt::FloatingLiteralClass:
        return ast.CreateFPLit(node.float_value);
      case clang::Stmt::StringLiteralClass:
        return ast.CreateStrLit(node.string_value);
      case clang::Stmt::DeclRefExprClass:
        return ast.CreateDeclRef(node.val_decl);
      case clang::Stmt::MemberExprClass:
        return ast.CreateFieldAcc(extExpr(0), node.field_access.field_decl,
                                  node.field_access.is_arrow);
      case clang::Stmt::DeclStmtClass:
        return ast.CreateDeclStmt(node.decl_group);
      case clang::Stmt::CStyleCastExprClass:
        return ast.CreateCStyleCast(node.type, extExpr(0));
      case clang::Stmt::ImplicitCastExprClass:
        return extExpr(0);
      case clang::Stmt::UnaryOperatorClass:
        return ast.CreateUnaryOp(node.unop, extExpr(0));
      case clang::Stmt::BinaryOperatorClass:
        return ast.CreateBinaryOp(node.binop, extExpr(0), extExpr(1));
      case clang::Stmt::CompoundStmtClass: {
        std::vector<clang::Stmt*> body;
        for (size_t i{0}; i < children.size(); ++i) {
          body.push_back(ext(i));
        }
        return ast.CreateCompoundStmt(body);
      }
      case clang::Stmt::IfStmtClass:
        return ast.CreateIf(extExpr(0), ext(1), ext(2));
      case clang::Stmt::WhileStmtClass:
        return ast.CreateWhile(extExpr(0), ext(1));
      case clang::Stmt::DoStmtClass:
        return ast.CreateDo(extExpr(1), ext(0));
      case clang::Stmt::ParenExprClass:
        return ast.CreateParen(extExpr(0));
      case clang::Stmt::CallExprClass: {
        std::vector<clang::Expr*> args;
        for (size_t i{1}; i < children.size(); ++i) {
          args.push_back(extExpr(i));
        }
        return ast.CreateCall(extExpr(0), args);
      }
      case clang::Stmt::ConditionalOperatorClass:
        return ast.CreateConditional(extExpr(0), extExpr(1), extExpr(2));
      case clang::Stmt::BreakStmtClass:
        return ast.CreateBreak();
      case clang::Stmt::ReturnStmtClass:
        return ast.CreateReturn(extExpr(0));
      case clang::Stmt::NullStmtClass:
        return ast.CreateNullStmt();
      case clang::Stmt::ArraySubscriptExprClass:
        return ast.CreateArraySub(extExpr(0), extExpr(1));
    }
  }

  static void escape(llvm::raw_ostream& os, const std::string& s) {
    for (auto c : s) {
      switch (c) {
        default:
          os << c;
          break;
        case '\n':
          os << "\\l";
          break;
        case '"':
          os << "\\\"";
          break;
        case '|':
          os << "\\|";
          break;
        case '{':
          os << "\\{";
          break;
        case '}':
          os << "\\}";
          break;
        case ' ':
          os << "&nbsp;";
          break;
        case '\\':
          os << "\\\\";
          break;
      }
    }
  }

 public:
  EGraph() {
    // Add EClass representing nullptr
    class_parents.push_back(invalid_class);
    class_uses.emplace_back();
  }

  size_t GetVersion() { return version; }

  ClassMap GetClasses() {
    ClassMap res;
    for (auto [node, eid] : hashcons) {
      eid = FindCanonical(eid);
      res[eid].insert(node);
    }
    return res;
  }

  EClass AddENode(const ENode& enode) {
    auto can_enode{Canonicalize(enode)};
    if (!hashcons.count(can_enode)) {
      ++version;
      auto eclass{NewSingletonEClass()};
      for (auto& arg : can_enode.children) {
        class_uses[arg.id].push_back({can_enode, eclass});
      }
      hashcons[can_enode] = eclass;
      return eclass;
    } else {
      return FindCanonical(hashcons[can_enode]);
    }
  }

  EClass AddNode(clang::Stmt* node, const char* label) {
    if (!node) {
      return null_class;
    }
    auto id{std::hash<clang::Stmt*>()(node)};
    auto create_node = [&]() {
      if (auto int_lit = clang::dyn_cast<clang::IntegerLiteral>(node)) {
        return ENode(id, int_lit->getValue());
      } else if (auto char_lit =
                     clang::dyn_cast<clang::CharacterLiteral>(node)) {
        return ENode(id, char_lit->getValue());
      } else if (auto fp_lit = clang::dyn_cast<clang::FloatingLiteral>(node)) {
        return ENode(id, fp_lit->getValue());
      } else if (auto str_lit = clang::dyn_cast<clang::StringLiteral>(node)) {
        return ENode(id, str_lit->getString().str());
      } else if (auto declref = clang::dyn_cast<clang::DeclRefExpr>(node)) {
        return ENode(id, declref->getDecl());
      } else if (auto declstmt = clang::dyn_cast<clang::DeclStmt>(node)) {
        return ENode(id, declstmt->getDeclGroup());
      } else if (auto ccast = clang::dyn_cast<clang::CStyleCastExpr>(node)) {
        return ENode(id, ccast->getStmtClass(), ccast->getType(),
                     AddNode(ccast->getSubExpr(), label));
      } else if (auto icast = clang::dyn_cast<clang::ImplicitCastExpr>(node)) {
        return ENode(id, icast->getStmtClass(), icast->getType(),
                     AddNode(icast->getSubExpr(), label));
      } else if (auto memb = clang::dyn_cast<clang::MemberExpr>(node)) {
        return ENode(id, clang::cast<clang::FieldDecl>(memb->getMemberDecl()),
                     memb->isArrow(), AddNode(memb->getBase(), label));
      } else if (auto unop = clang::dyn_cast<clang::UnaryOperator>(node)) {
        return ENode(id, unop->getOpcode(), AddNode(unop->getSubExpr(), label));
      } else if (auto binop = clang::dyn_cast<clang::BinaryOperator>(node)) {
        return ENode(id, binop->getOpcode(), AddNode(binop->getLHS(), label),
                     AddNode(binop->getRHS(), label));
      } else if (auto arr = clang::dyn_cast<clang::ArraySubscriptExpr>(node)) {
        return ENode(id, AddNode(arr->getBase(), label),
                     AddNode(arr->getIdx(), label));
      } else {
        std::vector<EClass> children;
        for (auto child : node->children()) {
          children.push_back(AddNode(child, label));
        }
        return ENode(id, node->getStmtClass(), children);
      }
    };
    auto enode{create_node()};
    enode.label = label;
    return AddENode(enode);
  }

  void Merge(EClass a, EClass b) {
    a = FindCanonical(a);
    b = FindCanonical(b);
    if (a == b) {
      return;
    }
    auto& a_uses{class_uses[a.id]};
    auto& b_uses{class_uses[b.id]};
    ++version;
    class_parents[b.id] = a;
    std::copy(b_uses.begin(), b_uses.end(), std::back_inserter(a_uses));
    b_uses.clear();
    worklist.push_back(a);
  }

  void Rebuild() {
    while (!worklist.empty()) {
      std::unordered_set<EClass> todo{worklist.begin(), worklist.end()};
      worklist.clear();
      for (auto id : todo) {
        Repair(id);
      }
    }
  }

  clang::Stmt* Schedule(EClass result, clang::ASTUnit& unit,
                        StmtToIRMap& provenance) {
    result = FindCanonical(result);
    auto changed{true};
    auto eclasses{GetClasses()};
    CostMap costs;
    costs[null_class] = {0.f, {}};

    for (auto& [eclass, enodes] : eclasses) {
      for (auto& enode : enodes) {
        costs[eclass] = {std::numeric_limits<float>::infinity(), enode};
        break;
      }
    }

    auto NodeCost = [&](ENode node) {
      float cost{1};
      for (auto& child : node.children) {
        cost += costs[child].first;
      }
      return cost;
    };

    while (changed) {
      changed = false;
      for (auto& [eclass, enodes] : eclasses) {
        for (auto enode : enodes) {
          auto new_cost{NodeCost(enode)};
          auto& best{costs[eclass]};
          if (new_cost < best.first) {
            best = {new_cost, enode};
            changed = true;
          }
        }
      }
    }

    return Extract(result, costs, unit, provenance);
  }

  void PrintGraph(llvm::raw_ostream& os) {
    static int numgraph = 0;
    os << "digraph G" << numgraph++
       << " {\ngraph [rankdir = LR];\nnode [shape=\"record\" "
          "fontname=monospace];\n";
    auto classes{GetClasses()};
    for (auto& [class_id, nodes] : classes) {
      os << "e" << class_id.id << " [label=\"e" << class_id.id
         << "\" shape=\"circle\"];\n";

      if (class_id != null_class) {
        for (auto& node : nodes) {
          auto node_id{std::hash<ENode>()(node)};
          os << "e" << class_id.id << " -> n" << node_id << ";\n";

          for (size_t i = 0; i < node.children.size(); i++) {
            os << "n" << node_id << ":c" << i << " -> e" << node.children[i].id
               << ";\n";
          }

          os << "n" << node_id << " [label=\"";
          if (node.label) {
            escape(os, node.label);
            os << '|';
          }
          switch (node.node_class) {
            default:
              break;
#define STMT(CLASS, PARENT)       \
  case clang::Stmt::CLASS##Class: \
    os << #CLASS;                 \
    break;
#define ABSTRACT_STMT(STMT)
#include <clang/AST/StmtNodes.inc>
          }

          for (size_t i = 0; i < node.children.size(); i++) {
            os << "|<c" << i << ">";
          }
          os << "\"];\n";
        }
      }
    }
    os << "}\n\n\n";
  }
};

namespace matchers {
// Matcher :: ClassMap -> EClass -> Maybe<Result>

template <typename T>
class Maybe {
  bool has_value;
  T result;

 public:
  using type = T;

  Maybe() : has_value(false) {}
  Maybe(const T& value) : has_value(true), result(value) {}

  bool HasValue() { return has_value; }

  T GetValue() {
    CHECK(has_value);
    return result;
  }

  operator bool() { return has_value; }
};

using MResult = Maybe<EClass>;

inline auto any() {
  return [](ClassMap& classes, EClass id) -> MResult { return MResult(id); };
}

inline auto null() {
  return [](ClassMap& classes, EClass id) {
    return id.id == 0 ? MResult(id) : MResult();
  };
}

template <typename TPredicate>
auto exists(TPredicate pred) {
  return [pred](ClassMap& classes,
                EClass id) -> decltype(pred(classes, ENode(), id)) {
    for (auto& node : classes[id]) {
      auto res{pred(classes, node, id)};
      if (res) {
        return res;
      }
    }
    return {};
  };
}

template <typename TMatcher>
auto notMatch(TMatcher match) {
  return [match](ClassMap& classes, EClass id) -> MResult {
    if (match(classes, id)) {
      return {};
    } else {
      return MResult(id);
    }
  };
}

template <typename T>
auto either(T pred) {
  return pred;
}

template <typename T, typename... Ts>
auto either(T pred, Ts... preds) {
  return [pred, preds...](ClassMap& classes, EClass id) {
    auto res{pred(classes, id)};
    if (res) {
      return res;
    } else {
      return either(preds...)(classes, id);
    }
  };
}

template <typename TMatcher>
auto cast(TMatcher match) {
  return exists(
      [match](ClassMap& map, const ENode& node, EClass id)
          -> Maybe<std::tuple<EClass, decltype(match(map, id).GetValue())>> {
        switch (node.node_class) {
          case clang::Stmt::CStyleCastExprClass:
          case clang::Stmt::ImplicitCastExprClass: {
            auto res{match(map, node.children[0])};
            if (res) {
              auto val{res.GetValue()};
              return Maybe<std::tuple<EClass, decltype(val)>>({id, val});
            }
          }
          default:
            return {};
        }
      });
}

template <typename TMatcher>
auto implicitCast(TMatcher match) {
  return exists(
      [match](ClassMap& map, const ENode& node, EClass id)
          -> Maybe<std::tuple<EClass, decltype(match(map, id).GetValue())>> {
        if (node.node_class == clang::Stmt::ImplicitCastExprClass) {
          auto res{match(map, node.children[0])};
          if (res) {
            auto val{res.GetValue()};
            return Maybe<std::tuple<EClass, decltype(val)>>({id, val});
          }
        }
        return {};
      });
}

template <typename TMatcher>
auto cStyleCast(TMatcher match) {
  return exists(
      [match](ClassMap& map, const ENode& node, EClass id)
          -> Maybe<std::tuple<EClass, decltype(match(map, id).GetValue())>> {
        if (node.node_class == clang::Stmt::CStyleCastExprClass) {
          auto res{match(map, node.children[0])};
          if (res) {
            auto val{res.GetValue()};
            return Maybe<std::tuple<EClass, decltype(val)>>({id, val});
          }
        }
        return {};
      });
}

template <typename TMatcher>
auto paren(TMatcher match) {
  return exists(
      [match](ClassMap& map, const ENode& node, EClass id)
          -> Maybe<std::tuple<EClass, decltype(match(map, id).GetValue())>> {
        if (node.node_class == clang::Stmt::ParenExprClass) {
          auto res{match(map, node.children[0])};
          if (res) {
            auto val{res.GetValue()};
            return Maybe<std::tuple<EClass, decltype(val)>>({id, val});
          }
        }
        return {};
      });
}

inline auto literalTrue() {
  return exists([](ClassMap& map, const ENode& node, EClass id) -> MResult {
    if ((node.node_class == clang::Stmt::IntegerLiteralClass &&
         node.int_value.getBoolValue()) ||
        (node.node_class == clang::Stmt::CharacterLiteralClass &&
         node.char_value)) {
      return MResult(id);
    } else {
      return {};
    }
  });
}

template <typename TClass>
auto stmtClass(TClass c) {
  return exists(
      [c](ClassMap& classes, const ENode& node, EClass id) -> MResult {
        if (c(node.node_class)) {
          return {id};
        }
        return {};
      });
}

inline auto nullStmt() {
  return exists([](ClassMap& map, const ENode& node, EClass id) -> MResult {
    if (node.node_class == clang::Stmt::NullStmtClass) {
      return MResult(id);
    } else {
      return {};
    }
  });
}

template <typename TCond, typename TThen, typename TElse>
auto ifStmt(TCond mcond, TThen mthen, TElse melse) {
  return exists(
      [mcond, mthen, melse](ClassMap& classes, const ENode& node, EClass id)
          -> Maybe<std::tuple<
              EClass, decltype(mcond(classes, node.children[0]).GetValue()),
              decltype(mthen(classes, node.children[1]).GetValue()),
              decltype(melse(classes, node.children[2]).GetValue())>> {
        if (node.node_class == clang::Stmt::IfStmtClass) {
          auto condres{mcond(classes, node.children[0])};
          auto thenres{mthen(classes, node.children[1])};
          auto elseres{melse(classes, node.children[2])};
          if (condres && thenres && elseres) {
            return Maybe<std::tuple<
                EClass, decltype(mcond(classes, node.children[0]).GetValue()),
                decltype(mthen(classes, node.children[1]).GetValue()),
                decltype(melse(classes, node.children[2]).GetValue())>>(
                {id, condres.GetValue(), thenres.GetValue(),
                 elseres.GetValue()});
          }
        }
        return {};
      });
}

template <typename TCond, typename TBody>
auto whileStmt(TCond mcond, TBody mbody) {
  return exists(
      [mcond, mbody](ClassMap& classes, const ENode& node, EClass id)
          -> Maybe<std::tuple<
              EClass, decltype(mcond(classes, node.children[0]).GetValue()),
              decltype(mbody(classes, node.children[1]).GetValue())>> {
        if (node.node_class == clang::Stmt::WhileStmtClass) {
          auto condres{mcond(classes, node.children[0])};
          auto bodyres{mbody(classes, node.children[1])};
          if (condres && bodyres) {
            return Maybe<std::tuple<
                EClass, decltype(mcond(classes, node.children[0]).GetValue()),
                decltype(mbody(classes, node.children[1]).GetValue())>>(
                {id, condres.GetValue(), bodyres.GetValue()});
          }
        }
        return {};
      });
}

template <typename TOp, typename TExpr>
auto unop(TOp opcode, TExpr mexpr) {
  return exists(
      [opcode, mexpr](ClassMap& classes, const ENode& node, EClass id)
          -> Maybe<std::tuple<
              EClass, clang::UnaryOperator::Opcode,
              decltype(mexpr(classes, node.children[0]).GetValue())>> {
        if (node.node_class == clang::Stmt::UnaryOperatorClass &&
            opcode(node.unop)) {
          auto exprres{mexpr(classes, node.children[0])};
          if (exprres) {
            return Maybe<std::tuple<
                EClass, clang::UnaryOperator::Opcode,
                decltype(mexpr(classes, node.children[0]).GetValue())>>(
                {id, node.unop, exprres.GetValue()});
          }
        }
        return {};
      });
}

template <typename TOp, typename TLeft, typename TRight>
auto binop(TOp opcode, TLeft mleft, TRight mright) {
  return exists(
      [opcode, mleft, mright](ClassMap& classes, const ENode& node, EClass id)
          -> Maybe<std::tuple<
              EClass, clang::BinaryOperator::Opcode,
              decltype(mleft(classes, node.children[0]).GetValue()),
              decltype(mright(classes, node.children[0]).GetValue())>> {
        if (node.node_class == clang::Stmt::BinaryOperatorClass &&
            opcode(node.binop)) {
          auto leftres{mleft(classes, node.children[0])};
          if (leftres) {
            auto rightres{mright(classes, node.children[1])};
            if (rightres) {
              return Maybe<std::tuple<EClass, clang::BinaryOperator::Opcode,
                                      decltype(leftres.GetValue()),
                                      decltype(rightres.GetValue())>>(
                  {id, node.binop, leftres.GetValue(), rightres.GetValue()});
            }
          }
        }
        return {};
      });
}

inline auto breakStmt() {
  return exists(
      [](ClassMap& classes, const ENode& node, EClass id) -> Maybe<EClass> {
        if (node.node_class == clang::Stmt::BreakStmtClass) {
          return Maybe(id);
        }
        return {};
      });
}

template <typename TComp>
auto compound(TComp mcomp) {
  return exists(
      [mcomp](ClassMap& classes, const ENode& node, EClass id)
          -> Maybe<std::tuple<
              EClass, decltype(mcomp(classes, id, node.children).GetValue())>> {
        if (node.node_class == clang::Stmt::CompoundStmtClass) {
          auto compres{mcomp(classes, id, node.children)};
          if (compres) {
            return Maybe<
                std::tuple<EClass, decltype(mcomp(classes, id, node.children)
                                                .GetValue())>>(
                {id, compres.GetValue()});
          }
        }
        return {};
      });
}

template <typename TMatch>
auto one(TMatch match) {
  return
      [match](ClassMap& classes, EClass id, const std::vector<EClass>& children)
          -> Maybe<std::tuple<EClass, decltype(match(classes, children[0]))>> {
        if (children.size() == 1) {
          return match(classes, children[0]);
        }
        return {};
      };
}

template <typename TMatch>
auto all(TMatch match) {
  return
      [match](ClassMap& classes, EClass id, const std::vector<EClass>& children)
          -> Maybe<
              std::vector<decltype(match(classes, children[0]).GetValue())>> {
        std::vector<decltype(match(classes, children[0]).GetValue())> res;
        for (auto child : children) {
          auto partial{match(classes, child)};
          if (partial) {
            res.push_back(partial.GetValue());
          } else {
            return {};
          }
        }
        return Maybe<decltype(res)>({id, res});
      };
}

template <typename TMatch>
auto filter(TMatch match) {
  return
      [match](ClassMap& classes, EClass id, const std::vector<EClass>& children)
          -> Maybe<
              std::vector<decltype(match(classes, children[0]).GetValue())>> {
        std::vector<decltype(match(classes, children[0]).GetValue())> res;
        for (auto child : children) {
          auto partial{match(classes, child)};
          if (partial) {
            res.push_back(partial.GetValue());
          }
        }
        return Maybe<decltype(res)>(res);
      };
}

inline auto empty() {
  return [](ClassMap& classes, EClass id,
            const std::vector<EClass>& children) -> MResult {
    if (children.size() == 0) {
      return MResult(id);
    }
    return {};
  };
}

template <typename TMatcher, typename TMap>
auto operator>>(TMatcher matcher, TMap map) {
  return [matcher, map](ClassMap& classes, EClass id)
             -> Maybe<decltype(map(matcher(classes, id).GetValue()))> {
    auto res{matcher(classes, id)};
    if (res) {
      return Maybe<decltype(map(res.GetValue()))>(map(res.GetValue()));
    }
    return {};
  };
}

}  // namespace matchers

}  // namespace rellic