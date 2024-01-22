#ifndef __SIM_LLVM_HH__
#define __SIM_LLVM_HH__

#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Support/MemoryBuffer.h"

#include "llvm/IR/AssemblyAnnotationWriter.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/ExecutionEngine/MCJIT.h"
#include "llvm/ExecutionEngine/GenericValue.h"
#include "llvm/ExecutionEngine/Interpreter.h"
#include "llvm/ExecutionEngine/JITEventListener.h"

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/Support/FormattedStream.h"

#include "llvm/Target/TargetOptions.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/MDBuilder.h"

#include "llvm/IR/InlineAsm.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Support/DynamicLibrary.h"

#define MakeGEP(PTR, IDX) CreateGEP((PTR)->getType()->getPointerElementType(), (PTR), (IDX))
#define MakeLoad(PTR, NAME) CreateLoad((PTR)->getType()->getPointerElementType(), (PTR), (NAME))

#endif

