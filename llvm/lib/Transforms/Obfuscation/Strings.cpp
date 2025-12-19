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
#include <vector>
#include <map>
#include <string>

using namespace llvm;

// === Command Line Options ===
static cl::opt<bool> StringObf("sobf", cl::init(false),
                               cl::desc("Enable String Obfuscation"));

static cl::opt<std::string> SobfKey("sobf_key", cl::init(""),
                                    cl::desc("Custom key for string obfuscation"),
                                    cl::value_desc("key_string"));

// Mode selection: 'static' (default) or 'stack' (runtime)
static cl::opt<std::string> SobfMode("sobf_mode", cl::init("static"),
                                     cl::desc("String obfuscation mode: 'static' (global ctor) or 'stack' (runtime stack promotion)"),
                                     cl::value_desc("mode"));

struct EncryptedString {
  GlobalVariable *GV;
  std::vector<uint8_t> Key;
  std::vector<uint8_t> EncryptedData; // Used for replacement
  uint64_t Length;
};

// === Helper Functions ===

// Generate a key and encrypt the raw data
void encryptData(const StringRef &RawData, std::vector<uint8_t> &EncData, std::vector<uint8_t> &KeyData) {
  for (size_t i = 0; i < RawData.size(); ++i) {
    uint8_t K;
    if (!SobfKey.empty()) {
      K = (uint8_t)SobfKey[i % SobfKey.size()];
    } else {
      K = llvm::cryptoutils->get_uint8_t();
    }
    KeyData.push_back(K);
    EncData.push_back(RawData[i] ^ K);
  }
}

// Helper to create the runtime decryption function (used by 'stack' mode)
Function *getOrCreateStackDecryptFunc(Module &M) {
  LLVMContext &Ctx = M.getContext();
  // void decrypt(i8* dest, i8* src, i8* key, i64 len)
  std::vector<Type*> Params = {
      PointerType::getUnqual(Ctx), // Dest
      PointerType::getUnqual(Ctx), // Src
      PointerType::getUnqual(Ctx), // Key
      Type::getInt64Ty(Ctx)        // Len
  };
  
  FunctionType *FTy = FunctionType::get(Type::getVoidTy(Ctx), Params, false);
  
  if (Function *F = M.getFunction("__jit_decrypt_string"))
    return F;

  Function *F = Function::Create(FTy, GlobalValue::PrivateLinkage, "__jit_decrypt_string", &M);
  BasicBlock *Entry = BasicBlock::Create(Ctx, "entry", F);
  IRBuilder<> Builder(Entry);

  auto ArgIt = F->arg_begin();
  Value *Dest = ArgIt++;
  Value *Src = ArgIt++;
  Value *KeyArr = ArgIt++;
  Value *Len = ArgIt++;

  BasicBlock *Loop = BasicBlock::Create(Ctx, "loop", F);
  BasicBlock *Exit = BasicBlock::Create(Ctx, "exit", F);
  
  Builder.CreateBr(Loop);
  Builder.SetInsertPoint(Loop);

  PHINode *I = Builder.CreatePHI(Type::getInt64Ty(Ctx), 2);
  I->addIncoming(ConstantInt::get(Type::getInt64Ty(Ctx), 0), Entry);

  Value *SrcPtr = Builder.CreateGEP(Type::getInt8Ty(Ctx), Src, I);
  Value *EncByte = Builder.CreateLoad(Type::getInt8Ty(Ctx), SrcPtr);

  Value *KeyPtr = Builder.CreateGEP(Type::getInt8Ty(Ctx), KeyArr, I);
  Value *KeyByte = Builder.CreateLoad(Type::getInt8Ty(Ctx), KeyPtr);

  Value *DecByte = Builder.CreateXor(EncByte, KeyByte);

  Value *DestPtr = Builder.CreateGEP(Type::getInt8Ty(Ctx), Dest, I);
  Builder.CreateStore(DecByte, DestPtr);

  Value *NextI = Builder.CreateAdd(I, ConstantInt::get(Type::getInt64Ty(Ctx), 1));
  I->addIncoming(NextI, Loop);

  Value *Cond = Builder.CreateICmpULT(NextI, Len);
  Builder.CreateCondBr(Cond, Loop, Exit);

  Builder.SetInsertPoint(Exit);
  Builder.CreateRetVoid();

  return F;
}

// === Implementations ===

