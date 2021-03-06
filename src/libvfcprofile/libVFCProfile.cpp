/*****************************************************************************
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
#include <memory>
#include <regex>
#include <set>
#include <stdio.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>

using namespace llvm;
namespace pt = boost::property_tree;

static cl::opt<std::string>
    VfclibInstFunction("vfclibinst-function",
                       cl::desc("Only instrument given FunctionName"),
                       cl::value_desc("FunctionName"), cl::init(""));

static cl::opt<std::string> VfclibInstIncludeFile(
    "vfclibinst-include-file",
    cl::desc("Only instrument modules / functions in file IncludeNameFile "),
    cl::value_desc("IncludeNameFile"), cl::init(""));

static cl::opt<std::string> VfclibInstExcludeFile(
    "vfclibinst-exclude-file",
    cl::desc("Do not instrument modules / functions in file ExcludeNameFile "),
    cl::value_desc("ExcludeNameFile"), cl::init(""));

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

  return "operand_" + std::to_string(i + 1);
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

void add_instr_metadata(unsigned &instr_cpt, pt::ptree &instr, Instruction &I,
                        Loop *L, Function &F, Module &M) {
  DebugLoc loc = I.getDebugLoc();

  std::string func_name = F.getName().str();
  std::string loop_id = (L) ? func_name +
                                  std::to_string(L->getStartLoc().getLine()) +
                                  std::to_string(L->getLoopDepth())
                            : "none";

  std::string file_name = M.getSourceFileName();

  unsigned column = (loc) ? loc.getCol() : 0;
  unsigned line = (loc) ? loc.getLine() : 0;
  unsigned depth = (L) ? L->getLoopDepth() : 0;

  std::string id =
      file_name + "/" + func_name + "/" + std::to_string(instr_cpt++);

  LLVMContext &C = I.getContext();
  MDNode *N = MDNode::get(C, MDString::get(C, id));
  I.setMetadata("VFC_PROFILE_NAME", N);

  instr.add("id", id);
  instr.add("filepath", getSourceFileNameAbsPath(M));
  instr.add("function", func_name);
  instr.add("line", line);
  instr.add("column", column);
  instr.add("loop", loop_id);
  instr.add("depth", depth);
}

pt::ptree add_arg_metadata(Function *F, Value *V, int i) {
  pt::ptree arg;
  Type *T = (V->getType()->isVectorTy())
                ? static_cast<VectorType *>(V->getType())->getElementType()
                : V->getType();
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
  arg.add("data_type", type);
  arg.add("precision", precision);
  arg.add("range", range);

  return arg;
}

void add_fops_metadata(unsigned &instr_cpt, pt::ptree &fops, Instruction &I,
                       Loop *L, Function &F, Module &M) {
  unsigned vec_size = isVectorized(I);
  bool use_float = false, use_double = false;
  haveFloatingPointArithmetic(I, &use_float, &use_double);
  unsigned precision = (use_float) ? 23 : 52;
  unsigned range = (use_float) ? 8 : 11;

  add_instr_metadata(instr_cpt, fops, I, L, F, M);

  fops.add("type", getFops(I));
  fops.add("data_type", int(use_double));
  fops.add("vector_size", vec_size);

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

  fops.add("nb_input", input_cpt);
  for (auto &input : input_args)
    fops.add_child("input", input);

  fops.add("nb_output", output_cpt);
  for (auto &output : output_args)
    fops.add_child("output", output);
}

void add_call_metadata(unsigned &instr_cpt, pt::ptree &call, Instruction &I,
                       Loop *L, Function &F, Module &M) {
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

  add_instr_metadata(instr_cpt, call, I, L, F, M);

  call.add("name", cast<CallInst>(I).getCalledFunction()->getName().str());
  call.add("library", library_name);

  call.add("nb_input", input_cpt);
  for (auto &input : input_args)
    call.add_child("input", input);

  call.add("nb_output", output_cpt);
  for (auto &output : output_args)
    call.add_child("output", output);
}

// https://thispointer.com/find-and-replace-all-occurrences-of-a-sub-string-in-c/
void findAndReplaceAll(std::string &data, std::string toSearch,
                       std::string replaceStr) {
  // Get the first occurrence
  size_t pos = data.find(toSearch);
  // Repeat till end is reached
  while (pos != std::string::npos) {
    // Replace this occurrence of Sub String
    data.replace(pos, toSearch.size(), replaceStr);
    // Get the next occurrence from the current position
    pos = data.find(toSearch, pos + replaceStr.size());
  }
}

void escape_regex(std::string &str) {
  findAndReplaceAll(str, ".", "\\.");
  // ECMAScript needs .* instead of * for matching any charactere
  // http://www.cplusplus.com/reference/regex/ECMAScript/
  findAndReplaceAll(str, "*", ".*");
}

std::regex parseFunctionSetFile(Module &M, cl::opt<std::string> &fileName) {
  // Skip if empty fileName
  if (fileName.empty()) {
    return std::regex("");
  }

  // Open File
  std::ifstream loopstream(fileName.c_str());
  if (!loopstream.is_open()) {
    errs() << "Cannot open " << fileName << "\n";
    report_fatal_error("libVFCInstrument fatal error");
  }

  // Parse File, if module name matches, add function to FunctionSet
  int lineno = 0;
  std::string line;

  // return the absolute path of the source file
  std::string moduleName = getSourceFileNameAbsPath(M);
  moduleName = (moduleName.empty()) ? M.getModuleIdentifier() : moduleName;

  // Regex that contains all regex for each function
  std::string moduleRegex = "";

  while (std::getline(loopstream, line)) {
    lineno++;
    StringRef l = StringRef(line);

    // Ignore empty or commented lines
    if (l.startswith("#") || l.trim() == "") {
      continue;
    }
    std::pair<StringRef, StringRef> p = l.split(" ");

    if (p.second.equals("")) {
      errs() << "Syntax error in exclusion/inclusion file " << fileName << ":"
             << lineno << "\n";
      report_fatal_error("libVFCInstrument fatal error");
    } else {
      std::string mod = p.first.trim();
      std::string fun = p.second.trim();

      // If mod is not an absolute path,
      // we search any module containing mod
      if (sys::path::is_relative(mod)) {
        mod = "*" + sys::path::get_separator().str() + mod;
      }
      // If the user does not specify extension for the module
      // we match any extension
      if (not sys::path::has_extension(mod)) {
        mod += ".*";
      }

      escape_regex(mod);
      escape_regex(fun);

      if (std::regex_match(moduleName, std::regex(mod))) {
        moduleRegex += fun + "|";
      }
    }
  }

  loopstream.close();
  // Remove the extra | at the end
  if (not moduleRegex.empty()) {
    moduleRegex.pop_back();
  }
  return std::regex(moduleRegex);
}

bool isIncluded(Instruction &I, std::regex &includeFunctionRgx,
                std::regex &excludeFunctionRgx) {
  std::string func_name;
  std::string called_name;

  if (isa<CallInst>(&I)) {
    CallInst *call = cast<CallInst>(&I);
    called_name = call->getCalledFunction()->getName().str();
  }

  func_name = I.getFunction()->getName().str();

  // Included-list
  if ((!called_name.empty() &&
       std::regex_match(called_name, includeFunctionRgx)) ||
      std::regex_match(func_name, includeFunctionRgx)) {
    return true;
  }

  // Excluded-list
  if ((!called_name.empty() &&
       std::regex_match(called_name, excludeFunctionRgx)) ||
      std::regex_match(func_name, excludeFunctionRgx)) {
    return false;
  }

  // If excluded-list is empty and included-list is not, we are done
  if (VfclibInstExcludeFile.empty() and not VfclibInstIncludeFile.empty()) {
    return false;
  }

  return true;
}

std::string getInstID(Instruction &I) {
  return I.getFunction()->getName().str() + "_" + I.getOpcodeName() + "_" +
         std::to_string(I.getDebugLoc().getLine()) + "_" +
         std::to_string((unsigned long int)(void *)&I);
}

std::pair<Instruction *, Instruction *>
depends(Instruction &I, Instruction &II,
        std::pair<std::vector<Instruction *>, std::vector<Instruction *>>
            &memInstAndFops) {
  for (auto U : II.users()) {
    if (U == &I) {
      if (II.getOpcode() == Instruction::Load ||
          II.getOpcode() == Instruction::Store) {
        memInstAndFops.first.push_back(&II);
        memInstAndFops.second.push_back(&I);
      } else {
        return std::pair<Instruction *, Instruction *>{&II, &I};
      }
    }
  }

  for (auto U : I.users()) {
    if (U == &II) {
      if (II.getOpcode() == Instruction::Load ||
          II.getOpcode() == Instruction::Store) {
        memInstAndFops.first.push_back(&II);
        memInstAndFops.second.push_back(&I);
      } else {
        return std::pair<Instruction *, Instruction *>{&I, &II};
      }
    }
  }

  return std::pair<Instruction *, Instruction *>{};
}

std::vector<Instruction *> getSourceInstructions(
    Function *F, Instruction *I, BasicBlock *OriginalBB, BasicBlock *BB,
    std::pair<std::vector<Instruction *>, std::vector<Instruction *>>
        memInstAndFops) {
  // instructions to return
  std::vector<Instruction *> to_return;
  // vector of memoryInst
  auto memInst = memInstAndFops.first;
  // vector of floating point operations
  auto fops = memInstAndFops.second;
  // get an iterator on the instruction in the basic block
  auto it = BB->end();
  // initial reseach flag
  bool flag = false;

  /*
  I->print(errs());
  std::cerr << std::endl;
  */

  if (OriginalBB == NULL) {
    flag = true;
    it = BB->begin();
    while (it != BB->end() && &(*it) != I) {
      it++;
    }
  }

  do {
    it--;

    if (isa<StoreInst>(*it) && isa<LoadInst>(I)) {
      StoreInst *SI = cast<StoreInst>(&(*it));
      LoadInst *LI = cast<LoadInst>(I);
      auto mem_it = std::find(memInst.begin(), memInst.end(), &(*it));

      if (SI->getPointerOperand() == LI->getPointerOperand()) {
        if (mem_it != memInst.end()) {
          unsigned i = mem_it - memInst.begin();
          to_return.push_back(fops[i]);
        } else {
          /*
          std::cerr << "    Store Inst -> ";
          it->print(errs());
          std::cerr << std::endl;
          */
          auto to_merge =
              getSourceInstructions(F, &(*it), OriginalBB, BB, memInstAndFops);
          to_return.insert(to_return.end(), to_merge.begin(), to_merge.end());
        }
      }
    } else if (isa<LoadInst>(*it) && isa<StoreInst>(I)) {
      LoadInst *LI = cast<LoadInst>(&(*it));
      StoreInst *SI = cast<StoreInst>(I);
      auto mem_it = std::find(memInst.begin(), memInst.end(), &(*it));

      if (SI->getValueOperand() == cast<Value>(LI)) {
        if (mem_it != memInst.end()) {
          unsigned i = mem_it - memInst.begin();
          to_return.push_back(fops[i]);
        } else {
          /*
          std::cerr << "    Load Inst -> ";
          it->print(errs());
          std::cerr << std::endl;
          */
          auto to_merge =
              getSourceInstructions(F, &(*it), OriginalBB, BB, memInstAndFops);
          to_return.insert(to_return.end(), to_merge.begin(), to_merge.end());
        }
      }
    } else if (isa<GetElementPtrInst>(*it) && isa<LoadInst>(I)) {
      GetElementPtrInst *GEP = cast<GetElementPtrInst>(&(*it));
      LoadInst *LI = cast<LoadInst>(I);
      /* To complete */
    }
    /*
    else{
      std::cerr << "  Other ->";
      it->print(errs());
      std::cerr << std::endl;
    }
    */
  } while (it != BB->begin());

  if (flag) {
    OriginalBB = BB;
  } else if (OriginalBB == BB) {
    return to_return;
  }

  for (auto iit = pred_begin(BB); iit != pred_end(BB); ++iit) {
    auto to_merge =
        getSourceInstructions(F, I, OriginalBB, (*iit), memInstAndFops);
    to_return.insert(to_return.end(), to_merge.begin(), to_merge.end());
  }

  return to_return;
}

