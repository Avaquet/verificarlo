/******************************************************************************
 *                                                                            *
 *  This file is part of Verificarlo.                                         *
 *                                                                            *
 *  Copyright (c) 2020                                                        *
 *     Verificarlo contributors                                               *
 *                                                                            *
 *  Verificarlo is free software: you can redistribute it and/or modify       *
 *  it under the terms of the GNU General Public License as published by      *
 *  the Free Software Foundation, either version 3 of the License, or         *
 *  (at your option) any later version.                                       *
 *                                                                            *
 *  Verificarlo is distributed in the hope that it will be useful,            *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of            *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the             *
 *  GNU General Public License for more details.                              *
 *                                                                            *
 *  You should have received a copy of the GNU General Public License         *
 *  along with Verificarlo.  If not, see <http://www.gnu.org/licenses/>.      *
 *                                                                            *
 ******************************************************************************/

#include "libVFCFuncInstrument.hpp"

#include <cxxabi.h>
#include <fstream>
#include <iostream>
#include <set>
#include <stdio.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>

using namespace llvm;

namespace {

// Fill use_double and use_float with true if the instruction ii use at least
// of the managed types
void haveFloatingPointArithmetic(Instruction &ii, bool *use_float,
                                 bool *use_double) {
  for (size_t i = 0; i < ii.getNumOperands(); i++) {
    Type *opType = ii.getOperand(i)->getType();

    if (opType->isVectorTy()) {
      VectorType *t = static_cast<VectorType *>(opType);
      opType = t->getElementType();
    }

    if (opType == FloatTy)
      (*use_float) = true;

    if (opType == DoubleTy)
      (*use_double) = true;
  }
}

// Fill use_double and use_float with true if the call_inst pi use at least
// of the managed types
void haveFloatingPointArithmetic(Instruction *I, Function *f, bool *use_float,
                                 bool *use_double) {
  // get the return type
  Type *ReturnTy = (f) ? f->getReturnType() : I->getType();

  // Test if return type of call is float
  (*use_float) = ReturnTy == FloatTy;

  // Test if return type of call is double
  (*use_double) = ReturnTy == DoubleTy;

  // Test if f treat floats point numbers
  if (f != NULL && f->size() != 0) {
    // Loop over each instruction of the function and test if one of them
    // use float or double
    for (auto &bbi : (*f)) {
      for (auto &ii : bbi) {
        haveFloatingPointArithmetic(ii, use_float, use_double);
      }
    }
  } else if (I != NULL) {
    // Loop over arguments types
    for (auto it = I->op_begin(); it < I->op_end() - 1; it++) {
      if ((*it)->getType() == FloatTy || (*it)->getType() == FloatPtrTy)
        (*use_float) = true;
      if ((*it)->getType() == DoubleTy || (*it)->getType() == DoublePtrTy)
        (*use_double) = true;
    }

    CallInst *Call = cast<CallInst>(I);
  }
}

// Search the size of the Value V which is a pointer
unsigned int getSizeOf(Value *V, const Function *F) {
  // if V is an argument of the F function, search the size of V in the parent
  // of F

  // Idea for the future, if i foud a GEP instruction, check the address of the
  // source pointer and compute the remaining size
  for (auto &Args : F->args()) {
    if (&Args == V) {
      for (const auto &U : F->users()) {
        if (const CallInst *call = cast<CallInst>(U)) {
          Value *to_search = call->getOperand(Args.getArgNo());

          return getSizeOf(to_search, call->getParent()->getParent());
        }
      }
    }
  }

  // search for the AllocaInst at the origin of V
  for (auto &BB : (*F)) {
    for (auto &I : BB) {
      if (&I == V) {
        if (const AllocaInst *Alloca = dyn_cast<AllocaInst>(&I)) {
          if (Alloca->getAllocatedType()->isArrayTy() ||
              Alloca->getAllocatedType()->isVectorTy()) {
            return Alloca->getAllocatedType()->getArrayNumElements();
          } else {
            return 1;
          }
        } else if (const GetElementPtrInst *GEP =
                       dyn_cast<GetElementPtrInst>(&I)) {
          Value *to_search = GEP->getOperand(0);
          return getSizeOf(to_search, F);
        }
      }
    }
  }

  return 0;
}

void InstrumentFunction(std::vector<Value *> MetaData,
                        Function *CurrentFunction, Function *HookedFunction,
                        const CallInst *call, BasicBlock *B, Module &M) {
  IRBuilder<> Builder(B);

  // Step 1: add space on the heap for pointers
  size_t output_cpt = 0;
  std::vector<Value *> OutputAlloca;

  if (HookedFunction->getReturnType() == DoubleTy) {
    OutputAlloca.push_back(Builder.CreateAlloca(DoubleTy, nullptr));
    output_cpt++;
  } else if (HookedFunction->getReturnType() == FloatTy) {
    OutputAlloca.push_back(Builder.CreateAlloca(FloatTy, nullptr));
    output_cpt++;
  } else if (HookedFunction->getReturnType() == FloatPtrTy && call) {
    output_cpt++;
  } else if (HookedFunction->getReturnType() ==
                 Type::getDoublePtrTy(M.getContext()) &&
             call) {
    output_cpt++;
  }

  size_t input_cpt = 0;
  std::vector<Value *> InputAlloca;

  for (auto &args : CurrentFunction->args()) {
    if (args.getType() == DoubleTy) {
      InputAlloca.push_back(Builder.CreateAlloca(DoubleTy, nullptr));
      input_cpt++;
    } else if (args.getType() == FloatTy) {
      InputAlloca.push_back(Builder.CreateAlloca(FloatTy, nullptr));
      input_cpt++;
    } else if (args.getType() == FloatPtrTy && call) {
      input_cpt++;
      output_cpt++;
    } else if (args.getType() == Type::getDoublePtrTy(M.getContext()) && call) {
      input_cpt++;
      output_cpt++;
    }
  }

  std::vector<Value *> InputMetaData = MetaData;
  InputMetaData.push_back(ConstantInt::get(Builder.getInt32Ty(), input_cpt));

  std::vector<Value *> OutputMetaData = MetaData;
  OutputMetaData.push_back(ConstantInt::get(Builder.getInt32Ty(), output_cpt));

  // Step 2: for each function input (arguments), add its type, size, name and
  // address to the list of parameters sent to vfc_enter for processing.
  std::vector<Value *> EnterArgs = InputMetaData;
  size_t input_index = 0;
  for (auto &args : CurrentFunction->args()) {
    if (args.getType() == DoubleTy) {
      EnterArgs.push_back(Types2val[DOUBLE]);
      EnterArgs.push_back(ConstantInt::get(Int32Ty, 1));
      EnterArgs.push_back(InputAlloca[input_index]);
      Builder.CreateStore(&args, InputAlloca[input_index++]);
    } else if (args.getType() == FloatTy) {
      EnterArgs.push_back(Types2val[FLOAT]);
      EnterArgs.push_back(ConstantInt::get(Int32Ty, 1));
      EnterArgs.push_back(InputAlloca[input_index]);
      Builder.CreateStore(&args, InputAlloca[input_index++]);
    } else if (args.getType() == FloatPtrTy && call) {
      EnterArgs.push_back(Types2val[FLOAT_PTR]);
      EnterArgs.push_back(
          ConstantInt::get(Int32Ty, getSizeOf(call->getOperand(args.getArgNo()),
                                              call->getParent()->getParent())));
      EnterArgs.push_back(&args);
    } else if (args.getType() == DoublePtrTy && call) {
      EnterArgs.push_back(Types2val[DOUBLE_PTR]);
      EnterArgs.push_back(
          ConstantInt::get(Int32Ty, getSizeOf(call->getOperand(args.getArgNo()),
                                              call->getParent()->getParent())));
      EnterArgs.push_back(&args);
    }
  }

  // Step 3: call vfc_enter
  Builder.CreateCall(func_enter, EnterArgs);

  // Step 4: load modified values
  std::vector<Value *> FunctionArgs;
  input_index = 0;
  for (auto &args : CurrentFunction->args()) {
    if (args.getType() == DoubleTy) {
      FunctionArgs.push_back(
          Builder.CreateLoad(DoubleTy, InputAlloca[input_index++]));
    } else if (args.getType() == FloatTy) {
      FunctionArgs.push_back(
          Builder.CreateLoad(FloatTy, InputAlloca[input_index++]));
    } else {
      FunctionArgs.push_back(&args);
    }
  }

  // Step 5: call hooked function with modified values
  Value *ret;
  if (call) {
    CallInst *hook = cast<CallInst>(call->clone());
    int i = 0;
    for (auto &args : FunctionArgs)
      hook->setArgOperand(i++, args);
    hook->setCalledFunction(HookedFunction);
    ret = Builder.Insert(hook);
  } else {
    CallInst *call = CallInst::Create(HookedFunction, FunctionArgs);
    call->setAttributes(HookedFunction->getAttributes());
    call->setCallingConv(HookedFunction->getCallingConv());
    ret = Builder.Insert(call);
  }

  // Step 6: for each function output (return value, and pointers as argument),
  // add its type, size, name and address to the list of parameters sent to
  // vfc_exit for processing.
  std::vector<Value *> ExitArgs = OutputMetaData;
  if (ret->getType() == DoubleTy) {
    ExitArgs.push_back(Types2val[DOUBLE]);
    ExitArgs.push_back(ConstantInt::get(Int32Ty, 1));
    ExitArgs.push_back(OutputAlloca[0]);
    Builder.CreateStore(ret, OutputAlloca[0]);
  } else if (ret->getType() == FloatTy) {
    ExitArgs.push_back(Types2val[FLOAT]);
    ExitArgs.push_back(ConstantInt::get(Int32Ty, 1));
    ExitArgs.push_back(OutputAlloca[0]);
    Builder.CreateStore(ret, OutputAlloca[0]);
  } else if (HookedFunction->getReturnType() == FloatPtrTy && call) {
    ExitArgs.push_back(Types2val[FLOAT_PTR]);
    ExitArgs.push_back(ConstantInt::get(
        Int32Ty, getSizeOf(ret, call->getParent()->getParent())));
    ExitArgs.push_back(ret);
  } else if (HookedFunction->getReturnType() == DoublePtrTy && call) {
    ExitArgs.push_back(Types2val[DOUBLE_PTR]);
    ExitArgs.push_back(ConstantInt::get(
        Int32Ty, getSizeOf(ret, call->getParent()->getParent())));
    ExitArgs.push_back(ret);
  }

  for (auto &args : CurrentFunction->args()) {
    if (args.getType() == FloatPtrTy && call) {
      ExitArgs.push_back(Types2val[FLOAT_PTR]);
      ExitArgs.push_back(
          ConstantInt::get(Int32Ty, getSizeOf(call->getOperand(args.getArgNo()),
                                              call->getParent()->getParent())));
      ExitArgs.push_back(&args);
    } else if (args.getType() == DoublePtrTy && call) {
      ExitArgs.push_back(Types2val[DOUBLE_PTR]);
      ExitArgs.push_back(
          ConstantInt::get(Type::getInt32Ty(M.getContext()),
                           getSizeOf(call->getOperand(args.getArgNo()),
                                     call->getParent()->getParent())));
      ExitArgs.push_back(&args);
    }
  }

  // Step 7: call vfc_exit
  Builder.CreateCall(func_exit, ExitArgs);

  // Step 8: load the modified return value
  if (ret->getType() == DoubleTy) {
    ret = Builder.CreateLoad(DoubleTy, OutputAlloca[0]);
  } else if (ret->getType() == FloatTy) {
    ret = Builder.CreateLoad(FloatTy, OutputAlloca[0]);
  }

  // Step 9: return the modified return value if necessary
  if (HookedFunction->getReturnType() != Builder.getVoidTy()) {
    Builder.CreateRet(ret);
  } else {
    Builder.CreateRetVoid();
  }
}

// return the absolute path of the module
std::string getSourceFileNameAbsPath(Module &M) {
  std::string filename = M.getSourceFileName();
  if (sys::path::is_absolute(filename))
    return filename;

  SmallString<4096> path;
  sys::fs::current_path(path);
  path.append("/" + filename);
  if (not sys::fs::make_absolute(path)) {
    return path.str().str();
  } else {
    return "";
  }
}

// return the good TLI (be careful with the used version)
const TargetLibraryInfo &getTLI(Function *f) {
  TargetLibraryInfoWrapperPass TLIWP;

#if LLVM_VERSION_MAJOR >= 10
  return TLIWP.getTLI(*f);
#else
  return TLIWP.getTLI();
#endif
}

struct VfclibFunc : public ModulePass {
  static char ID;
  std::vector<std::pair<Function *, std::string>> OriginalFunctions;
  // std::vector<Function *> ClonedFunctions;
  size_t inst_cpt;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
  }

