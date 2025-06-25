#ifndef LLVM_ASMPARSER_ASMPARSER_STATE_H
#define LLVM_ASMPARSER_ASMPARSER_STATE_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/Support/SMLoc.h"
#include "llvm/IR/Value.h"
#include <cstddef>

namespace llvm {
struct AsmParserState {
  DenseMap<Function*, FileLocRange> functions;
  DenseMap<BasicBlock*, FileLocRange> blocks;
  DenseMap<Instruction*, FileLocRange> instructions;
};
} // namespace llvm

#endif