struct VfclibProfile : public ModulePass {
  static char ID;
  pt::ptree profile;
  unsigned instr_cpt;
  std::map<Instruction *, Value *> InstrToStruct;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addRequired<TargetLibraryInfoWrapperPass>();
    AU.addRequired<LoopInfoWrapperPass>();
    AU.addRequired<MemorySSAWrapperPass>();
  }

  VfclibProfile() : ModulePass(ID) { instr_cpt = 0; }

  virtual bool runOnModule(Module &M) {
    // initialize common types
    FloatTy = Type::getFloatTy(M.getContext());
    FloatPtrTy = Type::getFloatPtrTy(M.getContext());
    DoubleTy = Type::getDoubleTy(M.getContext());
    DoublePtrTy = Type::getDoublePtrTy(M.getContext());

    // Parse both included and excluded function set
    std::regex includeFunctionRgx =
        parseFunctionSetFile(M, VfclibInstIncludeFile);
    std::regex excludeFunctionRgx =
        parseFunctionSetFile(M, VfclibInstExcludeFile);

    // Parse instrument single function option (--function)
    if (not VfclibInstFunction.empty()) {
      includeFunctionRgx = std::regex(VfclibInstFunction);
      excludeFunctionRgx = std::regex(".*");
    }

    /*************************************************************************
     *                    Create Instrumentation Profile                     *
     *************************************************************************/

    for (auto &F : M) {
      if (F.size() == 0)
        continue;

      std::ofstream dotFile;
      std::string func_name = F.getName().str();
      std::string mod_name = M.getSourceFileName();
      dotFile.open(mod_name.substr(0, mod_name.find(".")) + "_" + func_name +
                   ".dot");

      dotFile << "strict digraph {" << std::endl;

      LoopInfo &LI = getAnalysis<LoopInfoWrapperPass>(F).getLoopInfo();
      MemorySSA &MSSA = getAnalysis<MemorySSAWrapperPass>(F).getMSSA();
      std::pair<std::vector<Instruction *>, std::vector<Instruction *>>
          memInstAndFops;

      instr_cpt = 0;
      for (auto &B : F) {
        Loop *L = LI.getLoopFor(&B);

        for (auto &I : B) {
          // if the instruction is a call to an included function
          // or if the instruction is in an included function
          if (!I.getDebugLoc()) {
            continue;
          }

          if (mustInstrument(I) &&
              isIncluded(I, includeFunctionRgx, excludeFunctionRgx)) {
            pt::ptree instruction;

            for (auto &BB : F) {
              for (auto &&II : BB) {
                if (&II == &I || (!II.getDebugLoc())) {
                  continue;
                }
                auto pair = depends(I, II, memInstAndFops);
                if (pair.first && pair.second) {
                  dotFile << "  " << getInstID(*(pair.first)) << " -> "
                          << getInstID(*(pair.second)) << ";" << std::endl;
                }
              }
            }

            dotFile << "  " << getInstID(I) << " [style=filled fillcolor=blue];"
                    << std::endl;

            if (isa<CallInst>(I)) {
              add_call_metadata(instr_cpt, instruction, I, L, F, M);
              profile.add_child("call", instruction);
            } else {
              add_fops_metadata(instr_cpt, instruction, I, L, F, M);
              profile.add_child("fops", instruction);
            }
          }
        }
      }

      for (int i = 0; i < memInstAndFops.first.size(); i++) {
        Instruction *memoryInst = memInstAndFops.first[i];
        Instruction *I = memInstAndFops.second[i];

        std::vector<Instruction *> Instructions = getSourceInstructions(
            &F, memoryInst, NULL, memoryInst->getParent(), memInstAndFops);
        for (auto &II : Instructions) {
          if (II != I) {
            dotFile << "  " << getInstID(*II) << " -> " << getInstID(*I) << ";"
                    << std::endl;
          }
        }
      }

      dotFile << "}" << std::endl;

      dotFile.close();
    }

    pt::write_xml(
        std::cout, profile,
        boost::property_tree::xml_writer_make_settings<std::string>('\t', 1));

    return true;
  }
}; // namespace

} // namespace

char VfclibProfile::ID = 0;
static RegisterPass<VfclibProfile> X("vfclibprofile",
                                     "verificarlo profile pass", false, false);
