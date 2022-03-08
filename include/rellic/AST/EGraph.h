/*
 * Copyright (c) 2022-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once
#include <clang/AST/PrettyPrinter.h>
#include <clang/AST/Stmt.h>
#include <clang/Basic/LangOptions.h>
#include <glog/logging.h>
#include <llvm/Support/raw_ostream.h>

#include <algorithm>
#include <functional>
#include <iterator>
#include <limits>
#include <numeric>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "rellic/AST/Util.h"

namespace rellic {

struct ENode {
  clang::Stmt* stmt;
  std::vector<size_t> children;
};

using Use = std::pair<ENode, size_t>;

struct EClassID {
  size_t parent{static_cast<size_t>(-1)};
  std::vector<Use> uses;
};
}  // namespace rellic

template <>
struct ::std::hash<rellic::ENode> {
  constexpr std::size_t operator()(const rellic::ENode& n) const {
    auto h{std::hash<clang::Stmt*>()(n.stmt)};
    // for (auto i{0UL}; i < n.children.size(); ++i) {
    //  h ^= (std::hash<size_t>()(n.children[i])) << i;
    //}
    return h;
  }
};

template <>
struct ::std::equal_to<rellic::ENode> {
  constexpr bool operator()(const rellic::ENode& a,
                            const rellic::ENode& b) const {
    return a.stmt == b.stmt;

    if (a.stmt != b.stmt) {
      return false;
    }

    auto child_a{a.children.begin()};
    auto child_b{b.children.begin()};
    while (true) {
      bool a_end{child_a == a.children.end()};
      bool b_end{child_b == b.children.end()};
      if (a_end != b_end) {
        return false;
      } else if (a_end && b_end) {
        return true;
      } else if (*child_a != *child_b) {
        return false;
      }

      ++child_a;
      ++child_b;
    }
  }
};

namespace rellic {
struct StructHash {
  constexpr size_t operator()(const ENode& n) const {
    if (!n.stmt) {
      return 0;
    }
    return ::rellic::GetHash(n.stmt);
  }
};

struct StructEq {
  constexpr bool operator()(const ENode& a, const ENode& b) const {
    if ((!a.stmt || !b.stmt) && a.stmt != b.stmt) {
      return false;
    } else if (a.stmt && b.stmt) {
      return ::rellic::IsEquivalent(a.stmt, b.stmt);
    } else {
      return true;
    }
  }
};

class EGraph {
  constexpr static size_t invalid_idx{static_cast<size_t>(-1)};

  size_t version{0};

  std::vector<EClassID> classes;
  std::unordered_map<ENode, size_t, StructHash, StructEq> hashcons;
  std::vector<size_t> worklist;

  size_t NewSingletonEClass() {
    classes.emplace_back();
    return classes.size() - 1;
  }

  size_t FindCanonical(size_t id) {
    CHECK_NE(id, invalid_idx);
    auto& eclass{classes[id]};
    if (eclass.parent == invalid_idx) {
      return id;
    }
    eclass.parent = FindCanonical(eclass.parent);
    return eclass.parent;
  }

  ENode Canonicalize(const ENode& enode) {
    ENode res;
    res.stmt = enode.stmt;
    for (auto& child : enode.children) {
      res.children.push_back(FindCanonical(child));
    }
    return res;
  }

  size_t AddENode(const ENode& enode) {
    auto can_enode{Canonicalize(enode)};
    auto& eclass{hashcons[can_enode]};
    if (!eclass) {
      ++version;
      eclass = NewSingletonEClass();
      for (auto arg : can_enode.children) {
        classes[arg].uses.push_back({can_enode, eclass});
      }
      hashcons[can_enode] = eclass;
    }

    return FindCanonical(eclass);
  }

  void Repair(size_t id) {
    // CHECK_EQ(id->parent, nullptr);
    auto uses = classes[id].uses;
    classes[id].uses.clear();
    for (auto [node, eclass] : uses) {
      auto hash = hashcons.find(node);
      if (hash != hashcons.end()) {
        hashcons.erase(hash);
      }
      node = Canonicalize(node);
      hashcons[node] = FindCanonical(eclass);
    }

    std::unordered_map<ENode, size_t, StructHash, StructEq> new_uses;
    for (auto [node, eclass] : uses) {
      node = Canonicalize(node);
      if (new_uses.find(node) != new_uses.end()) {
        Merge(eclass, new_uses[node]);
      }
      new_uses[node] = FindCanonical(eclass);
    }
    auto eclass{classes[FindCanonical(id)]};
    for (auto use : new_uses) {
      classes[FindCanonical(id)].uses.push_back(use);
    }
  }

  clang::Stmt* Extract(
      size_t id, std::unordered_map<size_t, std::pair<float, ENode>> costs) {
    auto node{costs[id].second};
    if (node.stmt) {
      auto args{node.children.begin()};
      for (auto& child : node.stmt->children()) {
        child = Extract(*args, costs);
        ++args;
      }
    }
    return node.stmt;
  };

 public:
  size_t GetVersion() { return version; }
  using EqSet = std::unordered_set<ENode, StructHash, StructEq>;

  std::unordered_map<size_t, EqSet> GetClasses() {
    std::unordered_map<size_t, EqSet> res;
    for (auto [node, eid] : hashcons) {
      eid = FindCanonical(eid);
      res[eid].insert(node);
    }
    return res;
  }

  size_t AddNode(clang::Stmt* node) {
    ENode enode;
    enode.stmt = node;
    if (node) {
      for (auto child : node->children()) {
        enode.children.push_back(AddNode(child));
      }
    }
    return AddENode(enode);
  }

  void Merge(size_t a, size_t b) {
    a = FindCanonical(a);
    b = FindCanonical(b);
    auto& class_a{classes[a]};
    auto& class_b{classes[b]};
    if (a == b) {
      return;
    }
    ++version;
    class_a.parent = b;
    std::copy(class_a.uses.begin(), class_a.uses.end(),
              std::back_inserter(class_b.uses));
    class_a.uses.clear();
    worklist.push_back(b);
  }

  void Rebuild() {
    while (!worklist.empty()) {
      std::unordered_set<size_t> todo{worklist.begin(), worklist.end()};
      worklist.clear();
      for (auto id : todo) {
        Repair(id);
      }
    }
  }

  clang::Stmt* Schedule(size_t result) {
    result = FindCanonical(result);
    auto changed{true};
    auto eclasses{GetClasses()};
    std::unordered_map<size_t, std::pair<float, ENode>> costs;
    auto inf{std::numeric_limits<float>::infinity()};
    for (auto id : eclasses) {
      costs[id.first] = {inf, {}};
    }

    auto NodeCost = [&](ENode node) {
      if (!node.stmt) {
        return 0.f;
      }
      auto cost{1.f};
      for (auto& child : node.children) {
        cost += costs[child].first;
      }
      return cost;
    };

    while (changed) {
      changed = false;
      for (auto [eclass, enodes] : eclasses) {
        auto& cost = costs[eclass];
        for (auto enode : enodes) {
          auto new_cost{NodeCost(enode)};
          if (new_cost < cost.first) {
            cost = {new_cost, enode};
            changed = true;
          }
        }
      }
    }

    return Extract(result, costs);
  }

  void PrintGraph(llvm::raw_ostream& os) {
    static int numgraph = 0;
    os << "digraph G" << numgraph++
       << " {\ngraph [rankdir = LR];\nnode [shape=\"record\" "
          "fontname=monospace];\n";
    auto classes{GetClasses()};
    for (auto& [class_id, nodes] : classes) {
      os << "e" << class_id << " [label=\"e" << class_id
         << "\" shape=\"circle\"];\n";

      for (auto& node : nodes) {
        auto node_id{std::hash<ENode>()(node)};
        os << "e" << class_id << " -> n" << node_id << ";\n";

        for (size_t i = 0; i < node.children.size(); i++) {
          os << "n" << node_id << ":c" << i << " -> e" << node.children[i]
             << ";\n";
        }

        os << "n" << node_id << " [label=\"";
        std::string tmp;
        if (node.stmt) {
          llvm::raw_string_ostream tos(tmp);
          node.stmt->printPretty(tos, nullptr,
                                 clang::PrintingPolicy(clang::LangOptions()));
        } else {
          tmp = "<NULL>";
        }
        for (auto c : tmp) {
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

        for (size_t i = 0; i < node.children.size(); i++) {
          os << "|<c" << i << ">";
        }
        os << "\"];\n";
      }
    }
    os << "}\n\n\n";
  }
};

}  // namespace rellic