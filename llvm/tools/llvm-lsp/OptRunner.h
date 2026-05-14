//===-- OptRunner.h -------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_LSP_OPTRUNNER_H
#define LLVM_TOOLS_LLVM_LSP_OPTRUNNER_H

#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/IR/Function.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/LSP/Logging.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/raw_ostream.h"
#include <filesystem>
#include <string>

namespace llvm {
constexpr const char *IrOptArgsFilename = "ir_opt_args.json";

// Parses the output of 'opt --print-pass-numbers' to extract a list of pass
// names and their descriptions. This is used to provide information about the
// passes in an optimization pipeline.
SmallVector<std::pair<std::string, std::string>, 256>
parseOptPassList(StringRef OptOutput) {
  SmallVector<std::pair<std::string, std::string>, 256> PassListAndDescription;
  while (!(OptOutput.empty() || OptOutput == "\n")) {
    while (!OptOutput.starts_with("Running pass"))
      OptOutput = OptOutput.drop_front();
    auto NumberPlus = OptOutput.drop_while([](char C) { return !isDigit(C); });
    auto Number = NumberPlus.take_while(isDigit);
    auto AfterNumber =
        NumberPlus.drop_while([](char C) { return isDigit(C) || isSpace(C); });
    auto DescriptionStart = AfterNumber.find(" on ");
    auto Name = AfterNumber.take_front(DescriptionStart);
    auto Description =
        AfterNumber.drop_front(DescriptionStart).take_while([](char C) {
          return C != '\n';
        });
    auto Next = AfterNumber.drop_while([](char C) { return C != '\n'; });

    std::string OutName = Number.str() + "-";
    for (const auto &C : Name) {
      if (isSpace(C))
        continue;
      OutName += C;
    }

    PassListAndDescription.emplace_back(OutName, Description.str());
    OptOutput = Next;
  }
  return PassListAndDescription;
}

// A wrapper around the 'opt' command-line tool. It abstracts the process of
// invoking 'opt' with specific pipelines and arguments, managing temporary
// files for input and output, and parsing the results. It distinguishes between
// 'PipelineArgs' (persistent across a chain of IR transformations) and
// 'PassArgs' (transient for a single run).
class OptRunner {
  const std::filesystem::path File;
  const std::optional<std::string> OptPath = std::nullopt;
  const std::vector<std::string>
      PipelineArgs; // Arguments that are meant to be used for all files that
                    // were generated from this file
  const std::vector<std::string>
      PassArgs; // Arguments that are only used for this run

public:
  OptRunner(const std::filesystem::path &File,
            std::optional<std::string> OptPath = std::nullopt,
            const std::vector<std::string> &PipelineArgs = {},
            const std::vector<std::string> &PassArgs = {})
      : File(File), OptPath(OptPath), PipelineArgs(PipelineArgs),
        PassArgs(PassArgs) {}

  std::string getArgsString() {
    auto One = llvm::join(PipelineArgs, "-");
    auto Two = llvm::join(PassArgs, "-");
    return llvm::join(std::vector{One, Two}, "-");
  }

  std::vector<std::string> getAllArgs() const {
    std::vector<std::string> Out;
    Out.reserve(PipelineArgs.size() + PassArgs.size());
    Out.insert(Out.end(), PipelineArgs.begin(), PipelineArgs.end());
    Out.insert(Out.end(), PassArgs.begin(), PassArgs.end());
    return Out;
  }

  llvm::Expected<SmallVector<std::pair<std::string, std::string>, 256>>
  getPassListAndDescription(const std::string PipelineText) {
    // First is Passname, Second is Pass Description.
    std::vector<std::string> OptArgs = {"-S", "--print-pass-numbers",
                                        "--disable-output", "--passes",
                                        PipelineText};
    OptArgs.insert(OptArgs.end(), PipelineArgs.begin(), PipelineArgs.end());
    OptArgs.insert(OptArgs.end(), PassArgs.begin(), PassArgs.end());
    auto MaybeOutErr = runShellOpt(OptArgs);
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
    return parseOptPassList(StderrContent);
  }

  llvm::Expected<std::pair<SmallString<32>, SmallString<32>>>
  runShellOpt(std::vector<std::string> Args,
              std::optional<StringRef> StdoutPath = std::nullopt,
              std::optional<StringRef> StderrPath = std::nullopt) {
    std::string OptStr;
    if (OptPath) {
      lsp::Logger::debug("Using provided opt path: {}", OptPath.value());
      OptStr = OptPath.value();
    } else {
      lsp::Logger::debug("Searching for opt in PATH");
      auto Opt = llvm::sys::findProgramByName("opt");
      if (!Opt)
        return llvm::make_error<StringError>(Opt.getError(), "opt not found");
      OptStr = *Opt;
    }

    SmallString<32> StdoutPathStr;
    if (!StdoutPath.has_value()) {
      llvm::sys::fs::createTemporaryFile("llvm-lsp-stdout", "ll",
                                         StdoutPathStr);
      StdoutPath = StdoutPathStr;
    }
    SmallString<32> StderrPathStr;
    if (!StderrPath.has_value()) {
      llvm::sys::fs::createTemporaryFile("llvm-lsp-stderr", "ll",
                                         StderrPathStr);
      StderrPath = StderrPathStr;
    }

    std::optional<StringRef> Redirects[] = {std::nullopt, StdoutPath,
                                            StderrPath};

    std::vector<StringRef> AllArgs;
    for (const auto &Arg : Args)
      AllArgs.emplace_back(Arg);

    auto FileStr = File.string();
    AllArgs.emplace_back(FileStr);

    lsp::Logger::debug("Trying to run opt with these options:");
    for (const auto &Arg : AllArgs) {
      lsp::Logger::debug("{}", Arg);
    }
    lsp::Logger::debug("Shell command: {} {}", OptStr,
                       llvm::join(AllArgs, " "));

    lsp::Logger::debug("Output files:\n\tStdout: {}\n\tStderr: {}", StdoutPath,
                       StderrPath);

    AllArgs.emplace(AllArgs.begin(), OptStr);

    auto ExitCode =
        llvm::sys::ExecuteAndWait(OptStr, AllArgs, std::nullopt, Redirects);
    llvm::sys::fs::file_status S;
    llvm::sys::fs::status(*StderrPath, S);
    lsp::Logger::debug("stderr size: {}", S.getSize());
    lsp::Logger::debug("Opt run done. ExitCode: {}", ExitCode);
    if (ExitCode) {
      SmallString<128> StderrContent;
      auto MaybeStderrFD = llvm::sys::fs::openNativeFileForRead(*StderrPath);
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
      lsp::Logger::error("Opt error: Exit code: {}, Stderr: {}", ExitCode,
                         StderrContent);
      return llvm::createStringError(
          "Running opt failed. Exit code: %d, Stderr: %s", ExitCode,
          StderrContent.c_str());
    }
    lsp::Logger::debug("Opt run handle done");
    return std::make_pair(*StdoutPath, *StderrPath);
  }

