#ifndef LLVM_ASMPARSER_ASMPARSER_STATE_H
#define LLVM_ASMPARSER_ASMPARSER_STATE_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/Value.h"
#include <optional>

namespace llvm {
class AsmParserState {
public:
  std::optional<FileLocRange> getFunctionLocation(const Function *) const;
  std::optional<FileLocRange> getBlockLocation(const BasicBlock *) const;
  std::optional<FileLocRange> getInstructionLocation(const Instruction *) const;
  std::optional<Function *> getFunctionAtLocation(const FileLocRange &) const;
  std::optional<Function *> getFunctionAtLocation(const FileLoc &) const;
  std::optional<BasicBlock *> getBlockAtLocation(const FileLocRange &) const;
  std::optional<BasicBlock *> getBlockAtLocation(const FileLoc &) const;
  std::optional<Instruction *>
  getInstructionAtLocation(const FileLocRange &) const;
  std::optional<Instruction *> getInstructionAtLocation(const FileLoc &) const;
  bool addFunctionLocation(Function *, const FileLocRange &);
  bool addBlockLocation(BasicBlock *, const FileLocRange &);
  bool addInstructionLocation(Instruction *, const FileLocRange &);

private:
  DenseMap<Function *, FileLocRange> Functions;
  DenseMap<BasicBlock *, FileLocRange> Blocks;
  DenseMap<Instruction *, FileLocRange> Instructions;
};
} // namespace llvm

#endif
