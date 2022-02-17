/*
 * Copyright (c) 2022-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */
#include <clang/Basic/TargetInfo.h>
#include <clang/Frontend/ASTUnit.h>
#include <clang/Tooling/Tooling.h>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <linenoise.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/PassManager.h>
#include <llvm/InitializePasses.h>
#include <llvm/Pass.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/Utils.h>
#include <llvm/Transforms/Utils/Local.h>
#include <llvm/Transforms/Utils/LowerSwitch.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <csignal>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <sstream>
#include <system_error>

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

llvm::LLVMContext llvm_ctx;
std::unique_ptr<llvm::Module> module{nullptr};
std::unique_ptr<clang::ASTUnit> ast_unit{nullptr};
rellic::StmtToIRMap provenance;
rellic::IRToTypeDeclMap type_decls;
rellic::IRToValDeclMap value_decls;
rellic::IRToStmtMap stmts;
rellic::ArgToTempMap temp_decls;
std::unique_ptr<rellic::CompositeASTPass> global_pass{nullptr};

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

static const char* available_passes[] = {"cbr", "dse", "ec",  "lr", "ncp",
                                         "nsc", "nc",  "rbr", "zcs"};

static bool diff = false;

template <typename... Ts>
static void invoke(Ts... args) {
  auto pid{fork()};
  if (pid == 0) {
    execl(args...);
  } else {
    int status;
    waitpid(pid, &status, 0);
  }
}

template <typename Print>
class Diff {
  std::string before_path;
  const Print& print;

 public:
  Diff(const Print& print) : print(print) {
    if (!diff) {
      return;
    }
    char before[] = "/tmp/rellic.before.XXXXXX";
    int before_fd{mkstemp(before)};
    llvm::raw_fd_ostream os{before_fd, true};
    print(os);
    before_path = before;
  }

  ~Diff() {
    if (!diff) {
      return;
    }

    char after[] = "/tmp/rellic.after.XXXXXX";
    {
      int after_fd{mkstemp(after)};
      llvm::raw_fd_ostream os{after_fd, true};
      print(os);
    }

    invoke("/usr/bin/env", "env", "diff", "--color", "-u", before_path.c_str(),
           after, nullptr);
    unlink(before_path.c_str());
    unlink(after);
  }
};

static std::unique_ptr<rellic::ASTPass> CreatePass(const std::string& name) {
  if (name == "cbr") {
    return std::make_unique<rellic::CondBasedRefine>(provenance, *ast_unit);
  } else if (name == "dse") {
    return std::make_unique<rellic::DeadStmtElim>(provenance, *ast_unit);
  } else if (name == "ec") {
    return std::make_unique<rellic::ExprCombine>(provenance, *ast_unit);
  } else if (name == "lr") {
    return std::make_unique<rellic::LoopRefine>(provenance, *ast_unit);
  } else if (name == "ncp") {
    return std::make_unique<rellic::NestedCondProp>(provenance, *ast_unit);
  } else if (name == "nsc") {
    return std::make_unique<rellic::NestedScopeCombine>(provenance, *ast_unit);
  } else if (name == "nc") {
    return std::make_unique<rellic::NormalizeCond>(provenance, *ast_unit);
  } else if (name == "rbr") {
    return std::make_unique<rellic::ReachBasedRefine>(provenance, *ast_unit);
  } else if (name == "zcs") {
    return std::make_unique<rellic::Z3CondSimplify>(provenance, *ast_unit);
  } else {
    return nullptr;
  }
}

static void handle_stop(int signal) {
  if (global_pass) {
    global_pass->Stop();
  }
}

