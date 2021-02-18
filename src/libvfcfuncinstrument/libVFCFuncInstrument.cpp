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

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <cxxabi.h>
#include <fstream>
#include <iostream>
#include <set>
#include <stdio.h>
#include <string>
#include <utility>
#include <vector>


using namespace llvm;
namespace pt = boost::property_tree;

namespace {

// Fill use_double and use_float with true if the instruction ii use at least
// of the managed types
bool haveFloatingPointArithmetic(Instruction &ii, bool* use_float, bool* use_double)
{
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

  return (use_float || use_double);
}

// Fill use_double and use_float with true if the call_inst pi use at least
// of the managed types
bool haveFloatingPointArithmetic(Instruction *I, Function *f,
                                 bool *use_float, bool *use_double) {
  // get the return type
  Type *ReturnTy = (f) ? f->getReturnType(): I->getType();

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
  }else if (I != NULL) {
    // Loop over arguments types
    for (auto it = I->op_begin(); it < I->op_end() - 1; it++) {
      if ((*it)->getType() == FloatTy || (*it)->getType() == FloatPtrTy)
        (*use_float) = true;
      if ((*it)->getType() == DoubleTy || (*it)->getType() == DoublePtrTy)
        (*use_double) = true;
    }
    
    CallInst *Call = cast<CallInst>(I);
  }

  return use_double || use_float;
}

// return the vector size
unsigned isVectorized(Instruction &I)
{
  for (size_t i = 0; i < I.getNumOperands(); i++) {
    Type *opType = I.getOperand(i)->getType();

    if (opType->isVectorTy()) {
      VectorType *t = static_cast<VectorType *>(opType);
      return t->getNumElements();
    }
  }

  return 1;
}



