add_llvm_pass_plugin(Obfuscation
  CryptoUtils.cpp
  Substitution.cpp
  BogusControlFlow.cpp
  Utils.cpp
  SplitBasicBlock.cpp
  Flattening.cpp
  Strings.cpp
  Plugin.cpp  
  
  ADDITIONAL_HEADER_DIRS
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Transforms
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Transforms/Obfuscation

  DEPENDS
  intrinsics_gen
  vt_gen
  )
