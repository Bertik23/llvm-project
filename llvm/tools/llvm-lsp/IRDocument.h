//===-- IRDocument.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_LSP_IRDOCUMENT_H
#define LLVM_TOOLS_LLVM_LSP_IRDOCUMENT_H

#include "lsp-server-support/Logging.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/AsmParser/AsmParserContext.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/SourceMgr.h"

#include <memory>
#include <string>

namespace {

class IRDocumentHelpers {
public:
  static std::optional<std::string>
  basicBlockIdFormatter(const llvm::BasicBlock *BB,
                        const llvm::AsmParserContext &ParserContext) {
    return ParserContext.getBlockLocation(BB).transform([](const auto &Loc) {
      return llvm::formatv("range_{0}_{1}_{2}_{3}", Loc.Start.Line,
                           Loc.Start.Col, Loc.End.Line, Loc.End.Col);
    });
  }

  static std::optional<llvm::FileLocRange>
  basicBlockIdParser(std::string BBId) {
    unsigned StartLine, StartCol, EndLine, EndCol;
    auto [part0, rest0] = llvm::StringRef{BBId}.split('_');
    if (part0 != "range")
      return std::nullopt;
    auto [part1, rest1] = rest0.split('_');
    if (part1.getAsInteger(10, StartLine))
      return std::nullopt;
    auto [part2, rest2] = rest1.split('_');
    if (part2.getAsInteger(10, StartCol))
      return std::nullopt;
    auto [part3, rest3] = rest2.split('_');
    if (part3.getAsInteger(10, EndLine))
      return std::nullopt;
    if (rest3.contains('_') || rest3.getAsInteger(10, EndCol))
      return std::nullopt;
    if (part1.empty() || part2.empty() || part3.empty() || rest3.empty())
      return std::nullopt;
    return llvm::FileLocRange{llvm::FileLoc{StartLine, StartCol},
                              llvm::FileLoc{EndLine, EndCol}};
  }
};

} // namespace

namespace llvm {
// LSP Server will use this class to query details about the IR file.
// FIXME: For the moment we assume that we can only run "default<O3>" on the IR.
class IRDocument {
  LLVMContext C;
  std::unique_ptr<Module> ParsedModule;
  StringRef Filepath;

public:
  IRDocument(StringRef PathToIRFile) : Filepath(PathToIRFile) {
    ParsedModule = loadModuleFromIR(PathToIRFile, C);
    lsp::Logger::info("Finished setting up IR Document: {}",
                      PathToIRFile.str());
  }

  // ---------------- APIs that the Language Server can use  -----------------

  std::string getNodeId(const BasicBlock *BB) {
    if (auto Id = IRDocumentHelpers::basicBlockIdFormatter(BB, ParserContext))
      return *Id;
    return "";
  }

  FileLocRange parseNodeId(std::string BBId) {
    if (auto FLR = IRDocumentHelpers::basicBlockIdParser(BBId))
      return *FLR;
    return FileLocRange{};
  }

  Function *getFirstFunction() {
    return &ParsedModule->getFunctionList().front();
  }

  auto &getFunctions() { return ParsedModule->getFunctionList(); }

  Function *getFunctionAtLocation(unsigned Line, unsigned Col) {
    FileLoc FL(Line, Col);
    if (auto MaybeF = ParserContext.getFunctionAtLocation(FL))
      return MaybeF.value();
    return nullptr;
  }

  BasicBlock *getBlockAtLocation(unsigned Line, unsigned Col) {
    FileLoc FL(Line, Col);
    if (auto MaybeBB = ParserContext.getBlockAtLocation(FL))
      return MaybeBB.value();
    return nullptr;
  }

  Instruction *getInstructionAtLocation(unsigned Line, unsigned Col) {
    FileLoc FL(Line, Col);
    if (auto MaybeI = ParserContext.getInstructionAtLocation(FL))
      return MaybeI.value();
    return nullptr;
  }

  AsmParserContext ParserContext;

private:
  std::unique_ptr<Module> loadModuleFromIR(StringRef Filepath, LLVMContext &C) {
    SMDiagnostic Err;
    // Try to parse as textual IR
    auto M = parseIRFile(Filepath, Err, C, {}, &ParserContext);
    if (!M)
      // If parsing failed, print the error and crash
      lsp::Logger::error("Failed parsing IR file: {}", Err.getMessage().str());
    return M;
  }
};

} // namespace llvm

#endif // LLVM_TOOLS_LLVM_LSP_IRDOCUMENT_H
