/******************************************************************************
 *                                                                            *
 *  This file is part of Verificarlo.                                         *
 *                                                                            *
 *  Copyright (c) 2015                                                        *
 *     Universite de Versailles St-Quentin-en-Yvelines                        *
 *     CMLA, Ecole Normale Superieure de Cachan                               *
 *  Copyright (c) 2018                                                        *
 *     Universite de Versailles St-Quentin-en-Yvelines                        *
 *  Copyright (c) 2019                                                        *
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

#include "../../config.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/CallSite.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/UnifyFunctionExitNodes.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/Analysis/CallGraphSCCPass.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Support/Casting.h" 
#include "llvm/IR/Metadata.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Type.h"

#include <set>
#include <iostream>
#include <fstream>
#include <utility>
#include <stdio.h>
#include <vector>
#include <cxxabi.h>

using namespace llvm;

namespace {

    static Type *Int8PtrTy;
    static Type *Int8Ty;
    static Type *Int32Ty;
    static Type *Int64Ty;
    static Type *FloatTy;
    static Type *DoubleTy;
    static Type *FloatPtrTy;
    static Type *DoublePtrTy;
    static Type *VoidTy;

  // Enumeration of managed types 
  enum Ftypes { FLOAT, FLOAT_ARRAY, DOUBLE, DOUBLE_ARRAY};

  struct VfclibFunc : public ModulePass{
    static char ID;

    VfclibFunc() : ModulePass(ID) {}

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.setPreservesCFG();
      AU.addRequired<TargetLibraryInfoWrapperPass>();
    }

    // Fill have_double and have_float with true if the call_inst pi use at least 
    // of the managed types
    void haveFloatingPointArithmetic( Instruction *pi, 
                                      bool is_from_library, 
                                      bool is_intrinsic, 
                                      bool *have_float, 
                                      bool *have_double)
    {
      Function *f = cast<CallInst>(pi)->getCalledFunction();
      Type* ReturnTy = pi->getType();

      // Test if return type of pi is float
      (*have_float) = (ReturnTy == FloatTy || ReturnTy == FloatPtrTy);

      // Test if return type of pi is double
      (*have_double) = (ReturnTy == DoubleTy || ReturnTy == DoublePtrTy);

      // Test if f treat floats point numbers
      if (is_intrinsic || is_from_library){
        // Loop over arguments types 
        for (auto it = pi->op_begin(); it < pi->op_end()-1; it++){
          if ((*it)->getType() == FloatTy || (*it)->getType() == FloatPtrTy)
            (*have_float) = true;
          if ((*it)->getType() == DoubleTy || (*it)->getType() == DoublePtrTy)
            (*have_double) = true;
        }
      }else{
        // Loop over each instruction of the function and test if one of them 
        // use float or double
        for (auto bbi = f->begin(); bbi != f->end(); bbi++){
          for (auto ii = bbi->begin(); ii != bbi->end(); ii++){
            Type *opType = ii->getOperand(0)->getType();
            
            if (opType->isVectorTy()){
              VectorType *t = static_cast<VectorType *>(opType);
              opType = t->getElementType();
            }

            if (opType == FloatTy || opType == FloatPtrTy)
              (*have_float) = true;

            if (opType == DoubleTy || opType == DoublePtrTy)
              (*have_double) = true;
          }
                  
          if ((*have_float) && (*have_double))
            break;
        }
      }
    }

    // Demangling function 
    std::string demangle(std::string src)
    {
      int status = 0;
      char *demangled_name = NULL;
      if( (demangled_name = abi::__cxa_demangle(src.c_str(), 0, 0, &status))){
        src = demangled_name;
        std::size_t first = src.find("(");
        src = src.substr(0,first);
      }
      free(demangled_name);

      return src;
    }

    AllocaInst* allocatePtr(BasicBlock &B, const DataLayout &DL, Type* VType, std::string FunctionName)
    {
      static size_t cpt = 0;

      Constant *size = ConstantInt::get(Int32Ty, DL.getPrefTypeAlignment(VType));
      AllocaInst* ptr = new AllocaInst( VType, 
                                        0, 
                                        size, 
                                        FunctionName + "_arg_" + std::to_string(cpt++));
      ptr->insertBefore(&(*(B.begin())));

      return ptr;
    }

    Type* fillArgs(bool is_enter, std::vector<Value*> &Args, Value* v, Value **Types2val, int &cpt)
    {
      Type* VType = NULL;
      Type* T = v->getType();

      if (T == FloatTy){
        // Push the typeID
        VType = FloatTy;
        Args.push_back(Types2val[FLOAT]);

        // Number of argument added
        cpt += 2;

      }else if (T == FloatPtrTy){
        // Push the typeID
        VType = FloatPtrTy;
        Args.push_back(Types2val[FLOAT_ARRAY]);

        size_t n_elements = 0;
        if (isa<GetElementPtrInst>(v)){
          Type *T = cast<PointerType>(cast<GetElementPtrInst>(v)->getPointerOperandType())->getElementType();
          n_elements = cast<ArrayType>(T)->getNumElements();                  
        }

        // Size of the array
        Constant *size_value = ConstantInt::get(Int64Ty, n_elements);
        Args.push_back(size_value);

        cpt += 3;

        if (is_enter){
          // Pointer on the original array
          Args.push_back(v);
          cpt++;
        }

      }else if (T == DoubleTy){
        // Push the typeID
        VType = DoubleTy;
        Args.push_back(Types2val[DOUBLE]);

        // Number of argument added
        cpt += 2;

      }else if(T == DoublePtrTy){
        // Push the typeID
        VType = DoublePtrTy;
        Args.push_back(Types2val[DOUBLE_ARRAY]);

        size_t n_elements = 0;
        if (isa<GetElementPtrInst>(v)){
          Type *T = cast<PointerType>(cast<GetElementPtrInst>(v)->getPointerOperandType())->getElementType();
          n_elements = cast<ArrayType>(T)->getNumElements();                   
        }

        // Size of the array
        Constant *size_value = ConstantInt::get(Int64Ty, n_elements);
        Args.push_back(size_value);

        cpt += 3;

        if (is_enter){
          // Pointer on the original array
          Args.push_back(v);
          cpt++;
        }
      }

      return VType;
    }

    virtual bool runOnModule(Module &M)
    {
      const TargetLibraryInfo *TLI = &getAnalysis<TargetLibraryInfoWrapperPass>().getTLI();
      const DataLayout &DL = M.getDataLayout();

      // Types
      Int8PtrTy = Type::getInt8PtrTy(M.getContext());
      Int8Ty = Type::getInt8Ty(M.getContext());
      Int32Ty = Type::getInt32Ty(M.getContext());
      Int64Ty = Type::getInt64Ty(M.getContext());
      FloatTy = Type::getFloatTy(M.getContext());
      DoubleTy = Type::getDoubleTy(M.getContext());
      FloatPtrTy = Type::getFloatPtrTy(M.getContext());
      DoublePtrTy = Type::getDoublePtrTy(M.getContext());
      VoidTy = Type::getVoidTy(M.getContext());

      // Types string ptr
      IRBuilder<> Builder(&(*(*M.begin()).begin()));
      Value *Types2val[] = {  ConstantInt::get(Int32Ty, 0),
                                ConstantInt::get(Int32Ty, 1),
                                ConstantInt::get(Int32Ty, 2),
                                ConstantInt::get(Int32Ty, 3)};            

      /********************** Enter and exit functions declarations **********************/
      std::vector<Type *> ArgTypes {Int8PtrTy, Int8Ty, Int8Ty, Int8Ty, Int8Ty, Int32Ty};

      // void vfc_enter_function (char*, char, char, char, char, int, ...)
      Constant *func = M.getOrInsertFunction("vfc_enter_function", 
                                            FunctionType::get(VoidTy, 
                                            ArgTypes, 
                                            true));

      Function *func_enter = cast<Function>(func);

      // void vfc_exit_function (char*, char, char, char, char, int, ...)
      func = M.getOrInsertFunction("vfc_exit_function", 
                                    FunctionType::get(VoidTy, 
                                    ArgTypes, 
                                    true));

      Function *func_exit = cast<Function>(func);

      // free functions
      Function *func_free_double = cast<Function>(M.getOrInsertFunction("free", VoidTy, DoublePtrTy));

      Function *func_free_float = cast<Function>(M.getOrInsertFunction("free", VoidTy, FloatPtrTy));

      /********************** Function's calls instrumentation **********************/
       for (auto &F : M) {
        for (auto &B : F){
          IRBuilder<> Builder(&B);

          for (auto ii = B.begin(); ii != B.end(); ){
            Instruction *pi = &(*ii++);
            
            if (isa<CallInst>(pi)) {
              // collect metadata info //
              Function *f = cast<CallInst>(pi)->getCalledFunction();
              // get location information on the function call
              DILocation* Loc = cast<DILocation>(pi->getMetadata("dbg"));
              // get line of the function call
              unsigned Line = Loc->getLine();
              // get file of the function call
              std::string File = Loc->getFilename().str();
              // get name of the function call
              std::string Name = f->getName().str();

              // Test if f is a library function //
              LibFunc libfunc;
              bool is_from_library = TLI->getLibFunc(f->getName(), libfunc);

              // Test if f is intrinsic //
              bool is_intrinsic = f->isIntrinsic();

              bool have_float, have_double;

              // Test if the function use double or float
              haveFloatingPointArithmetic(pi, 
                                          is_from_library, 
                                          is_intrinsic, 
                                          &have_float, 
                                          &have_double);

              // If not, don't instrument the function
              if (!have_double && !have_float)
                continue;

              // Demangle the name of F //
              Name = demangle(Name);

              // Create function ID
              std::string FunctionName = File + "/" + Name + "_" + std::to_string(Line);
              Value *FunctionID = Builder.CreateGlobalStringPtr(FunctionName);
 
              // Constants creation 
              Constant * isLibraryFunction = ConstantInt::get(Int8Ty, is_from_library);
              Constant * isInstrinsicFunction = ConstantInt::get(Int8Ty, is_intrinsic);
              Constant * haveFloat = ConstantInt::get(Int8Ty, have_float);
              Constant * haveDouble = ConstantInt::get(Int8Ty, have_double);
              Constant * zero_const = ConstantInt::get(Int32Ty, 0);

              // Enter function arguments
              std::vector<Value *> EnterArgs {FunctionID, isLibraryFunction, isInstrinsicFunction, \
                                              haveFloat, haveDouble, zero_const};

              // Exit function arguments
              std::vector<Value *> ExitArgs { FunctionID, isLibraryFunction, isInstrinsicFunction, \
                                              haveFloat, haveDouble, zero_const};

              // Temporary function call to make loads and stores insertions easier
              CallInst * enter_call = CallInst::Create(func_enter, EnterArgs);
              enter_call->insertBefore(pi);

              // The clone of the instrumented function's call will be used with hooked arguments
              // and the original one will be replaced by the load of the hooked return value
              Instruction * func_call = pi->clone();
              func_call->insertBefore(pi);

              // Temporary function call to make loads and stores insertions easier
              CallInst * exit_call = CallInst::Create(func_exit, ExitArgs);
              exit_call->insertBefore(pi);

              // Instrumented function arguments
              std::vector<Value *> FunctionArgs;

              int m = 0;
              // loop over arguments
              for (auto it = pi->op_begin(); it < pi->op_end()-1; it++){
                // Get Value
                Value *v = (*it);

                Type* VType = fillArgs(1, EnterArgs, v, Types2val, m);

                if (VType == NULL){
                  FunctionArgs.push_back(v);
                  continue; 
                }

                // Allocate pointer
                AllocaInst *ptr = allocatePtr(B, DL, VType, FunctionName);

                StoreInst *enter_str = new StoreInst(v, ptr);
                enter_str->insertBefore(enter_call);
                LoadInst *enter_load = new LoadInst(VType, ptr);
                enter_load->insertAfter(enter_call);
                EnterArgs.push_back(ptr);
                FunctionArgs.push_back(enter_load);

                // Free the pointer
                if (VType == DoublePtrTy || VType == FloatPtrTy){
                  CallInst * free_call = (VType == DoublePtrTy) ? CallInst::Create(func_free_double, enter_load) : \
                                                                  CallInst::Create(func_free_float, enter_load);
                  free_call->insertAfter(func_call);
                }
              }


              // Values modified by the instrumented function
              std::vector<Value *> Returns;
              Returns.push_back(func_call);

              int n = 0;
              for (auto it = Returns.begin(); it < Returns.end(); it++){
                // Get Value
                Value *v = (*it);

                Type* VType = fillArgs(0, ExitArgs, v, Types2val, n);

                if (VType == NULL){
                  pi->eraseFromParent();
                  continue;
                }

                // Allocate pointer
                AllocaInst *ptr = allocatePtr(B, DL, VType, FunctionName);
               
                StoreInst *exit_str = new StoreInst(v, ptr);
                exit_str->insertBefore(exit_call);
                LoadInst *exit_load = new LoadInst( VType, ptr);
                ExitArgs.push_back(ptr);

                if (v == func_call){
                  ReplaceInstWithInst(pi, exit_load);
                }else{                 
                  exit_load->insertAfter(exit_call);
                }
              }


              // Replace temporary functions calls by definitive ones
              CallInst * new_enter_call = CallInst::Create(func_enter, EnterArgs);
              ReplaceInstWithInst(enter_call, new_enter_call);

              CallInst * new_exit_call = CallInst::Create(func_exit, ExitArgs);
              ReplaceInstWithInst(exit_call, new_exit_call);

              // Replace with the good number of operand
              Constant * num_operands = ConstantInt::get(Int32Ty, m);
              new_enter_call->setOperand(5, num_operands);

              // Replace with the good number of return value
              Constant * num_results = ConstantInt::get(Int32Ty, n);
              new_exit_call->setOperand(5, num_results);

              CallInst * new_func_call = CallInst::Create(f, FunctionArgs);
              ReplaceInstWithInst(func_call, new_func_call);
            }  
          } 
        }
      }

      return true;
    }
  };

}

char VfclibFunc::ID = 0;
static RegisterPass<VfclibFunc> X("vfclibfunc", "verificarlo function instrumentation pass",
                                  false, false);