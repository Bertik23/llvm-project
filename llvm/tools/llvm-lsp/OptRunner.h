//===-- OptRunner.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_LSP_OPTRUNNER_H
#define LLVM_TOOLS_LLVM_LSP_OPTRUNNER_H

#include "Protocol.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Analysis/CGSCCPassManager.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/LSP/Logging.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Program.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include <algorithm>
#include <memory>
#include <string>

namespace llvm {

// FIXME: Maybe a better name?
class OptRunner {
  LLVMContext Context;
  const Module &InitialIR;
  const StringRef File;

  SmallVector<std::unique_ptr<Module>, 256> IntermediateIRList;

public:
  OptRunner(Module &IIR, StringRef File) : InitialIR(IIR), File(File) {}

  llvm::Expected<SmallVector<std::pair<std::string, std::string>, 256>>
  getPassListAndDescriptionAPI(const std::string PipelineText) {
    // First is Passname, Second is Pass Description.
    SmallVector<std::pair<std::string, std::string>, 256>
        PassListAndDescription;
    unsigned PassNumber = 0;
    // FIXME: Should we only consider passes that modify the IR?
    std::function<void(const StringRef, Any, const PreservedAnalyses)>
        RecordPassNamesAndDescription = [&PassListAndDescription, &PassNumber](
                                            const StringRef PassName, Any IR,
                                            const PreservedAnalyses &PA) {
          PassNumber++;
          std::string PassNameStr =
              (std::to_string(PassNumber) + "-" + PassName.str());
          std::string PassDescStr = [&IR, &PassName]() -> std::string {
            if (auto *M = any_cast<const Module *>(&IR))
              return "Module Pass on \"" + (**M).getName().str() + "\"";
            if (auto *F = any_cast<const Function *>(&IR))
              return "Function Pass on \"" + (**F).getName().str() + "\"";
            if (auto *L = any_cast<const Loop *>(&IR)) {
              Function *F = (*L)->getHeader()->getParent();
              std::string Desc = "Loop Pass in Function \"" +
                                 F->getName().str() +
                                 "\" on loop with Header \"" +
                                 (*L)->getHeader()->getName().str() + "\"";
              return Desc;
            }
            if (auto *SCC = any_cast<const LazyCallGraph::SCC *>(&IR)) {
              Function &F = (**SCC).begin()->getFunction();
              std::string Desc =
                  "CGSCC Pass on Function \"" + F.getName().str() + "\"";
              return Desc;
            }
            lsp::Logger::error("Unknown Pass Type \"{}\"!", PassName.str());
            return "";
          }();

          PassListAndDescription.push_back({PassNameStr, PassDescStr});
        };

    auto RunOptResult = runOpt(PipelineText, RecordPassNamesAndDescription);
    if (!RunOptResult) {
      lsp::Logger::info("Handling error in getPassListAndDescription()");
      return RunOptResult.takeError();
    }
    return PassListAndDescription;
  }
  llvm::Expected<SmallVector<std::pair<std::string, std::string>, 256>>
  getPassListAndDescription(const std::string PipelineText) {
    // First is Passname, Second is Pass Description.
    auto MaybeOutErr =
        runShellOpt({"-S", "--print-pass-numbers", "--passes", PipelineText});
    if (!MaybeOutErr)
      return MaybeOutErr.takeError();
    auto [_, Stderr] = *MaybeOutErr;
    SmallString<1024> StderrContent;
    auto MaybeStderrFD = llvm::sys::fs::openNativeFileForRead(Stderr);
    if (!MaybeStderrFD) {
      lsp::Logger::error("Can't open error file from opt.");
      return MaybeStderrFD.takeError();
    }
    auto Res =
        llvm::sys::fs::readNativeFileToEOF(*MaybeStderrFD, StderrContent);

    if (Res)
      return Res;

    SmallVector<std::pair<std::string, std::string>, 256>
        PassListAndDescription;
    // lsp::Logger::debug("Starting to parse {}", StderrContent);
    StringRef ContentRef = StderrContent;
    int Iteration = 0;
    while (!(ContentRef.empty() || ContentRef == "\n") && Iteration <= 150) {
      ++Iteration;
      // lsp::Logger::debug("Parsing line {}", ContentRef);
      auto NumberPlus =
          ContentRef.drop_while([](char C) { return !isDigit(C); });
      // lsp::Logger::debug("Number found. {}", NumberPlus);
      auto Number = NumberPlus.take_while(isDigit);
      // lsp::Logger::debug("Number isolated. {}", Number);
      auto AfterNumber = NumberPlus.drop_while(
          [](char C) { return isDigit(C) || isSpace(C); });
      auto Description =
          AfterNumber.take_while([](char C) { return C != '\n'; });
      auto Next = AfterNumber.drop_while([](char C) { return C != '\n'; });
      // lsp::Logger::debug("Rest isolated. {}", AfterNumber);

      PassListAndDescription.emplace_back(Number.str(), Description.str());
      // lsp::Logger::debug("Emplaced");
      ContentRef = Next;
      // lsp::Logger::debug("Reset");
    }
    return PassListAndDescription;
  }

