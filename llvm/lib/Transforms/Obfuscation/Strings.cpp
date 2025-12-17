#include "llvm/Transforms/Obfuscation/Strings.h"
#include "llvm/Transforms/Obfuscation/CryptoUtils.h" 
#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

using namespace llvm;

// 1. Existing Enable Flag
static cl::opt<bool> StringObf("sobf", cl::init(false),
                               cl::desc("Enable String Obfuscation"));

// 2. NEW: Custom Key Argument
static cl::opt<std::string> SobfKey("sobf_key", cl::init(""),
                                    cl::desc("Custom key for string obfuscation (default: random)"),
                                    cl::value_desc("key_string"));

struct EncryptedString {
  GlobalVariable *GV;
  std::vector<uint8_t> Key;
  uint64_t Length;
};

PreservedAnalyses StringObfuscationPass::run(Module &M, ModuleAnalysisManager &AM) {
  if (!StringObf)
    return PreservedAnalyses::all();

  std::vector<EncryptedString> ToDecrypt;
  LLVMContext &Ctx = M.getContext();

  // 3. Identify and Encrypt Strings
  for (GlobalVariable &GV : M.globals()) {
    if (!GV.hasInitializer() || !GV.isConstant()) 
        continue;

    ConstantDataSequential *CDS = dyn_cast<ConstantDataSequential>(GV.getInitializer());
    if (!CDS || !CDS->isString()) 
        continue;

    StringRef RawData = CDS->getAsString();
    std::vector<uint8_t> EncryptedData;
    std::vector<uint8_t> Key;

    // 4. Generate Key (Custom vs Random)
    for (size_t i = 0; i < RawData.size(); ++i) {
      uint8_t K;
      
      if (!SobfKey.empty()) {
        // Case A: User provided a key. Cycle through it.
        // e.g., Key="ABC", String="HELLO" -> Keys: A, B, C, A, B
        K = (uint8_t)SobfKey[i % SobfKey.size()];
      } else {
        // Case B: Default. Generate a random byte.
        K = llvm::cryptoutils->get_uint8_t(); 
      }

      Key.push_back(K);
      EncryptedData.push_back(RawData[i] ^ K);
    }

    // Replace the global variable data with encrypted data
    Constant *NewConst = ConstantDataArray::get(Ctx, EncryptedData);
    GV.setInitializer(NewConst);
    GV.setConstant(false); // Must be mutable to decrypt later!
    
    // Store info to generate decryption code
    ToDecrypt.push_back({&GV, Key, RawData.size()});
  }

  if (ToDecrypt.empty())
    return PreservedAnalyses::all();

  // 5. Create the Decryption Function (Constructor)
  FunctionType *FTy = FunctionType::get(Type::getVoidTy(Ctx), false);
  Function *DecryptFunc = Function::Create(FTy, GlobalValue::InternalLinkage, "debug_init_strings", &M);
  BasicBlock *EntryBB = BasicBlock::Create(Ctx, "entry", DecryptFunc);
  IRBuilder<> Builder(EntryBB);

  // 6. Inject Decryption Logic
  for (const auto &Item : ToDecrypt) {
    Value *BasePtr = Item.GV;

    for (uint64_t i = 0; i < Item.Length; ++i) {
      Value *Idx = ConstantInt::get(Type::getInt64Ty(Ctx), i);
      Value *CharAddr = Builder.CreateGEP(Type::getInt8Ty(Ctx), BasePtr, {Idx});

      Value *EncVal = Builder.CreateLoad(Type::getInt8Ty(Ctx), CharAddr);

      // The key is baked into the instructions here
      Value *KeyVal = ConstantInt::get(Type::getInt8Ty(Ctx), Item.Key[i]);
      Value *DecVal = Builder.CreateXor(EncVal, KeyVal);

      Builder.CreateStore(DecVal, CharAddr);
    }
  }

  Builder.CreateRetVoid();

  // Register as a Global Constructor
  appendToGlobalCtors(M, DecryptFunc, 0);

  return PreservedAnalyses::none();
}