void runStaticObfuscation(Module &M, const std::vector<EncryptedString> &EncryptedGlobals) {
  LLVMContext &Ctx = M.getContext();
  
  // Create Decryption Constructor
  FunctionType *FTy = FunctionType::get(Type::getVoidTy(Ctx), false);
  Function *DecryptFunc = Function::Create(FTy, GlobalValue::InternalLinkage, "debug_init_strings", &M);
  BasicBlock *EntryBB = BasicBlock::Create(Ctx, "entry", DecryptFunc);
  IRBuilder<> Builder(EntryBB);

  for (const auto &Item : EncryptedGlobals) {
    Value *BasePtr = Item.GV;
    
    // We can embed the key directly in instructions for static mode
    for (uint64_t i = 0; i < Item.Length; ++i) {
      Value *Idx = ConstantInt::get(Type::getInt64Ty(Ctx), i);
      Value *CharAddr = Builder.CreateGEP(Type::getInt8Ty(Ctx), BasePtr, {Idx});
      Value *EncVal = Builder.CreateLoad(Type::getInt8Ty(Ctx), CharAddr);
      Value *KeyVal = ConstantInt::get(Type::getInt8Ty(Ctx), Item.Key[i]);
      Value *DecVal = Builder.CreateXor(EncVal, KeyVal);
      Builder.CreateStore(DecVal, CharAddr);
    }
  }
  Builder.CreateRetVoid();
  appendToGlobalCtors(M, DecryptFunc, 0);
}

void runStackObfuscation(Module &M, const std::vector<EncryptedString> &EncryptedGlobals) {
  LLVMContext &Ctx = M.getContext();
  Function *DecryptFunc = getOrCreateStackDecryptFunc(M);

  for (const auto &Item : EncryptedGlobals) {
    // For stack mode, we need a Global Variable to hold the Key
    Constant *KeyConst = ConstantDataArray::get(Ctx, Item.Key);
    GlobalVariable *KeyGV = new GlobalVariable(M, KeyConst->getType(), true, 
                                               GlobalValue::PrivateLinkage, KeyConst, "key");

    // Replace Uses
    std::vector<Instruction*> UsersToReplace;
    for (User *U : Item.GV->users()) {
      if (Instruction *Inst = dyn_cast<Instruction>(U)) {
        UsersToReplace.push_back(Inst);
      }
    }

    for (Instruction *Inst : UsersToReplace) {
      Function *F = Inst->getFunction();
      if (!F) continue;

      IRBuilder<> Builder(Inst);
      
      // 1. Alloca on Stack (at entry block)
      IRBuilder<> EntryBuilder(&F->getEntryBlock(), F->getEntryBlock().begin());
      AllocaInst *StackBuf = EntryBuilder.CreateAlloca(
          ArrayType::get(Type::getInt8Ty(Ctx), Item.Length), 
          nullptr, "stack_str");

      // 2. Call Decrypt
      Value *DestPtr = Builder.CreatePointerCast(StackBuf, PointerType::getUnqual(Ctx));
      Value *SrcPtr = Builder.CreatePointerCast(Item.GV, PointerType::getUnqual(Ctx));
      Value *KeyPtr = Builder.CreatePointerCast(KeyGV, PointerType::getUnqual(Ctx));
      Value *LenVal = ConstantInt::get(Type::getInt64Ty(Ctx), Item.Length);

      Builder.CreateCall(DecryptFunc, {DestPtr, SrcPtr, KeyPtr, LenVal});

      // 3. Replace Operand
      for (unsigned i = 0; i < Inst->getNumOperands(); ++i) {
        if (Inst->getOperand(i) == Item.GV) {
          Inst->setOperand(i, StackBuf);
        }
      }
    }
  }
}

// === Main Pass Logic ===

PreservedAnalyses StringObfuscationPass::run(Module &M, ModuleAnalysisManager &AM) {
  if (!StringObf)
    return PreservedAnalyses::all();

  LLVMContext &Ctx = M.getContext();
  std::vector<EncryptedString> EncryptedGlobals;

  // 1. Identify and Encrypt
  for (GlobalVariable &GV : M.globals()) {
    if (!GV.hasInitializer() || !GV.isConstant()) continue;
    ConstantDataSequential *CDS = dyn_cast<ConstantDataSequential>(GV.getInitializer());
    if (!CDS || !CDS->isString()) continue;

    // Skip section-specific globals (like llvm.metadata)
    if (GV.hasSection()) continue;

    StringRef RawData = CDS->getAsString();
    std::vector<uint8_t> EncData;
    std::vector<uint8_t> KeyData;

    encryptData(RawData, EncData, KeyData);

    // Apply Encrypted Data to Global
    Constant *NewConst = ConstantDataArray::get(Ctx, EncData);
    GV.setInitializer(NewConst);
    
    // Static mode requires the global to be writable (RW)
    // Stack mode reads it as RO, but we can leave it RW to be safe.
    GV.setConstant(SobfMode == "stack"); 

    EncryptedString ES;
    ES.GV = &GV;
    ES.Key = KeyData;
    ES.Length = RawData.size();
    EncryptedGlobals.push_back(ES);
  }

  if (EncryptedGlobals.empty())
    return PreservedAnalyses::all();

  // 2. Dispatch based on Mode
  if (SobfMode == "stack") {
    runStackObfuscation(M, EncryptedGlobals);
  } else {
    // Default to static if unknown or explicit
    runStaticObfuscation(M, EncryptedGlobals);
  }

  return PreservedAnalyses::none();
}