#ifndef LLVM_ASMPARSER_ASMPARSER_STATE_H
#define LLVM_ASMPARSER_ASMPARSER_STATE_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/Value.h"

namespace llvm {
struct AsmParserState {
  DenseMap<Function *, FileLocRange> Functions;
  DenseMap<BasicBlock *, FileLocRange> Blocks;
  DenseMap<Instruction *, FileLocRange> Instructions;
};
} // namespace llvm

#endif
