#ifndef LLVM_TRANSFORMS_OBFUSCATION_STRINGS_H
#define LLVM_TRANSFORMS_OBFUSCATION_STRINGS_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class StringObfuscationPass : public PassInfoMixin<StringObfuscationPass> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
  static bool isRequired() { return true; }
};

} // namespace llvm

#endif