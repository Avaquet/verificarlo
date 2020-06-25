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
                                      bool *have_double,
                                      Module &M)
    {
      Function *f = cast<CallInst>(pi)->getCalledFunction();
      Type *FloatTy = Type::getFloatTy(M.getContext());
      Type *DoubleTy = Type::getDoubleTy(M.getContext());
      Type *FloatPtrTy = Type::getFloatPtrTy(M.getContext());
      Type *DoublePtrTy = Type::getDoublePtrTy(M.getContext());
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

    virtual bool runOnModule(Module &M)
    {
      const TargetLibraryInfo *TLI = &getAnalysis<TargetLibraryInfoWrapperPass>().getTLI();
      const DataLayout &DL = M.getDataLayout();

      // Types
      Type *Int8PtrTy = Type::getInt8PtrTy(M.getContext());
      Type *Int8Ty = Type::getInt8Ty(M.getContext());
      Type *Int32Ty = Type::getInt32Ty(M.getContext());
      Type *FloatTy = Type::getFloatTy(M.getContext());
      Type *DoubleTy = Type::getDoubleTy(M.getContext());
      Type *FloatPtrTy = Type::getFloatPtrTy(M.getContext());
      Type *DoublePtrTy = Type::getDoublePtrTy(M.getContext());
      Type *VoidTy = Type::getVoidTy(M.getContext());

      // Types string ptr
      IRBuilder<> Builder(&(*(*M.begin()).begin()));
      Value *Types2val[] = {  ConstantInt::get(Int32Ty, 0),
                                ConstantInt::get(Int32Ty, 1),
                                ConstantInt::get(Int32Ty, 2),
                                ConstantInt::get(Int32Ty, 0)};            

      /********************** Enter and exit functions declarations **********************/
      std::vector<Type *> ArgTypes;

      ArgTypes.push_back(Int8PtrTy);
      ArgTypes.push_back(Int8Ty);
      ArgTypes.push_back(Int8Ty);
      ArgTypes.push_back(Int8Ty);
      ArgTypes.push_back(Int8Ty);
      ArgTypes.push_back(Int32Ty);

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

      /********************** Function's calls instrumentation **********************/
       for (auto &F : M) {
        for (auto &B : F){
          IRBuilder<> Builder(&B);

          for (auto ii = B.begin(); ii != B.end(); ){
            Instruction *pi = &(*ii++);
            
            if (isa<CallInst>(pi)) {
              // collect metadata info //
              Function *f = cast<CallInst>(pi)->getCalledFunction();
              MDNode *N = pi->getMetadata("dbg"); 
              DILocation* Loc = cast<DILocation>(N);
              unsigned Line = Loc->getLine();
              std::string File = Loc->getFilename().str();
              std::string Name = f->getName().str();

              // Test if f is a library function //
              LibFunc libfunc;
              bool is_from_library = TLI->getLibFunc(f->getName(), libfunc);

              // Test if f is instrinsic //
              bool is_intrinsic = f->isIntrinsic();

              bool have_float, have_double;

              // Test if the function use double or float
              haveFloatingPointArithmetic(pi, 
                                          is_from_library, 
                                          is_intrinsic, 
                                          &have_float, 
                                          &have_double,
                                          M);

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
              std::vector<Value *> EnterArgs;
              EnterArgs.push_back(FunctionID);
              EnterArgs.push_back(isLibraryFunction);
              EnterArgs.push_back(isInstrinsicFunction);
              EnterArgs.push_back(haveFloat);
              EnterArgs.push_back(haveDouble);
              EnterArgs.push_back(zero_const);

              // Exit function arguments
              std::vector<Value *> ExitArgs;
              ExitArgs.push_back(FunctionID);
              ExitArgs.push_back(isLibraryFunction);
              ExitArgs.push_back(isInstrinsicFunction);
              ExitArgs.push_back(haveFloat);
              ExitArgs.push_back(haveDouble);
              ExitArgs.push_back(zero_const);

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

                // Get type
                Type* VType;
                Value *TypeID;

                if (v->getType() == FloatTy){
                  VType = FloatTy;
                  TypeID = Types2val[FLOAT];
                }else if (v->getType() == FloatPtrTy){
                  VType = FloatPtrTy;
                  TypeID = Types2val[FLOAT_ARRAY];
                }else if (v->getType() == DoubleTy){
                  VType = DoubleTy;
                  TypeID = Types2val[DOUBLE];
                }else if(v->getType() == DoublePtrTy){
                  VType = DoublePtrTy;
                  TypeID = Types2val[DOUBLE_ARRAY];
                }else{
                  FunctionArgs.push_back(v);
                  continue;
                }

                EnterArgs.push_back(TypeID);

                // Allocate pointer
                Constant *size = ConstantInt::get(Int32Ty, DL.getPrefTypeAlignment(VType));
                AllocaInst* ptr = new AllocaInst(VType, 
                                                0, 
                                                size, 
                                                FunctionName + "_arg_" + std::to_string(m));
                ptr->insertBefore(&(*(B.begin())));

                // Enter store and load
                StoreInst *enter_str = new StoreInst(v, ptr);
                enter_str->insertBefore(enter_call);
                LoadInst *enter_load = new LoadInst(VType, ptr);
                enter_load->insertAfter(enter_call);
                EnterArgs.push_back(ptr);
                FunctionArgs.push_back(enter_load);

                m++;
              }


              // Values modified by the instumented function
              std::vector<Value *> Returns;
              Returns.push_back(func_call);

              int n = 0;
              for (auto it = Returns.begin(); it < Returns.end(); it++){
                // Get Value
                Value *v = (*it);

                // Get type
                Type* VType;
                Value *TypeID;

                if (v->getType() == FloatTy){
                  VType = FloatTy;
                  TypeID = Types2val[FLOAT];
                }else if (v->getType() == FloatPtrTy){
                  VType = FloatPtrTy;
                  TypeID = Types2val[FLOAT_ARRAY];
                }else if (v->getType() == DoubleTy){
                  VType = DoubleTy;
                  TypeID = Types2val[DOUBLE];
                }else if(v->getType() == DoublePtrTy){
                  VType = DoublePtrTy;
                  TypeID = Types2val[DOUBLE_ARRAY];
                }else{
                  pi->eraseFromParent();
                  continue;
                }

                ExitArgs.push_back(TypeID);

                // Allocate pointer
                Constant *size = ConstantInt::get(Int32Ty, DL.getPrefTypeAlignment(VType));
                AllocaInst* ptr = new AllocaInst(VType, 
                                                0, 
                                                size, 
                                                FunctionName + "_return_" + std::to_string(n));
                ptr->insertBefore(&(*(B.begin())));

                if (n != 0){
                  StoreInst *exit_str = new StoreInst(v, ptr);
                  exit_str->insertBefore(exit_call);
                  LoadInst *exit_load = new LoadInst( VType, ptr);                  
                  exit_load->insertAfter(exit_call);
                  ExitArgs.push_back(ptr);
                }else{
                  StoreInst *exit_str = new StoreInst(v, ptr);
                  exit_str->insertBefore(exit_call);
                  LoadInst *exit_load = new LoadInst( VType, ptr);
                  ReplaceInstWithInst(pi, exit_load);
                  ExitArgs.push_back(ptr);
                }

                n++;
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