//===-- AsmParserContext.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ASMPARSER_ASMPARSER_STATE_H
#define LLVM_ASMPARSER_ASMPARSER_STATE_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/Value.h"
#include <optional>

namespace llvm {

template <> struct DenseMapInfo<FileLocRange> {
  static constexpr FileLocRange getEmptyKey() {
    return FileLocRange(FileLoc(-1, -1), FileLoc(-1, -1));
  }
  static constexpr FileLocRange getTombstoneKey() {
    return FileLocRange(FileLoc(-2, -2), FileLoc(-2, -2));
  }
  static unsigned getHashValue(const FileLocRange &Val) {
    return (Val.Start.Line * 31) ^ (Val.Start.Col * 37) ^ (Val.End.Line * 41) ^
           (Val.End.Col * 43);
  }
  static bool isEqual(const FileLocRange &LHS, const FileLocRange &RHS) {
    return LHS.contains(RHS) && RHS.contains(LHS);
  }
};

/// Registry of file location information for LLVM IR constructs
///
/// This class provides access to the file location information
/// for various LLVM IR constructs. Currently, it supports Function,
/// BasicBlock and Instruction locations.
///
/// When available, it can answer queries about what is at a given
/// file location, as well as where in a file a given IR construct
/// is.
/// 
/// This information is optionally emitted by the LLParser while
/// it reads LLVM textual IR.
class AsmParserContext {
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

  DenseMap<FileLocRange, Value *> LocRangeValueMap;

private:
  DenseMap<Function *, FileLocRange> Functions;
  DenseMap<BasicBlock *, FileLocRange> Blocks;
  DenseMap<Instruction *, FileLocRange> Instructions;
};
} // namespace llvm

#endif