static void do_help() {
  std::cout << "available commands:\n"
            << "  quit               Exits the REPL\n"
            << "  load [path]        Loads an LLVM module\n"
            << "  print module       Prints the LLVM module\n"
            << "  print ast          Prints the C AST\n"
            << "  diff [on/off]      Enables/disables printing the diff "
               "between before and after executing a command\n"
            << "  clear              Clears the screen\n"
            << "  apply [pass]       Applies preprocessing pass\n"
            << "  decompile          Performs initial decompilation\n"
            << "  run [passes]       Applies a sequence of refinement passes\n"
            << "  fixpoint [passes]  Tries to find a fixpoint for a sequence "
               "of refinement passes\n\n"
            << "available preprocessing passes:\n"
            << "  remove-phi-nodes   Replaces Phi nodes with allocas\n"
            << "  lower-switches     Lowers switch instructions\n\n"
            << "available refinement passes:\n"
            << "  cbr                Condition-based refinement\n"
            << "  dse                Dead statement elimination\n"
            << "  ec                 Expression combination\n"
            << "  lr                 Loop refinement\n"
            << "  nc                 Condition normalization\n"
            << "  ncp                Nested condition propagation\n"
            << "  nsc                Nested scope combination\n"
            << "  rbr                Reach-based refinement\n"
            << "  zcs                Z3-based condition simplification\n"
            << std::endl;
}

static void do_load(std::istream& is) {
  std::string file;
  is >> file;
  module = std::unique_ptr<llvm::Module>(
      rellic::LoadModuleFromFile(&llvm_ctx, file, true));
  if (!module) {
    std::cout << "error: cannot load module `" << file << "'." << std::endl;
    return;
  }

  std::vector<std::string> args{"-Wno-pointer-to-int-cast", "-target",
                                module->getTargetTriple()};
  ast_unit = clang::tooling::buildASTFromCodeWithArgs("", args, "out.c");
  global_pass =
      std::make_unique<rellic::CompositeASTPass>(provenance, *ast_unit);
  provenance.clear();

  std::cout << "ok." << std::endl;
}

static void do_print(std::istream& is) {
  if (module == nullptr) {
    std::cout << "error: no module loaded." << std::endl;
    return;
  }

  std::string what;
  is >> what;
  std::string res;
  llvm::raw_string_ostream os(res);
  if (what == "module") {
    module->print(os, nullptr);
    std::cout << res << std::endl;
  } else if (what == "ast") {
    ast_unit->getASTContext().getTranslationUnitDecl()->print(os, 0, false);
    std::cout << res << std::endl;
  } else {
    std::cout << "I'm afraid I can't print that." << std::endl;
  }
}

static void do_apply(std::istream& is) {
  std::string what;
  is >> what;
  if (what == "remove-phi-nodes") {
    Diff d{[](llvm::raw_ostream& os) { module->print(os, nullptr); }};
    std::vector<llvm::PHINode*> work_list;
    for (auto& func : *module) {
      for (auto& inst : llvm::instructions(func)) {
        if (auto phi = llvm::dyn_cast<llvm::PHINode>(&inst)) {
          work_list.push_back(phi);
        }
      }
    }
    for (auto phi : work_list) {
      llvm::SmallVector<std::pair<unsigned, llvm::MDNode*>, 16u> mds;
      phi->getAllMetadataOtherThanDebugLoc(mds);
      llvm::DemotePHIToStack(phi);
    }
    std::cout << "ok." << std::endl;
  } else if (what == "lower-switches") {
    Diff d{[](llvm::raw_ostream& os) { module->print(os, nullptr); }};
    llvm::PassBuilder pb;
    llvm::ModulePassManager mpm;
    llvm::ModuleAnalysisManager mam;
    llvm::LoopAnalysisManager lam;
    llvm::CGSCCAnalysisManager cam;
    llvm::FunctionAnalysisManager fam;

    pb.registerFunctionAnalyses(fam);
    pb.registerModuleAnalyses(mam);
    pb.registerCGSCCAnalyses(cam);
    pb.registerLoopAnalyses(lam);

    pb.crossRegisterProxies(lam, fam, cam, mam);

    llvm::FunctionPassManager fpm;
    fpm.addPass(llvm::LowerSwitchPass());

    mpm.addPass(llvm::createModuleToFunctionPassAdaptor(std::move(fpm)));
    mpm.run(*module, mam);

    mam.clear();
    fam.clear();
    cam.clear();
    lam.clear();
    std::cout << "ok." << std::endl;
  } else {
    std::cout << "error: unknown preprocess pass `" << what << "'."
              << std::endl;
  }
}