  bool dumpIrOptArgsJson(std::filesystem::path IrOptArgsPath) {
    llvm::json::Array PipelineArgsJson;
    for (const auto &Arg : PipelineArgs) {
      PipelineArgsJson.emplace_back(Arg);
    }
    llvm::json::Array PassArgsJson;
    for (const auto &Arg : PassArgs) {
      PassArgsJson.emplace_back(Arg);
    }
    llvm::json::Object IrOptArgsJson;
    IrOptArgsJson.insert(
        {"pipeline-opt-args", llvm::json::Value(std::move(PipelineArgsJson))});
    IrOptArgsJson.insert(
        {"pass-opt-args", llvm::json::Value(std::move(PassArgsJson))});
    std::string JsonString;
    llvm::raw_string_ostream JsonStream(JsonString);
    json::OStream J(JsonStream);
    J.value(json::Value(std::move(IrOptArgsJson)));
    JsonStream.flush();

    std::error_code EC;
    llvm::raw_fd_ostream OS(IrOptArgsPath.string(), EC);
    if (EC) {
      lsp::Logger::error("Failed to write {}: {}", IrOptArgsPath.string(),
                         EC.message());
      return true;
    }
    OS << JsonString;
    return false;
  }

  // TODO: Check if N lies with in bounds for below methods. And to verify that
  // they are populated.
  // N is 1-Indexed
  llvm::Expected<std::filesystem::path>
  getModuleBeforePass(const std::string PipelineText, unsigned N) {

    SmallString<128> IRFolder;
    llvm::sys::fs::createUniqueDirectory("llvm-lsp-server-opt-output",
                                         IRFolder);

    std::vector<std::string> OptArgs = {"-S",
                                        "--disable-output",
                                        "--print-before-pass-number",
                                        std::to_string(N),
                                        "--passes",
                                        PipelineText,
                                        "--ir-dump-directory",
                                        IRFolder.c_str()};
    OptArgs.insert(OptArgs.end(), PipelineArgs.begin(), PipelineArgs.end());
    OptArgs.insert(OptArgs.end(), PassArgs.begin(), PassArgs.end());
    auto MaybeOutErr = runShellOpt(OptArgs, std::nullopt, std::nullopt);
    if (!MaybeOutErr)
      return MaybeOutErr.takeError();
    std::filesystem::path IRFolderPath(IRFolder.c_str());

    dumpIrOptArgsJson(IRFolderPath / IrOptArgsFilename);

    for (const auto &IRFile :
         std::filesystem::directory_iterator(IRFolderPath)) {
      if (!IRFile.path().filename().string().starts_with(std::to_string(N))) {
        continue;
      }
      return IRFile.path();
    }

    return llvm::createStringError("No intermediate IR was created");
  }

  llvm::Expected<std::filesystem::path>
  runPass(const std::string Pass,
          std::optional<StringRef> StderrPath = std::nullopt) {

    SmallString<128> IRFolder;
    llvm::sys::fs::createUniqueDirectory("llvm-lsp-server-opt-output",
                                         IRFolder);

    auto IRPath = std::string(IRFolder.c_str()) + "/ir.ll";
    std::vector<std::string> OptArgs = {"-S", "--passes", Pass};
    OptArgs.insert(OptArgs.end(), PipelineArgs.begin(), PipelineArgs.end());
    OptArgs.insert(OptArgs.end(), PassArgs.begin(), PassArgs.end());
    auto MaybeOutErr = runShellOpt(OptArgs, IRPath, StderrPath);
    if (!MaybeOutErr)
      return MaybeOutErr.takeError();
    std::filesystem::path IRFolderPath(IRFolder.c_str());

    dumpIrOptArgsJson(IRFolderPath / IrOptArgsFilename);

    for (const auto &IRFile :
         std::filesystem::directory_iterator(IRFolderPath)) {
      if (!IRFile.path().filename().string().ends_with("ir.ll")) {
        continue;
      }
      return IRFile.path();
    }

    return llvm::createStringError("No intermediate IR was created");
  }
};

} // namespace llvm

#endif // LLVM_TOOLS_LLVM_LSP_OPTRUNNER_H