  VfclibFunc() : ModulePass(ID) { inst_cpt = 1; }

  bool mustInstrument(Type *T) {
    if (T->isPointerTy()) {
      T = T->getPointerElementType();
    } else if (T->isVectorTy()) {
      VectorType *t = static_cast<VectorType *>(T);
      T = t->getElementType();
    }

    return T->isFloatTy() || T->isDoubleTy();
  }

  bool mustInstrument(Instruction &I) {
    bool isCall = isa<CallInst>(I);

    if (isCall) {
      Function *calledFunction = cast<CallInst>(I).getCalledFunction();
      isCall = !(calledFunction == NULL || calledFunction->isIntrinsic());
    }

    bool useFloat = false;

    if (mustInstrument(I.getType())) {
      useFloat = true;
    }

    for (Use *U = I.op_begin(); U != I.op_end(); U++) {
      if (mustInstrument(U->get()->getType())) {
        useFloat = true;
      }
    }

    return isCall && useFloat;
  }

  virtual bool runOnModule(Module &M) {
    FloatTy = Type::getFloatTy(M.getContext());
    FloatPtrTy = Type::getFloatPtrTy(M.getContext());
    DoubleTy = Type::getDoubleTy(M.getContext());
    DoublePtrTy = Type::getDoublePtrTy(M.getContext());
    Int8Ty = Type::getInt8Ty(M.getContext());
    Int8PtrTy = Type::getInt8PtrTy(M.getContext());
    Int32Ty = Type::getInt32Ty(M.getContext());

    Types2val[0] = ConstantInt::get(Int32Ty, 0);
    Types2val[1] = ConstantInt::get(Int32Ty, 1);
    Types2val[2] = ConstantInt::get(Int32Ty, 2);
    Types2val[3] = ConstantInt::get(Int32Ty, 3);

    /*************************************************************************
     *                  Get original functions's names                       *
     *************************************************************************/
    for (auto &F : M) {
      if ((F.getName().str() != "main") && F.size() != 0) {
        OriginalFunctions.push_back(
            std::pair<Function *, std::string>(&F, F.getName().str()));
      }
    }

    /*************************************************************************
     *                  Enter and exit functions declarations                *
     *************************************************************************/
    // Argument of the function  func_name    id           nb_args
    std::vector<Type *> ArgTypes{Int8PtrTy, Int8PtrTy, Int32Ty};

    // Signature of enter_function and exit_function
    FunctionType *FunTy =
        FunctionType::get(Type::getVoidTy(M.getContext()), ArgTypes, true);

    // void vfc_enter_function (char*, char*create null pointer llvm builder,
    // int, ...)
    func_enter = Function::Create(FunTy, Function::ExternalLinkage,
                                  "vfc_enter_function", &M);
    func_enter->setCallingConv(CallingConv::C);

    // void vfc_exit_function (char*, char*, int, ...)
    func_exit = Function::Create(FunTy, Function::ExternalLinkage,
                                 "vfc_exit_function", &M);
    func_exit->setCallingConv(CallingConv::C);

    /*************************************************************************
     *                             Main special case                         *
     *************************************************************************/
    Function *Clone = NULL;

    if (M.getFunction("main")) {
      Function *Main = M.getFunction("main");

      ValueToValueMapTy VMap;
      Clone = CloneFunction(Main, VMap);

      DISubprogram *Sub = Main->getSubprogram();
      std::string Name = Main->getName().str();
      std::string File = M.getSourceFileName();
      std::string Line = std::to_string(Sub->getLine());
      std::string Column = std::to_string(0);

      std::string NewName = "vfc_main__hook";

      // Test if the function use double or float
      bool use_float, use_double;
      haveFloatingPointArithmetic(NULL, Main, &use_float, &use_double);

      // Delete Main Body
      Main->deleteBody();

      // create the basic block of the hook function
      BasicBlock *block = BasicBlock::Create(M.getContext(), "block", Main);
      IRBuilder<> Builder(block);

      // Create function name
      Value *FunctionName = Builder.CreateGlobalStringPtr(Name);

      // Create Instruction ID
      Value *InstructionID =
          ConstantPointerNull::get(PointerType::get(Int8Ty, 0));

      // Enter metadata arguments
      std::vector<Value *> MetaData{FunctionName, InstructionID};

      Clone->setName(NewName);

      InstrumentFunction(MetaData, Main, Clone, NULL, block, M);

      // OriginalFunctions.push_back(Clone);
      OriginalFunctions.push_back(
          std::pair<Function *, std::string>(Clone, "main"));
    }

    /*************************************************************************
     *                      Instrument Function calls                        *
     *************************************************************************/
    for (auto &P : OriginalFunctions) {
      Function *F = P.first;

      if (F->size() != 0) {
        std::string Parent = P.second;

        for (auto &B : (*F)) {
          IRBuilder<> Builder(&B);

          for (auto ii = B.begin(); ii != B.end();) {
            Instruction *pi = &(*ii++);

            if (isa<CallInst>(pi)) {
              Function *f = cast<CallInst>(pi)->getCalledFunction();

              if (f && !f->isIntrinsic()) {
                DebugLoc Loc = pi->getDebugLoc();
                DISubprogram *Sub = f->getSubprogram();
                std::string Line = std::to_string(Loc.getLine());
                std::string Column = std::to_string(Loc.getCol());
                std::string File = M.getSourceFileName();
                std::string Name = f->getName().str();

                std::string NewName = "vfc_" + File + "/" + Parent + "/" +
                                      Name + "/" + Line + "/" +
                                      std::to_string(inst_cpt++) + +"_hook";

                // Test if the function use double or float
                bool use_float, use_double;
                haveFloatingPointArithmetic(cast<CallInst>(pi), NULL,
                                            &use_float, &use_double);

                // Create function Name
                Value *FunctionName = Builder.CreateGlobalStringPtr(Name);

                // Create Instruction ID
                Value *InstructionID;
                if (pi->getMetadata("VFC_PROFILE_NAME") != NULL) {
                  std::string InstID =
                      cast<MDString>(
                          pi->getMetadata("VFC_PROFILE_NAME")->getOperand(0))
                          ->getString();
                  InstructionID = Builder.CreateGlobalStringPtr(InstID);
                } else {
                  InstructionID =
                      ConstantPointerNull::get(PointerType::get(Int8Ty, 0));
                }

                // Enter function arguments
                std::vector<Value *> MetaData{FunctionName, InstructionID};

                Type *ReturnTy = f->getReturnType();
                std::vector<Type *> CallTypes;
                for (auto it = pi->op_begin(); it < pi->op_end() - 1; it++) {
                  CallTypes.push_back(cast<Value>(it)->getType());
                }

                // Create the hook function
                FunctionType *HookFunTy =
                    FunctionType::get(ReturnTy, CallTypes, false);
                Function *hook_func = Function::Create(
                    HookFunTy, Function::ExternalLinkage, NewName, &M);

                // Gives to the hook function the calling convention and
                // attributes of the original function.
                hook_func->setAttributes(f->getAttributes());
                hook_func->setCallingConv(f->getCallingConv());

                BasicBlock *block =
                    BasicBlock::Create(M.getContext(), "block", hook_func);

                // Instrument the original function call
                InstrumentFunction(MetaData, hook_func, f, cast<CallInst>(pi),
                                   block, M);
                // Replace the call to the original function by a call to the
                // hook function
                cast<CallInst>(pi)->setCalledFunction(hook_func);
              }
            }
          }
        }
      }
    }

    return true;
  }
}; // namespace

} // namespace

char VfclibFunc::ID = 0;
static RegisterPass<VfclibFunc>
    X("vfclibfunc", "verificarlo function instrumentation pass", false, false);