static void do_decompile() {
  try {
    rellic::DebugInfoCollector dic;
    dic.visit(*module);
    rellic::GenerateAST::run(*module, provenance, *ast_unit, type_decls,
                             value_decls, stmts, temp_decls);
    rellic::LocalDeclRenamer ldr{provenance, *ast_unit, dic.GetIRToNameMap(),
                                 value_decls};
    rellic::StructFieldRenamer sfr{provenance, *ast_unit,
                                   dic.GetIRTypeToDITypeMap(), type_decls};
    ldr.Run();
    sfr.Run();
    std::cout << "ok." << std::endl;
  } catch (rellic::Exception& ex) {
    std::cout << "error: " << ex.what() << std::endl;
  }
}

static void do_run(std::istream& is) {
  if (module == nullptr) {
    std::cout << "error: no module loaded." << std::endl;
    return;
  }

  rellic::CompositeASTPass comp(provenance, *ast_unit);
  std::string name;
  while (is >> name) {
    auto pass{CreatePass(name)};
    if (pass) {
      comp.GetPasses().push_back(std::move(pass));
    } else {
      std::cout << "error: unknown pass `" << name << "'." << std::endl;
      return;
    }
  }

  Diff d{[](llvm::raw_ostream& os) {
    ast_unit->getASTContext().getTranslationUnitDecl()->print(os, 0, false);
  }};
  try {
    if (comp.Run()) {
      std::cout << "ok: the passes reported changes." << std::endl;
    } else {
      std::cout << "ok: the passes did not report any change." << std::endl;
    }
  } catch (rellic::Exception& ex) {
    std::cout << "error: " << ex.what() << std::endl;
  }
}

static void do_fixpoint(std::istream& is) {
  if (module == nullptr) {
    std::cout << "error: no module loaded." << std::endl;
    return;
  }

  rellic::CompositeASTPass comp(provenance, *ast_unit);
  std::string name;
  while (is >> name) {
    auto pass{CreatePass(name)};
    if (pass) {
      comp.GetPasses().push_back(std::move(pass));
    } else {
      std::cout << "error: unknown pass `" << name << "'." << std::endl;
      return;
    }
  }

  Diff d{[](llvm::raw_ostream& os) {
    ast_unit->getASTContext().getTranslationUnitDecl()->print(os, 0, false);
  }};
  unsigned iter_count{0};
  std::cout << "computing fixpoint... press Ctrl-Z to stop " << std::flush;
  try {
    while (comp.Run()) {
      std::cout << '.' << std::flush;
      ++iter_count;
    }
    if (comp.Stopped()) {
      std::cout << "\nstopped.";
    } else {
      std::cout << "\nreached in " << iter_count << " iterations.";
    }
    std::cout << std::endl;
  } catch (rellic::Exception& ex) {
    std::cout << "error: " << ex.what() << std::endl;
  }
}

static void do_diff(std::istream& is) {
  std::string value;
  is >> value;
  if (value == "on") {
    diff = true;
  } else if (value == "off") {
    diff = false;
  } else {
    std::cout << "unknown diff value `" << value << "'." << std::endl;
    return;
  }
  std::cout << "ok." << std::endl;
}