// Search the size of the Value V which is a pointer
unsigned int getSizeOf(Value *V, const Function *F) {
  // if V is an argument of the F function, search the size of V in the parent
  // of F
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

// Get the Name of the given argument V
std::string getArgName(Function *F, Value *V, unsigned int i) {
  for (auto &BB : (*F)) {
    for (auto &I : BB) {
      if (isa<CallInst>(&I)) {
        CallInst *Call = cast<CallInst>(&I);
        
        DILocalVariable *Var = cast<DILocalVariable>(
            cast<MetadataAsValue>(I.getOperand(1))->getMetadata());

        if (Var->isParameter() && (Var->getArg() == i + 1)) {
          return Var->getName().str();
        }
      }
    }
  }

  return "parameter_" + std::to_string(i + 1);
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
      EnterArgs.push_back(Builder.CreateGlobalStringPtr(
          getArgName(HookedFunction, &args, args.getArgNo())));
      EnterArgs.push_back(ConstantInt::get(Int32Ty, 1));
      EnterArgs.push_back(InputAlloca[input_index]);
      Builder.CreateStore(&args, InputAlloca[input_index++]);
    } else if (args.getType() == FloatTy) {
      EnterArgs.push_back(Types2val[FLOAT]);
      EnterArgs.push_back(Builder.CreateGlobalStringPtr(
          getArgName(HookedFunction, &args, args.getArgNo())));
      EnterArgs.push_back(ConstantInt::get(Int32Ty, 1));
      EnterArgs.push_back(InputAlloca[input_index]);
      Builder.CreateStore(&args, InputAlloca[input_index++]);
    } else if (args.getType() == FloatPtrTy && call) {
      EnterArgs.push_back(Types2val[FLOAT_PTR]);
      EnterArgs.push_back(Builder.CreateGlobalStringPtr(
          getArgName(HookedFunction, &args, args.getArgNo())));
      EnterArgs.push_back(
          ConstantInt::get(Int32Ty, getSizeOf(call->getOperand(args.getArgNo()),
                                              call->getParent()->getParent())));
      EnterArgs.push_back(&args);
    } else if (args.getType() == DoublePtrTy && call) {
      EnterArgs.push_back(Types2val[DOUBLE_PTR]);
      EnterArgs.push_back(Builder.CreateGlobalStringPtr(
          getArgName(HookedFunction, &args, args.getArgNo())));
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
    ExitArgs.push_back(Builder.CreateGlobalStringPtr("return_value"));
    ExitArgs.push_back(ConstantInt::get(Int32Ty, 1));
    ExitArgs.push_back(OutputAlloca[0]);
    Builder.CreateStore(ret, OutputAlloca[0]);
  } else if (ret->getType() == FloatTy) {
    ExitArgs.push_back(Types2val[FLOAT]);
    ExitArgs.push_back(Builder.CreateGlobalStringPtr("return_value"));
    ExitArgs.push_back(ConstantInt::get(Int32Ty, 1));
    ExitArgs.push_back(OutputAlloca[0]);
    Builder.CreateStore(ret, OutputAlloca[0]);
  } else if (HookedFunction->getReturnType() == FloatPtrTy && call) {
    ExitArgs.push_back(Types2val[FLOAT_PTR]);
    ExitArgs.push_back(Builder.CreateGlobalStringPtr("return_value"));
    ExitArgs.push_back(ConstantInt::get(
        Int32Ty, getSizeOf(ret, call->getParent()->getParent())));
    ExitArgs.push_back(ret);
  } else if (HookedFunction->getReturnType() == DoublePtrTy && call) {
    ExitArgs.push_back(Types2val[DOUBLE_PTR]);
    ExitArgs.push_back(Builder.CreateGlobalStringPtr("return_value"));
    ExitArgs.push_back(ConstantInt::get(
        Int32Ty, getSizeOf(ret, call->getParent()->getParent())));
    ExitArgs.push_back(ret);
  }

  for (auto &args : CurrentFunction->args()) {
    if (args.getType() == FloatPtrTy && call) {
      ExitArgs.push_back(Types2val[FLOAT_PTR]);
      ExitArgs.push_back(Builder.CreateGlobalStringPtr(
          getArgName(HookedFunction, &args, args.getArgNo())));
      ExitArgs.push_back(
          ConstantInt::get(Int32Ty, getSizeOf(call->getOperand(args.getArgNo()),
                                              call->getParent()->getParent())));
      ExitArgs.push_back(&args);
    } else if (args.getType() == DoublePtrTy && call) {
      ExitArgs.push_back(Types2val[DOUBLE_PTR]);
      ExitArgs.push_back(Builder.CreateGlobalStringPtr(
          getArgName(HookedFunction, &args, args.getArgNo())));
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

Fops getFops(Instruction &I) {
  switch (I.getOpcode()) {
  case Instruction::FAdd:
    return FOP_ADD;
  case Instruction::FSub:
    // In LLVM IR the FSub instruction is used to represent FNeg
    return FOP_SUB;
  case Instruction::FMul:
    return FOP_MUL;
  case Instruction::FDiv:
    return FOP_DIV;
  case Instruction::FCmp:
    return FOP_CMP;
  default:
    return FOP_IGNORE;
  }
}

const TargetLibraryInfo &getTLI(Function *f) {
  TargetLibraryInfoWrapperPass TLIWP;

#if LLVM_VERSION_MAJOR >= 10
  return TLIWP.getTLI(*f);
#else
  return TLIWP.getTLI();
#endif
}

void add_function_metadata(pt::ptree &function, Function &F, Module &M)
{
  unsigned func_line, func_column;
  MDNode *func_md = F.getMetadata("dbg");

  if (func_md) {
    DebugLoc Loc = DebugLoc(func_md);
    func_column = Loc.getCol();
    func_line = Loc.getLine();
  }

  function.add("filepath", getSourceFileNameAbsPath(M));
  function.add("name", F.getName().str());
  function.add("line", func_line);
  function.add("column", func_column);
}

void add_loop_metadata(pt::ptree &loop, Loop *L, Module &M)
{
  static unsigned cpt = 0;

  DebugLoc loop_loc = L->getStartLoc();
  unsigned loop_line = loop_loc.getLine();
  unsigned loop_column = loop_loc.getCol();

  loop.add("filepath", getSourceFileNameAbsPath(M));
  loop.add("name", "Loop_" + std::to_string(cpt++));
  loop.add("line", loop_line);
  loop.add("column", loop_column);
}

std::string Fops2str[] = {"add", "sub", "mul", "div", "cmp", "ignore"};

void add_fops_metadata(pt::ptree &fops, Fops type, Instruction &I, Module &M)
{
  unsigned fops_line, fops_column;
  DebugLoc fops_loc = I.getDebugLoc();
  
  fops_column = fops_loc.getCol();
  fops_line = fops_loc.getLine();

  fops.add("filepath", getSourceFileNameAbsPath(M));
  fops.add("type", Fops2str[type]);
  fops.add("line", fops_line);
  fops.add("column", fops_column);

  bool use_float = false, use_double = false;

  haveFloatingPointArithmetic(I, &use_float, &use_double);

  unsigned vec_size = isVectorized(I);
  fops.add("vector_size", vec_size);

  if (use_float){
    fops.add("precision", 23);
    fops.add("range", 8);
  }else if (use_double){
    fops.add("precision", 52);
    fops.add("range", 11);
  }else{
    std::cerr << "An fops uses float and double it's strange" << std::endl;
  }
}

bool add_call_metadata(pt::ptree &call, Instruction &I, Module &M, Function *f)
{
  unsigned call_line, call_column;
  DebugLoc call_loc = I.getDebugLoc();

  call_column = call_loc.getCol();
  call_line = call_loc.getLine();

  const TargetLibraryInfo &TLI = getTLI(f);

  LibFunc libfunc;

  bool is_from_library = TLI.getLibFunc(f->getName(), libfunc);

  // Test if f is instrinsic //
  bool is_intrinsic = f->isIntrinsic();

  // Test if the function use double or float
  bool use_float, use_double;
  bool instrument = haveFloatingPointArithmetic(&I, f, &use_float, &use_double);

  call.add("filepath", getSourceFileNameAbsPath(M));
  call.add("name", cast<CallInst>(I).getCalledFunction()->getName().str());
  call.add("line", call_line);
  call.add("column", call_column);
  call.add("use_double", use_double);
  call.add("use_float", use_float);
  call.add("is_library", is_from_library);

  // If the called function is an intrinsic function that does
  // not use float or double, do not instrument it.
  return instrument && !is_intrinsic;
}


struct VfclibFunc : public ModulePass {
  static char ID;
  std::vector<Function *> OriginalFunctions;
  std::vector<Function *> ClonedFunctions;
  size_t inst_cpt;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addRequired<TargetLibraryInfoWrapperPass>();
    AU.addRequired<LoopInfoWrapperPass>();
  }

  VfclibFunc() : ModulePass(ID) { inst_cpt = 1; }

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

    static size_t loop_cpt = 0;

    /*************************************************************************
     *                  Get original functions's names                       *
     *************************************************************************/
    for (auto &F : M) {
      if ((F.getName().str() != "main") && F.size() != 0) {
        OriginalFunctions.push_back(&F);
      }
    }

    /*************************************************************************
     *                  Enter and exit functions declarations                *
     *************************************************************************/

    std::vector<Type *> ArgTypes{Int8PtrTy};

    // Signature of enter_function and exit_function
    FunctionType *FunTy =
        FunctionType::get(Type::getVoidTy(M.getContext()), ArgTypes, true);

    // void vfc_enter_function (char*, char, char, char, char, int, ...)
    func_enter = Function::Create(FunTy, Function::ExternalLinkage,
                                  "vfc_enter_function", &M);
    func_enter->setCallingConv(CallingConv::C);

    // void vfc_exit_function (char*, char, char, char, char, int, ...)
    func_exit = Function::Create(FunTy, Function::ExternalLinkage,
                                 "vfc_exit_function", &M);
    func_exit->setCallingConv(CallingConv::C);

    /*************************************************************************
     *                    Create Instrumentation Profile                     *
     *************************************************************************/

    std::map<Loop *, std::pair<pt::ptree, pt::ptree>> loops_map;
    std::map<Loop *, unsigned> loops_start;
    std::vector<Loop*> loops_stack;
    unsigned bb_cpt = 0;

    // profile tree
    pt::ptree profile;

    for (auto &F : M) {
      if (F.size() == 0)
        continue;

      // get the loop informations of the function
      LoopInfo &LI = getAnalysis<LoopInfoWrapperPass>(F).getLoopInfo();

      // function tree
      pt::ptree function, func_body;

      // add metadata
      add_function_metadata(function, F, M);

      for (auto &B : F) {
        // get the loop associated with this basic block
        Loop *L = LI.getLoopFor(&B);

        // 
        if (L != NULL && loops_map.find(L) == loops_map.end()) {
          loops_map.insert(std::pair<Loop *, std::pair<pt::ptree, pt::ptree>>(L, std::pair<pt::ptree, pt::ptree>(pt::ptree(), pt::ptree())));
          loops_start.insert(std::pair<Loop *, unsigned>(L, bb_cpt));
          loops_stack.push_back(L);

          add_loop_metadata(loops_map[L].first, L, M);
        }

        for (auto &I : B) {
          Fops ops = getFops(I);

          if (ops != FOP_IGNORE) {
            // fops tree
            pt::ptree fops;

            add_fops_metadata(fops, ops, I, M);

              if (L != NULL){
                loops_map[L].second.add_child("fops", fops);
              }else {
                func_body.add_child("fops", fops);
              }
          }

          if (isa<CallInst>(I)) {
            if (Function *f = cast<CallInst>(I).getCalledFunction()) {
              // call tree
              pt::ptree call;

              if (add_call_metadata(call, I, M, f)){
                if (L != NULL){
                  loops_map[L].second.add_child("call", call);
                }else {
                  func_body.add_child("call", call);
                }                
              }
            }
          }
        }

        bb_cpt++;

        for (auto it = loops_start.begin(); it != loops_start.end(); ){
          L = (*it).first;
          if (L->getNumBlocks() == bb_cpt - loops_start[L] && loops_map[L].second.size()) {
            loops_map[L].first.add_child("body", loops_map[L].second);
            
            if (Loop* P = L->getParentLoop()){
              loops_map[P].second.add_child("loop", loops_map[L].first);
            }else{
              func_body.add_child("loop", loops_map[L].first);
            }
            
            it = loops_start.erase(it);
            loops_map.erase(L);
          }else{
            it++;
          }     
        }
      }

      function.add_child("body", func_body);
      profile.add_child("profile.function", function);
    }

    // save instrumentation informations
    std::ofstream file("vfc_profile.xml");

    boost::property_tree::write_xml( file, profile,
          boost::property_tree::xml_writer_make_settings<std::string>(' ', 2));

    /*************************************************************************
     *                             Main special case                         *
     *************************************************************************/
    if (M.getFunction("main")) {
      Function *Main = M.getFunction("main");

      ValueToValueMapTy VMap;
      Function *Clone = CloneFunction(Main, VMap);

      DISubprogram *Sub = Main->getSubprogram();
      std::string Name = Sub->getName().str();
      std::string File = Sub->getFilename().str();
      std::string Line = std::to_string(Sub->getLine());
      std::string NewName = "vfc_" + File + "//" + Name + "/" + Line + "/" +
                            std::to_string(inst_cpt) + "_hook";
      std::string FunctionName =
          File + "//" + Name + "/" + Line + "/" + std::to_string(++inst_cpt);

      bool use_float, use_double;

      // Test if the function use double or float
      haveFloatingPointArithmetic(NULL, Main, &use_float, &use_double);

      // Delete Main Body
      Main->deleteBody();

      BasicBlock *block = BasicBlock::Create(M.getContext(), "block", Main);
      IRBuilder<> Builder(block);

      // Create function ID
      Value *FunctionID = Builder.CreateGlobalStringPtr(FunctionName);

      // Enter metadata arguments
      std::vector<Value *> MetaData{FunctionID};

      Clone->setName(NewName);

      InstrumentFunction(MetaData, Main, Clone, NULL, block, M);

      OriginalFunctions.push_back(Clone);
    }

    /*************************************************************************
     *                      Instrument Function calls                        *
     *************************************************************************/

    for (auto &F : OriginalFunctions) {
      if (F->getSubprogram()) {
        std::string Parent = F->getSubprogram()->getName().str();
        for (auto &B : (*F)) {
          IRBuilder<> Builder(&B);

          for (auto ii = B.begin(); ii != B.end();) {
            Instruction *pi = &(*ii++);

            if (isa<CallInst>(pi)) {
              // collect metadata info //
              if (Function *f = cast<CallInst>(pi)->getCalledFunction()) {

                if (MDNode *N = pi->getMetadata("dbg")) {
                  DILocation *Loc = cast<DILocation>(N);
                  DISubprogram *Sub = f->getSubprogram();
                  unsigned line = Loc->getLine();
                  std::string File = Loc->getFilename();
                  std::string Name;

                  if (Sub) {
                    Name = Sub->getName().str();
                  } else {
                    Name = f->getName().str();
                  }

                  std::string Line = std::to_string(line);
                  std::string NewName = "vfc_" + File + "/" + Parent + "/" +
                                        Name + "/" + Line + "/" +
                                        std::to_string(inst_cpt) + +"_hook";

                  std::string FunctionName = File + "/" + Parent + "/" + Name +
                                             "/" + Line + "/" +
                                             std::to_string(++inst_cpt);

                  // Test if f is instrinsic //
                  bool is_intrinsic = f->isIntrinsic();

                  // If the called function is an intrinsic function that does
                  // not use float or double, do not instrument it.
                  if (is_intrinsic) {
                    continue;
                  }

                  // Create function ID
                  Value *FunctionID =
                      Builder.CreateGlobalStringPtr(FunctionName);

                  // Enter function arguments
                  std::vector<Value *> MetaData{FunctionID};

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
    }

    return true;
  }
}; // namespace

} // namespace

char VfclibFunc::ID = 0;
static RegisterPass<VfclibFunc>
    X("vfclibfunc", "verificarlo function instrumentation pass", false, false);
