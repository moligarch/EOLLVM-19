#include "llvm/Transforms/Obfuscation/BogusControlFlow.h"
#include "llvm/Transforms/Obfuscation/Flattening.h"
#include "llvm/Transforms/Obfuscation/SplitBasicBlock.h"
#include "llvm/Transforms/Obfuscation/Strings.h"
#include "llvm/Transforms/Obfuscation/Substitution.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"

using namespace llvm;

llvm::PassPluginLibraryInfo getObfuscationPluginInfo() {
  return {
      LLVM_PLUGIN_API_VERSION, "Obfuscation", LLVM_VERSION_STRING,
      [](PassBuilder &PB) {
		  
		// 1. Register the pipeline callback
        // This runs automatically when the pipeline is built (e.g. -O2)
        PB.registerPipelineStartEPCallback(
			[](llvm::ModulePassManager &MPM, llvm::OptimizationLevel Level) {
		  MPM.addPass(StringObfuscationPass());	
          MPM.addPass(createModuleToFunctionPassAdaptor(SplitBasicBlockPass()));
          MPM.addPass(createModuleToFunctionPassAdaptor(BogusControlFlowPass()));
          MPM.addPass(createModuleToFunctionPassAdaptor(FlatteningPass()));
        });
		
        PB.registerOptimizerLastEPCallback(
		[](llvm::ModulePassManager &MPM, llvm::OptimizationLevel Level) {
          MPM.addPass(createModuleToFunctionPassAdaptor(SubstitutionPass()));
        });
      }};
}

#ifndef LLVM_OBFUSCATION_LINK_INTO_TOOLS
extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return getObfuscationPluginInfo();
}
#endif