  llvm::Expected<std::unique_ptr<Module>>
  runOpt(const std::string PipelineText,
         std::function<void(const StringRef, Any, const PreservedAnalyses)>
             &AfterPassCallback) {
    // Analysis Managers
    LoopAnalysisManager LAM;
    FunctionAnalysisManager FAM;
    CGSCCAnalysisManager CGAM;
    ModuleAnalysisManager MAM;

    PassInstrumentationCallbacks PIC;

    ModulePassManager MPM;
    PassBuilder PB;

    // Callback that redirects to a custom callback.
    PIC.registerAfterPassCallback(AfterPassCallback);

    PB = PassBuilder(nullptr, PipelineTuningOptions(), std::nullopt, &PIC);
    PB.registerModuleAnalyses(MAM);
    PB.registerCGSCCAnalyses(CGAM);
    PB.registerFunctionAnalyses(FAM);
    PB.registerLoopAnalyses(LAM);
    PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

    // Parse Pipeline text
    auto ParseError = PB.parsePassPipeline(MPM, PipelineText);
    if (ParseError) {
      lsp::Logger::info("Error parsing pipeline text!");
      return llvm::createStringError(toString(std::move(ParseError)).c_str());
    }

    // Run Opt on a copy of the original IR, so that we dont modify the original
    // IR.
    auto FinalIR = CloneModule(InitialIR);
    MPM.run(*FinalIR, MAM);
    return FinalIR;
  }

  llvm::Expected<std::pair<SmallString<32>, SmallString<32>>>
  runShellOpt(std::vector<std::string> Args) {
    auto Opt = llvm::sys::findProgramByName("opt");
    if (!Opt)
      return llvm::make_error<StringError>(Opt.getError(), "opt not found");

    auto OptStr = *Opt;
    SmallString<32> /*std::string*/ Stdout; // = "/tmp/stderr";
    SmallString<32> /*std::string*/ Stderr; // = "/tmp/stdout";
    llvm::sys::fs::createTemporaryFile("llvm-lsp-stdout", "ll", Stdout);
    llvm::sys::fs::createTemporaryFile("llvm-lsp-stderr", "ll", Stderr);

    // llvm::sys::fs::createUniquePath("llvm-lsp-stdout-%.ll", Stdout, true);
    // llvm::sys::fs::createUniquePath("llvm-lsp-stderr-%.ll", Stderr, true);

    // llvm::sys::fs::remove(Stdout);
    // llvm::sys::fs::remove(Stderr);

    std::optional<StringRef> Redirects[] = {std::nullopt, StringRef(Stdout),
                                            StringRef(Stderr)};
    // std::optional<StringRef> Redirects[] = {std::nullopt, std::nullopt,
    //                                         std::nullopt};

    std::vector<StringRef> AllArgs;
    for (const auto &Arg : Args)
      AllArgs.emplace_back(Arg);

    AllArgs.emplace_back(File);

    lsp::Logger::debug("Trying to run opt with these options:");
    for (const auto &Arg : AllArgs) {
      lsp::Logger::debug("{}", Arg);
    }
    lsp::Logger::debug("Shell command: {} {}", OptStr,
                       llvm::join(AllArgs, " "));

    lsp::Logger::debug("Output files:\n\tStdout: {}\n\tStderr: {}", Stdout,
                       Stderr);

    auto ExitCode =
        llvm::sys::ExecuteAndWait(OptStr, AllArgs, std::nullopt, Redirects);
    llvm::sys::fs::file_status S;
    llvm::sys::fs::status(Stderr, S);
    lsp::Logger::debug("stderr size: {}", S.getSize());
    lsp::Logger::debug("Opt run done. ExitCode: {}", ExitCode);
    if (ExitCode) {
      SmallString<128> StderrContent;
      auto MaybeStderrFD = llvm::sys::fs::openNativeFileForRead(Stderr);
      if (!MaybeStderrFD) {
        lsp::Logger::error("Can't open error file from opt.");
        return MaybeStderrFD.takeError();
      }
      auto Res =
          llvm::sys::fs::readNativeFileToEOF(*MaybeStderrFD, StderrContent);
      if (Res) {
        lsp::Logger::error("Can't open error file from opt.");
        return Res;
      }
      lsp::Logger::error("Opt error: {}", StderrContent);
      return llvm::createStringError("Running opt failed.");
    }
    lsp::Logger::debug("Opt run handle done");
    return std::make_pair(Stdout, Stderr);
  }

  // TODO: Check if N lies with in bounds for below methods. And to verify that
  // they are populated.
  // N is 1-Indexed
  llvm::Expected<std::unique_ptr<Module>>
  getModuleAfterPass(const std::string PipelineText, unsigned N) {
    auto MaybeOutErr =
        runShellOpt({"-S", "--print-before-pass-number", std::to_string(N),
                     "--passes", PipelineText});
    if (!MaybeOutErr)
      return MaybeOutErr.takeError();
    auto [_, Stderr] = *MaybeOutErr;

    SMDiagnostic Err;
    // Try to parse as textual IR
    return parseIRFile(Stderr, Err, InitialIR.getContext());
  }

  llvm::Expected<std::unique_ptr<Module>>
  getFinalModule(const std::string PipelineText) {
    std::function<void(const StringRef, Any, const PreservedAnalyses)>
        EmptyCallback = [](const StringRef, Any, const PreservedAnalyses &) {};
    return runOpt(PipelineText, EmptyCallback);
  }

  llvm::Expected<std::string> getPassName(std::string PipelineText,
                                          unsigned N) {
    auto Passes = getPassListAndDescription(PipelineText);
    if (!Passes)
      return Passes.takeError();
    return Passes->operator[](N).first;
  }
};

} // namespace llvm

#endif // LLVM_TOOLS_LLVM_LSP_OPTRUNNER_H
