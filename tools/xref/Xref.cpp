/*
 * Copyright (c) 2022-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include <clang/Frontend/ASTUnit.h>
#include <clang/Tooling/Tooling.h>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <httplib.h>
#include <llvm/IR/AssemblyAnnotationWriter.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/PassManager.h>
#include <llvm/InitializePasses.h>
#include <llvm/Pass.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/FormattedStream.h>
#include <llvm/Support/JSON.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/Utils.h>
#include <llvm/Transforms/Utils/Local.h>
#include <llvm/Transforms/Utils/LowerSwitch.h>

#include <chrono>
#include <iostream>
#include <mutex>
#include <random>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <utility>

#include "Printer.h"
#include "rellic/AST/ASTPass.h"
#include "rellic/AST/CondBasedRefine.h"
#include "rellic/AST/DeadStmtElim.h"
#include "rellic/AST/DebugInfoCollector.h"
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
#include "rellic/Decompiler.h"
#include "rellic/Exception.h"
#include "rellic/Version.h"

#ifndef LLVM_VERSION_STRING
#define LLVM_VERSION_STRING LLVM_VERSION_MAJOR << "." << LLVM_VERSION_MINOR
#endif

DECLARE_bool(version);
DEFINE_string(address, "0.0.0.0", "Address on which the server will listen");
DEFINE_int32(port, 80, "Port on which the server will listen");
DEFINE_string(home, "./www", "");

using namespace std::chrono_literals;

static void SetVersion(void) {
  std::stringstream version;

  auto vs = rellic::Version::GetVersionString();
  if (0 == vs.size()) {
    vs = "unknown";
  }
  version << vs << "\n";
  if (!rellic::Version::HasVersionData()) {
    version << "No extended version information found!\n";
  } else {
    version << "Commit Hash: " << rellic::Version::GetCommitHash() << "\n";
    version << "Commit Date: " << rellic::Version::GetCommitDate() << "\n";
    version << "Last commit by: " << rellic::Version::GetAuthorName() << " ["
            << rellic::Version::GetAuthorEmail() << "]\n";
    version << "Commit Subject: [" << rellic::Version::GetCommitSubject()
            << "]\n";
    version << "\n";
    if (rellic::Version::HasUncommittedChanges()) {
      version << "Uncommitted changes were present during build.\n";
    } else {
      version << "All changes were committed prior to building.\n";
    }
  }
  version << "Using LLVM " << LLVM_VERSION_STRING << std::endl;

  google::SetVersionString(version.str());
}

struct Session {
  size_t Id;
  std::chrono::time_point<std::chrono::system_clock> LastAccess;
  std::unique_ptr<llvm::LLVMContext> Context;
  std::unique_ptr<llvm::Module> Module;
  std::unique_ptr<clang::ASTUnit> Unit;
  std::unique_ptr<rellic::ASTPass> Pass;
  rellic::StmtToIRMap Provenance;
  rellic::IRToTypeDeclMap TypeDecls;
  rellic::IRToValDeclMap ValueDecls;
  rellic::IRToStmtMap Stmts;
  rellic::ArgToTempMap TempDecls;
  // Must always be acquired in this order and released all at once
  std::shared_mutex LoadMutex, MutationMutex;
};

static httplib::Server svr;
static std::unordered_map<size_t, Session> sessions;
static std::mutex sessions_mutex;

static std::vector<std::string> Split(const std::string& s,
                                      const std::string& delim) {
  std::vector<std::string> res;
  size_t start{0};
  size_t end{s.find(delim)};
  while (end != std::string::npos) {
    res.push_back(s.substr(start, end - start));
    start = end + delim.size();
    end = s.find(delim, start);
  }
  res.push_back(s.substr(start, end - start));
  return res;
}

using write_lock = std::unique_lock<std::shared_mutex>;
using read_lock = std::shared_lock<std::shared_mutex>;

static std::unordered_map<std::string, std::string> GetCookies(
    const httplib::Request& req) {
  std::unordered_map<std::string, std::string> res;
  auto range{req.headers.equal_range("Cookie")};
  for (auto it{range.first}; it != range.second && it != req.headers.end();
       ++it) {
    auto cookies{Split(it->second, "; ")};
    for (auto cookie : cookies) {
      auto pair{Split(cookie, "=")};
      if (pair.size() == 2) {
        res[pair[0]] = pair[1];
      }
    }
  }

  return res;
}

static Session& GetSession(const httplib::Request& req) {
  std::unique_lock<std::mutex> lock(sessions_mutex);
  auto now{std::chrono::system_clock::now()};

  auto cookies{GetCookies(req)};
  auto sessionId{cookies.find("sessionId")};
  if (sessionId != cookies.end()) {
    auto id{std::stoull(sessionId->second)};
    auto kvp{sessions.find(id)};
    auto& session{sessions[id]};
    session.LastAccess = now;
    if (kvp == sessions.end()) {
      session.Id = id;
      session.Context = std::make_unique<llvm::LLVMContext>();
    }
    return session;
  }

  std::random_device dev;
  std::uniform_int_distribution<std::size_t> dist;
  size_t id{dist(dev)};
  auto& session{sessions[id]};
  session.Id = id;
  session.LastAccess = now;
  session.Context = std::make_unique<llvm::LLVMContext>();

  for (auto it{sessions.begin()}; it != sessions.end(); ++it) {
    if (now - it->second.LastAccess > 30min) {
      sessions.erase(it);
    }
  }

  return session;
}

static void SendJSON(httplib::Response& res, llvm::json::Object& obj) {
  std::string s;
  llvm::raw_string_ostream os(s);
  os << llvm::json::Value(std::move(obj));
  res.set_content(s, "application/json");
}

static httplib::Server::HandlerResponse PreRoutingHandler(
    const httplib::Request& req, httplib::Response& res) {
  auto& session{GetSession(req)};
  std::string header{"sessionId="};
  header += std::to_string(session.Id);
  res.set_header("Set-Cookie", header.c_str());

  return httplib::Server::HandlerResponse::Unhandled;
}

static void LoadModule(const httplib::Request& req, httplib::Response& res) {
  auto& session{GetSession(req)};
  write_lock lock(session.LoadMutex, std::try_to_lock);
  if (!lock.owns_lock()) {
    llvm::json::Object msg{
        {"message",
         "Cannot load a new module while other operations are in progress."}};
    res.status = 409;
    SendJSON(res, msg);
    return;
  }

  auto mod{rellic::LoadModuleFromMemory(session.Context.get(), req.body, true)};
  if (!mod) {
    llvm::json::Object msg{{"message", "Couldn't load LLVM module."}};
    res.status = 400;
    SendJSON(res, msg);
    return;
  }
  session.Module = std::unique_ptr<llvm::Module>(mod);
  llvm::json::Object msg{{"message", "Ok."}};
  SendJSON(res, msg);
  res.status = 200;
}

static void Decompile(const httplib::Request& req, httplib::Response& res) {
  auto& session{GetSession(req)};
  read_lock load_mutex(session.LoadMutex);
  write_lock mutation_mutex(session.MutationMutex);

  if (!session.Module) {
    llvm::json::Object msg{{"message", "No module loaded."}};
    res.status = 400;
    SendJSON(res, msg);
    return;
  }

  try {
    session.Provenance.clear();
    session.TypeDecls.clear();
    session.ValueDecls.clear();
    session.Stmts.clear();
    session.TempDecls.clear();
    std::vector<std::string> args{"-Wno-pointer-to-int-cast", "-target",
                                  session.Module->getTargetTriple()};
    session.Unit = clang::tooling::buildASTFromCodeWithArgs("", args, "out.c");
    rellic::DebugInfoCollector dic;
    dic.visit(*session.Module);
    rellic::GenerateAST::run(*session.Module, session.Provenance, *session.Unit,
                             session.TypeDecls, session.ValueDecls,
                             session.Stmts, session.TempDecls);
    rellic::LocalDeclRenamer ldr{session.Provenance, *session.Unit,
                                 dic.GetIRToNameMap(), session.ValueDecls};
    rellic::StructFieldRenamer sfr{session.Provenance, *session.Unit,
                                   dic.GetIRTypeToDITypeMap(),
                                   session.TypeDecls};
    ldr.Run();
    sfr.Run();

    llvm::json::Object msg{{"message", "Ok."}};
    SendJSON(res, msg);
    res.status = 200;
  } catch (rellic::Exception& e) {
    llvm::json::Object msg{{"message", e.what()}};
    SendJSON(res, msg);
    res.status = 400;
    session.Unit = nullptr;
  }
}

static void RemovePhi(const httplib::Request& req, httplib::Response& res) {
  auto& session{GetSession(req)};
  read_lock load_mutex(session.LoadMutex);
  write_lock mutation_mutex(session.MutationMutex);

  if (!session.Module) {
    llvm::json::Object msg{{"message", "No module loaded."}};
    res.status = 400;
    SendJSON(res, msg);
    return;
  }

  rellic::RemovePHINodes(*session.Module);

  llvm::json::Object msg{{"message", "Ok."}};
  SendJSON(res, msg);
  res.status = 200;
}

static void LowerSwitches(const httplib::Request& req, httplib::Response& res) {
  auto& session{GetSession(req)};
  read_lock load_mutex(session.LoadMutex);
  write_lock mutation_mutex(session.MutationMutex);

  if (!session.Module) {
    llvm::json::Object msg{{"message", "No module loaded."}};
    res.status = 400;
    SendJSON(res, msg);
    return;
  }

  rellic::LowerSwitches(*session.Module);

  llvm::json::Object msg{{"message", "Ok."}};
  SendJSON(res, msg);
  res.status = 200;
}

static void RemoveInsertValue(const httplib::Request& req,
                              httplib::Response& res) {
  auto& session{GetSession(req)};
  read_lock load_mutex(session.LoadMutex);
  write_lock mutation_mutex(session.MutationMutex);

  if (!session.Module) {
    llvm::json::Object msg{{"message", "No module loaded."}};
    res.status = 400;
    SendJSON(res, msg);
    return;
  }

  rellic::RemoveInsertValues(*session.Module);

  llvm::json::Object msg{{"message", "Ok."}};
  SendJSON(res, msg);
}

static void RemoveAggregateStores(const httplib::Request& req,
                                  httplib::Response& res) {
  auto& session{GetSession(req)};
  read_lock load_mutex(session.LoadMutex);
  write_lock mutation_mutex(session.MutationMutex);

  if (!session.Module) {
    llvm::json::Object msg{{"message", "No module loaded."}};
    res.status = 400;
    SendJSON(res, msg);
    return;
  }

  rellic::ConvertArrayStores(*session.Module);

  llvm::json::Object msg{{"message", "Ok."}};
  SendJSON(res, msg);
}

static void RemoveArrayArguments(const httplib::Request& req,
                                 httplib::Response& res) {
  auto& session{GetSession(req)};
  read_lock load_mutex(session.LoadMutex);
  write_lock mutation_mutex(session.MutationMutex);

  if (!session.Module) {
    llvm::json::Object msg{{"message", "No module loaded."}};
    res.status = 400;
    SendJSON(res, msg);
    return;
  }

  rellic::ConvertArrayArguments(*session.Module);

  llvm::json::Object msg{{"message", "Ok."}};
  SendJSON(res, msg);
}

class FixpointPass : public rellic::ASTPass {
  rellic::CompositeASTPass comp;

 protected:
  void StopImpl() override { comp.Stop(); }

  void RunImpl() override { comp.Fixpoint(); }

 public:
  FixpointPass(rellic::StmtToIRMap& provenance, clang::ASTUnit& ast_unit)
      : ASTPass(provenance, ast_unit), comp(provenance, ast_unit) {}
  std::vector<std::unique_ptr<ASTPass>>& GetPasses() {
    return comp.GetPasses();
  }
};

static std::unique_ptr<rellic::ASTPass> CreatePass(
    Session& session, const llvm::json::Value& val) {
  if (auto obj = val.getAsObject()) {
    auto name{obj->getString("id")};
    if (!name) {
      LOG(ERROR) << "Request doesn't contain pass id";
      return nullptr;
    }
    auto str{name->str()};

    if (str == "cbr") {
      return std::make_unique<rellic::CondBasedRefine>(session.Provenance,
                                                       *session.Unit);
    } else if (str == "dse") {
      return std::make_unique<rellic::DeadStmtElim>(session.Provenance,
                                                    *session.Unit);
    } else if (str == "ec") {
      return std::make_unique<rellic::ExprCombine>(session.Provenance,
                                                   *session.Unit);
    } else if (str == "lr") {
      return std::make_unique<rellic::LoopRefine>(session.Provenance,
                                                  *session.Unit);
    } else if (str == "ncp") {
      return std::make_unique<rellic::NestedCondProp>(session.Provenance,
                                                      *session.Unit);
    } else if (str == "nsc") {
      return std::make_unique<rellic::NestedScopeCombine>(session.Provenance,
                                                          *session.Unit);
    } else if (str == "nc") {
      return std::make_unique<rellic::NormalizeCond>(session.Provenance,
                                                     *session.Unit);
    } else if (str == "rbr") {
      return std::make_unique<rellic::ReachBasedRefine>(session.Provenance,
                                                        *session.Unit);
    } else if (str == "zcs") {
      return std::make_unique<rellic::Z3CondSimplify>(session.Provenance,
                                                      *session.Unit);
    } else {
      LOG(ERROR) << "Request contains invalid pass id";
      return nullptr;
    }
  } else if (auto arr = val.getAsArray()) {
    auto fix{std::make_unique<FixpointPass>(session.Provenance, *session.Unit)};
    for (auto& pass : *arr) {
      auto p{CreatePass(session, pass)};
      if (!p) {
        return nullptr;
      }
      fix->GetPasses().push_back(std::move(p));
    }
    return fix;
  } else {
    std::string s;
    llvm::raw_string_ostream os(s);
    os << val;
    LOG(ERROR) << "Invalid request type: " << s;
    return nullptr;
  }
}

static void Stop(const httplib::Request& req, httplib::Response& res) {
  auto& session{GetSession(req)};
  read_lock load_mutex(session.LoadMutex);

  if (!session.Module) {
    llvm::json::Object msg{{"message", "No module loaded."}};
    res.status = 400;
    SendJSON(res, msg);
    return;
  }

  if (!session.Unit) {
    llvm::json::Object msg{{"message", "No AST available."}};
    res.status = 400;
    SendJSON(res, msg);
    return;
  }

  if (!session.Pass) {
    llvm::json::Object msg{{"message", "Nothing running."}};
    res.status = 400;
    SendJSON(res, msg);
    return;
  }

  llvm::json::Object msg{{"message", "Ok."}};
  SendJSON(res, msg);
  session.Pass->Stop();
}

static void Run(const httplib::Request& req, httplib::Response& res) {
  auto& session{GetSession(req)};
  read_lock load_mutex(session.LoadMutex);
  write_lock mutation_mutex(session.MutationMutex, std::try_to_lock);

  if (!mutation_mutex.owns_lock()) {
    llvm::json::Object msg{{"message", "Server busy."}};
    res.status = 400;
    SendJSON(res, msg);
    return;
  }

  if (!session.Module) {
    llvm::json::Object msg{{"message", "No module loaded."}};
    res.status = 400;
    SendJSON(res, msg);
    return;
  }

  if (!session.Unit) {
    llvm::json::Object msg{{"message", "No AST available."}};
    res.status = 400;
    SendJSON(res, msg);
    return;
  }

  auto json{llvm::json::parse(req.body)};
  if (!json) {
    llvm::json::Object msg{{"message", "Invalid request: cannot parse."}};
    SendJSON(res, msg);
    res.status = 400;
    return;
  }

  auto composite{std::make_unique<rellic::CompositeASTPass>(session.Provenance,
                                                            *session.Unit)};
  for (auto& obj : *json->getAsArray()) {
    auto pass{CreatePass(session, obj)};
    if (!pass) {
      llvm::json::Object msg{{"message", "Invalid request."}};
      SendJSON(res, msg);
      res.status = 400;
      return;
    }
    composite->GetPasses().push_back(std::move(pass));
  }

  session.Pass = std::move(composite);

  try {
    session.Pass->Run();

    if (session.Pass->Stopped()) {
      llvm::json::Object msg{{"message", "Stopped."}};
      SendJSON(res, msg);
    } else {
      llvm::json::Object msg{{"message", "Ok."}};
      SendJSON(res, msg);
    }
    res.status = 200;
    session.Pass = nullptr;
  } catch (rellic::Exception& e) {
    llvm::json::Object msg{{"message", e.what()}};
    SendJSON(res, msg);
    res.status = 400;
    session.Pass = nullptr;
  }
}

static void Fixpoint(const httplib::Request& req, httplib::Response& res) {
  auto& session{GetSession(req)};
  read_lock load_mutex(session.LoadMutex);
  write_lock mutation_mutex(session.MutationMutex, std::try_to_lock);

  if (!mutation_mutex.owns_lock()) {
    llvm::json::Object msg{
        {"message", "Cannot execute while other operations are in progress"}};
    res.status = 400;
    SendJSON(res, msg);
    return;
  }

  if (!session.Module) {
    llvm::json::Object msg{{"message", "No module loaded"}};
    res.status = 400;
    SendJSON(res, msg);
    return;
  }

  if (!session.Unit) {
    llvm::json::Object msg{{"message", "No AST available"}};
    res.status = 400;
    SendJSON(res, msg);
    return;
  }

  auto json{llvm::json::parse(req.body)};
  if (!json) {
    llvm::json::Object msg{{"message", "Invalid request: cannot parse."}};
    SendJSON(res, msg);
    res.status = 400;
    return;
  }

  auto composite{std::make_unique<rellic::CompositeASTPass>(session.Provenance,
                                                            *session.Unit)};
  for (auto& obj : *json->getAsArray()) {
    auto pass{CreatePass(session, obj)};
    if (!pass) {
      llvm::json::Object msg{{"message", "Invalid request"}};
      SendJSON(res, msg);
      res.status = 400;
      return;
    }
    composite->GetPasses().push_back(std::move(pass));
  }

  session.Pass = std::move(composite);

  try {
    auto t1{std::chrono::system_clock::now()};
    auto num_iterations{session.Pass->Fixpoint()};
    auto t2{std::chrono::system_clock::now()};
    auto elapsed{
        std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count()};

    if (session.Pass->Stopped()) {
      llvm::json::Object msg{{"message", "Stopped."}};
      SendJSON(res, msg);
    } else {
      std::string s;
      llvm::raw_string_ostream os(s);
      os << "Fixpoint found after " << num_iterations << " iterations ("
         << elapsed << " ms).";
      llvm::json::Object msg{{"message", s}};
      SendJSON(res, msg);
    }
    res.status = 200;
    session.Pass = nullptr;
  } catch (rellic::Exception& e) {
    llvm::json::Object msg{{"message", e.what()}};
    SendJSON(res, msg);
    res.status = 400;
    session.Pass = nullptr;
  }
}

template <typename TMap, typename TKey>
static void PrintProvenances(llvm::raw_ostream& OS, const TKey* key,
                             const TMap& provenances) {
  std::vector<unsigned long long> provs{};
  auto range{provenances.equal_range((TKey*)key)};
  for (auto it{range.first}; it != range.second && it != provenances.end();
       it++) {
    provs.emplace_back((unsigned long long)it->second);
  }
  OS << " data-addr=\"";
  OS.write_hex((unsigned long long)key);
  OS << "\" data-provenance=\"";
  if (provs.size() > 0) {
    for (auto i{0U}; i < provs.size() - 1; ++i) {
      OS.write_hex(provs[i]);
      OS << ',';
    }
    OS.write_hex(provs.back());
  }
  OS << '"';
}

class AAW : public llvm::AssemblyAnnotationWriter {
  const Session& session;

 public:
  AAW(const Session& session) : session(session) {}

  void emitFunctionAnnot(const llvm::Function* F,
                         llvm::formatted_raw_ostream& OS) override {
    OS << "</span><span class=\"llvm\"";
    PrintProvenances(OS, F, session.ValueDecls);
    OS << '>';
  }
  void emitInstructionAnnot(const llvm::Instruction* I,
                            llvm::formatted_raw_ostream& OS) override {
    OS << "</span><span class=\"llvm\"";
    PrintProvenances(OS, I, session.Stmts);
    OS << '>';
  }
  void emitBasicBlockStartAnnot(const llvm::BasicBlock*,
                                llvm::formatted_raw_ostream& OS) override {
    OS << "</span><span>";
  }
  void emitBasicBlockEndAnnot(const llvm::BasicBlock*,
                              llvm::formatted_raw_ostream& OS) override {
    OS << "</span><span>";
  }
  void printInfoComment(const llvm::Value&,
                        llvm::formatted_raw_ostream& OS) override {
    OS << "</span><span>";
  }
};

static void PrintModule(const httplib::Request& req, httplib::Response& res) {
  auto& session{GetSession(req)};
  read_lock load_mutex(session.LoadMutex);
  read_lock mutation_mutex(session.MutationMutex);
  if (!session.Module) {
    llvm::json::Object msg{{"message", "No module loaded."}};
    res.status = 400;
    SendJSON(res, msg);
    return;
  }

  std::string s;
  llvm::raw_string_ostream os(s);
  AAW aaw(session);
  os << "<pre><span>";
  session.Module->print(os, &aaw);
  os << "</span></pre>";
  res.status = 200;
  res.set_content(s, "text/html");
}

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

static void PrintAST(const httplib::Request& req, httplib::Response& res) {
  auto& session{GetSession(req)};
  read_lock load_mutex(session.LoadMutex);
  read_lock mutation_mutex(session.MutationMutex);
  if (!session.Module) {
    llvm::json::Object msg{{"message", "No module loaded."}};
    res.status = 400;
    SendJSON(res, msg);
    return;
  }

  if (!session.Unit) {
    llvm::json::Object msg{{"message", "No AST available."}};
    res.status = 400;
    SendJSON(res, msg);
    return;
  }

  rellic::DecompilationResult::StmtToIRMap stmt_provenance_map;
  rellic::DecompilationResult::IRToStmtMap value_to_stmt_map;
  rellic::DecompilationResult::IRToDeclMap value_to_decl_map;
  rellic::DecompilationResult::DeclToIRMap decl_provenance_map;
  rellic::DecompilationResult::IRToTypeDeclMap type_to_decl_map;
  rellic::DecompilationResult::TypeDeclToIRMap type_provenance_map;

  CopyMap(session.Provenance, stmt_provenance_map, value_to_stmt_map);
  CopyMap(session.ValueDecls, value_to_decl_map, decl_provenance_map);
  CopyMap(session.TypeDecls, type_to_decl_map, type_provenance_map);

  std::string s;
  llvm::raw_string_ostream os(s);
  os << "<pre>";
  PrintDecl(session.Unit->getASTContext().getTranslationUnitDecl(),
            decl_provenance_map, stmt_provenance_map, type_provenance_map,
            session.Unit->getASTContext().getPrintingPolicy(), 0, os);
  os << "</pre>";
  res.status = 200;
  res.set_content(s, "text/html");
}

int main(int argc, char* argv[]) {
  std::stringstream usage;
  usage << std::endl
        << std::endl
        << "  " << argv[0] << " \\" << std::endl
        << std::endl

        // Print the version and exit.
        << "    [--version]" << std::endl
        << std::endl;

  google::InitGoogleLogging(argv[0]);
  google::InstallFailureSignalHandler();
  google::SetUsageMessage(usage.str());
  SetVersion();
  google::ParseCommandLineFlags(&argc, &argv, true);

  svr.set_logger([](const httplib::Request& req, const httplib::Response&) {
    LOG(INFO) << req.method << " " << req.path;
  });
  svr.set_mount_point("/", FLAGS_home);
  svr.set_pre_routing_handler(PreRoutingHandler);
  svr.Post("/action/module", LoadModule);
  svr.Post("/action/decompile", Decompile);
  svr.Post("/action/remove-phi-nodes", RemovePhi);
  svr.Post("/action/lower-switches", LowerSwitches);
  svr.Post("/action/remove-array-arguments", RemoveArrayArguments);
  svr.Post("/action/remove-insertvalue", RemoveInsertValue);
  svr.Post("/action/remove-aggregate-stores", RemoveAggregateStores);
  svr.Post("/action/run", Run);
  svr.Post("/action/fixpoint", Fixpoint);
  svr.Post("/action/stop", Stop);

  svr.Get("/action/module", PrintModule);
  svr.Get("/action/ast", PrintAST);

  LOG(INFO) << "Listening";
  svr.listen(FLAGS_address.c_str(), FLAGS_port);

  google::ShutDownCommandLineFlags();
  google::ShutdownGoogleLogging();

  return EXIT_SUCCESS;
}