void completion(const char* buf, linenoiseCompletions* lc) {
  std::string line{buf};
  auto last_space{line.find_last_of(' ')};
  auto last_fragment{line.substr(last_space + 1)};
  if (std::string("help").find(line) != std::string::npos) {
    linenoiseAddCompletion(lc, "help");
  } else if (buf[0] == 'p') {
    if (line.find("print ") == 0) {
      for (auto option : {"module", "ast"}) {
        std::string option_str{option};
        if (option_str.find(last_fragment) == 0) {
          linenoiseAddCompletion(
              lc, (line.substr(0, last_space + 1) + option_str).c_str());
        }
      }
    } else {
      linenoiseAddCompletion(lc, "print");
    }
  } else if (buf[0] == 'a') {
    if (line.find("apply ") == 0) {
      for (auto option : {"remove-phi-nodes", "lower-switches"}) {
        std::string option_str{option};
        if (option_str.find(last_fragment) == 0) {
          linenoiseAddCompletion(
              lc, (line.substr(0, last_space + 1) + option_str).c_str());
        }
      }
    } else {
      linenoiseAddCompletion(lc, "apply");
    }
  } else if (buf[0] == 'd') {
    if (line.find("diff ") == 0) {
      for (auto option : {"on", "off"}) {
        std::string option_str{option};
        if (option_str.find(last_fragment) == 0) {
          linenoiseAddCompletion(
              lc, (line.substr(0, last_space + 1) + option_str).c_str());
        }
      }
    } else {
      linenoiseAddCompletion(lc, "decompile");
      linenoiseAddCompletion(lc, "diff");
    }
  } else if (buf[0] == 'q') {
    linenoiseAddCompletion(lc, "quit");
  } else if (buf[0] == 'l') {
    linenoiseAddCompletion(lc, "load");
  } else if (buf[0] == 'c') {
    linenoiseAddCompletion(lc, "clear");
  } else if (buf[0] == 'r') {
    if (line.find("run ") == 0) {
      for (auto pass : available_passes) {
        std::string pass_str{pass};
        if (pass_str.find(last_fragment) == 0) {
          linenoiseAddCompletion(
              lc, (line.substr(0, last_space + 1) + pass_str).c_str());
        }
      }
    } else {
      linenoiseAddCompletion(lc, "run");
    }
  } else if (buf[0] == 'f') {
    if (line.find("fixpoint ") == 0) {
      for (auto pass : available_passes) {
        std::string pass_str{pass};
        if (pass_str.find(last_fragment) == 0) {
          linenoiseAddCompletion(
              lc, (line.substr(0, last_space + 1) + pass_str).c_str());
        }
      }
    } else {
      linenoiseAddCompletion(lc, "fixpoint");
    }
  }
}

int main(int argc, char* argv[]) {
  struct sigaction sa;

  sa.sa_handler = handle_stop;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART;
  PCHECK(sigaction(SIGTSTP, &sa, NULL) != -1)
      << "Signal handler registration failed";

  std::stringstream usage;
  usage << std::endl << "  " << argv[0] << " [--version]" << std::endl;

  google::InitGoogleLogging(argv[0]);
  google::InstallFailureSignalHandler();
  google::SetUsageMessage(usage.str());
  SetVersion();
  google::ParseCommandLineFlags(&argc, &argv, true);

  auto& pr{*llvm::PassRegistry::getPassRegistry()};
  initializeCore(pr);
  initializeAnalysis(pr);

  linenoiseSetCompletionCallback(completion);
  while (auto input = linenoise("rellic> ")) {
    std::string line{input};
    std::istringstream iss{line};
    std::string command;
    iss >> command;
    if (command == "help") {
      do_help();
    } else if (command == "load") {
      do_load(iss);
    } else if (command == "print") {
      do_print(iss);
    } else if (command == "diff") {
      do_diff(iss);
    } else if (command == "clear") {
      linenoiseClearScreen();
    } else if (command == "apply") {
      do_apply(iss);
    } else if (command == "decompile") {
      do_decompile();
    } else if (command == "run") {
      do_run(iss);
    } else if (command == "fixpoint") {
      do_fixpoint(iss);
    } else if (command == "quit") {
      std::cout << "goodbye." << std::endl;
      break;
    } else {
      std::cout << "error: unknown command `" << command << "'." << std::endl;
    }
    linenoiseHistoryAdd(input);
    linenoiseFree(input);
  }

  google::ShutDownCommandLineFlags();
  google::ShutdownGoogleLogging();

  return EXIT_SUCCESS;
}
