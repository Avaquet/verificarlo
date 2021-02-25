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

#include "libVFCProfile.hpp"

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ptree_fwd.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <cmath>
#include <cstring>
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
namespace pt = boost::property_tree;

namespace {

// return true if the file exists
inline bool exists(const std::string &name) {
  struct stat buffer;
  return (stat(name.c_str(), &buffer) == 0);
}

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

// return the vector size
unsigned isVectorized(Instruction &I) {
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

// Get the Name of the given argument V
std::string getArgName(Function *F, Value *V, int i) {
  if (i < 0) {
    return "return_value";
  }

  return "parameter_" + std::to_string(i + 1);
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

// return the type of floating point operation
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

// return the good TLI (be careful with the used version)
const TargetLibraryInfo &getTLI(Function *f) {
  TargetLibraryInfoWrapperPass TLIWP;

#if LLVM_VERSION_MAJOR >= 10
  return TLIWP.getTLI(*f);
#else
  return TLIWP.getTLI();
#endif
}

struct VfclibProfile : public ModulePass {
  static char ID;
  pt::ptree profile;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addRequired<TargetLibraryInfoWrapperPass>();
    AU.addRequired<LoopInfoWrapperPass>();
  }

  VfclibProfile() : ModulePass(ID) {}

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

    bool isArithmetic = getFops(I) != FOP_IGNORE;
    bool useFloat = false;

    if (mustInstrument(I.getType())) {
      useFloat = true;
    }

    for (Use *U = I.op_begin(); U != I.op_end(); U++) {
      if (mustInstrument(U->get()->getType())) {
        useFloat = true;
      }
    }

    return (isCall || isArithmetic) && useFloat;
  }

  void add_instr_metadata(pt::ptree &instr, Instruction &I, Loop *L,
                          Function &F, Module &M) {
    DebugLoc loc = I.getDebugLoc();
    std::string func_name = F.getName().str();
    std::string loop_id = (L) ? func_name +
                                    std::to_string(L->getStartLoc().getLine()) +
                                    std::to_string(L->getLoopDepth())
                              : "none";
    std::string file_name = M.getSourceFileName();
    unsigned column = loc.getCol();
    unsigned line = loc.getLine();
    unsigned depth = (L) ? L->getLoopDepth() : 0;

    instr.add("id", file_name + "/" + std::to_string(line) + "/" +
                        std::to_string(column));
    instr.add("filepath", getSourceFileNameAbsPath(M));
    instr.add("function", func_name);
    instr.add("line", line);
    instr.add("column", column);
    instr.add("loop", loop_id);
    instr.add("depth", depth);
  }

  void add_fops_metadata(pt::ptree &fops, Instruction &I, Loop *L, Function &F,
                         Module &M) {
    unsigned vec_size = isVectorized(I);
    bool use_float = false, use_double = false;
    haveFloatingPointArithmetic(I, &use_float, &use_double);
    unsigned precision = (use_float) ? 23 : 52;
    unsigned range = (use_float) ? 8 : 11;

    add_instr_metadata(fops, I, L, F, M);

    fops.add("type", getFops(I));
    fops.add("data_type", int(use_double));
    fops.add("vector_size", vec_size);
    fops.add("precision", precision);
    fops.add("range", range);
  }

  pt::ptree add_arg_metadata(Function *F, Value *V, int i) {
    pt::ptree arg;
    Type *T = V->getType();
    Ftypes type;
    unsigned precision, range, size = 1;

    if (FloatTy == T) {
      type = FLOAT;
      precision = 23;
      range = 8;
    } else if (FloatPtrTy == T) {
      type = FLOAT_PTR;
      precision = 23;
      range = 8;
      size = getSizeOf(V, F);
    } else if (DoubleTy == T) {
      type = DOUBLE;
      precision = 52;
      range = 11;
    } else if (DoublePtrTy == T) {
      type = DOUBLE_PTR;
      precision = 52;
      range = 11;
      size = getSizeOf(V, F);
    } else {
      V->getType()->print(errs());
      std::cerr << " Arg type not suported" << std::endl;
    }

    arg.add("name", getArgName(F, V, i));
    arg.add("size", size);
    arg.add("type", type);
    arg.add("precision", precision);
    arg.add("range", range);

    return arg;
  }

  void add_call_metadata(pt::ptree &call, Instruction &I, Loop *L, Function &F,
                         Module &M) {
    LibFunc libfunc;
    const TargetLibraryInfo &TLI = getTLI(&F);
    bool is_from_library = TLI.getLibFunc(F.getName(), libfunc);
    std::string library_name =
        (is_from_library) ? TLI.getName(libfunc).str() : "none";
    unsigned input_cpt = 0, output_cpt = 0;
    std::vector<pt::ptree> input_args, output_args;

    if (mustInstrument(I.getType())) {
      output_cpt++;
      output_args.push_back(add_arg_metadata(&F, &I, -1));
    }

    int i = 0;
    for (Use *U = I.op_begin(); U != I.op_end(); U++, i++) {
      Type *arg_type = U->get()->getType();

      if (mustInstrument(arg_type)) {
        pt::ptree arg = add_arg_metadata(&F, U->get(), i);

        if (arg_type == DoublePtrTy || arg_type == FloatPtrTy) {
          output_cpt++;
          output_args.push_back(arg);
        }

        input_cpt++;
        input_args.push_back(arg);
      }
    }

    add_instr_metadata(call, I, L, F, M);

    call.add("name", cast<CallInst>(I).getCalledFunction()->getName().str());
    call.add("library", library_name);

    call.add("nb_input", input_cpt);
    for (auto &input : input_args)
      call.add_child("input", input);

    call.add("nb_output", output_cpt);
    for (auto &output : output_args)
      call.add_child("output", output);
  }

  virtual bool runOnModule(Module &M) {
    FloatTy = Type::getFloatTy(M.getContext());
    FloatPtrTy = Type::getFloatPtrTy(M.getContext());
    DoubleTy = Type::getDoubleTy(M.getContext());
    DoublePtrTy = Type::getDoublePtrTy(M.getContext());

    /*************************************************************************
     *                    Create Instrumentation Profile                     *
     *************************************************************************/

    for (auto &F : M) {
      if (F.size() == 0)
        continue;

      LoopInfo &LI = getAnalysis<LoopInfoWrapperPass>(F).getLoopInfo();

      for (auto &B : F) {
        Loop *L = LI.getLoopFor(&B);

        for (auto &I : B) {
          if (mustInstrument(I)) {
            pt::ptree instruction;

            if (isa<CallInst>(I)) {
              add_call_metadata(instruction, I, L, F, M);
              profile.add_child("call", instruction);
            } else {
              add_fops_metadata(instruction, I, L, F, M);
              profile.add_child("fops", instruction);
            }
          }
        }
      }
    }

    pt::write_xml(
        std::cout, profile,
        boost::property_tree::xml_writer_make_settings<std::string>('\t', 1));

    return false;
  }
}; // namespace

} // namespace

char VfclibProfile::ID = 0;
static RegisterPass<VfclibProfile> X("vfclibprofile",
                                     "verificarlo profile pass", false, false);
