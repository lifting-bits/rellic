#include "llvm/IR/IntrinsicInst.h"
#include "rellic/BC/Version.h"

#if LLVM_VERSION_NUMBER < LLVM_VERSION(11, 0)
namespace llvm {
static inline bool isDbgInfoIntrinsic(Intrinsic::ID ID) {
  switch (ID) {
  case Intrinsic::dbg_declare:
  case Intrinsic::dbg_value:
  case Intrinsic::dbg_addr:
  case Intrinsic::dbg_label:
    return true;
  default:
    return false;
  }
}
} // namespace llvm
#endif