//===-- IRDocument.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_LSP_IRDOCUMENT_H
#define LLVM_TOOLS_LLVM_LSP_IRDOCUMENT_H

#include "OptRunner.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/Analysis/CFGPrinter.h"
#include "llvm/AsmParser/AsmParserContext.h"
#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FormatAdapters.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/GraphWriter.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/LSP/Protocol.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"

#include <cstdint>
#include <filesystem>
#include <ios>
#include <memory>
#include <string>

namespace {

constexpr const char *IrLLFilename = "ir.ll";

// NOLINTBEGIN(readability-identifier-naming)
// Represents the arguments passed to 'opt' that are persisted across
// different stages of IR transformation. This allows maintaining a consistent
// environment (e.g., data layout) for a chain of optimizations.
struct IrOptArgs {
  std::vector<std::string> pipeline_opt_args;
  std::vector<std::string> pass_opt_args;
};

bool fromJSON(const llvm::json::Value &Value, IrOptArgs &OptArgs,
              llvm::json::Path Path) {
  llvm::json::ObjectMapper O(Value, Path);
  return O &&
         O.map("pipeline-opt-args", OptArgs.pipeline_opt_args) &&
         O.map("pass-opt-args", OptArgs.pass_opt_args);
}

bool toJSON(const IrOptArgs &OptArgs, llvm::json::Value &Value) {
  llvm::json::Object O;
  O["pipeline-opt-args"] = OptArgs.pipeline_opt_args;
  O["pass-opt-args"] = OptArgs.pass_opt_args;
  Value = std::move(O);
  return true;
}

// NOLINTEND(readability-identifier-naming)

// Provides utilities for creating and parsing stable identifiers for basic
// blocks based on their source location. This is used to link nodes in the
// CFG SVG back to the source code.
class IRDocumentHelpers {
public:
  static std::optional<std::string>
  basicBlockIdFormatter(const llvm::BasicBlock *BB,
                        const llvm::AsmParserContext &ParserContext) {
    auto MaybeBBLoc = ParserContext.getBlockLocation(BB);
    if (MaybeBBLoc.has_value()) {
      auto Loc = *MaybeBBLoc;
      return llvm::formatv("range_{0}_{1}_{2}_{3}", Loc.Start.Line,
                           Loc.Start.Col, Loc.End.Line, Loc.End.Col);
    }
    return std::nullopt;
  };

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
// Tracks and Manages the Cache of all Artifacts for a given IR.
// Manages the on-disk artifacts generated for a given IR file, such as CFG
// .dot/.svg files, intermediate IR from optimization passes, and cached pass
// lists. It organizes these artifacts in a dedicated 'Artifacts-...' directory
// and provides caching to avoid redundant generation.
class IRArtifacts {
  const Module &IR;
  const std::filesystem::path &IRFile;
  mutable std::optional<std::filesystem::path> ArtifactsFolderPath;

  // FIXME: Can perhaps maintain a single list of only SVG/Dot files
  DenseMap<Function *, std::filesystem::path> DotFileList;
  DenseMap<Function *, std::filesystem::path> SVGFileList;

  // TODO: Add support to store locations of Intermediate IR file locations

  std::filesystem::path getArtifactsFolderPath() const {
    if (ArtifactsFolderPath)
      return ArtifactsFolderPath.value();

    lsp::Logger::info("Creating IRArtifacts Directory for {}", IRFile.string());
    ArtifactsFolderPath =
        IRFile.parent_path() / ("Artifacts-" + IRFile.stem().string());
    if (!std::filesystem::exists(*ArtifactsFolderPath)) {
      std::filesystem::create_directories(*ArtifactsFolderPath);
      lsp::Logger::info("Finished creating IR Artifacts Directory {} for {}",
                        ArtifactsFolderPath->string(), IRFile.string());
    } else
      lsp::Logger::info("Directory {} already exists",
                        ArtifactsFolderPath->string());
    return ArtifactsFolderPath.value();
  }

public:
  IRArtifacts(const std::filesystem::path &Filepath, Module &M)
      : IR(M), IRFile(Filepath) {}

  void generateGraphs(const AsmParserContext &ParserContext) {
    for (auto &F : IR.getFunctionList())
      if (!F.isDeclaration() && DotFileList.contains(&F))
        generateGraphsForFunc(const_cast<Function *>(&F), ParserContext);
  }

  void generateGraphsForFunc(Function *Func,
                             const AsmParserContext &ParserContext) {
    assert(Func && "Function does not exist to generate Dot file");

    // Generate Dot file
    std::filesystem::path DotFilePath =
        getArtifactsFolderPath() / (Func->getName() + ".dot").str();
    // Regenerate cfg
    PassBuilder PB;
    FunctionAnalysisManager FAM;
    PB.registerFunctionAnalyses(FAM);
    auto &BFI = FAM.getResult<BlockFrequencyAnalysis>(*Func);
    auto &BPI = FAM.getResult<BranchProbabilityAnalysis>(*Func);
    DOTFuncInfo DFI(
        Func, &BFI, &BPI, getMaxFreq(*Func, &BFI), [&](const BasicBlock *BB) {
          return IRDocumentHelpers::basicBlockIdFormatter(BB, ParserContext);
        });
    DFI.setHeatColors(true);
    DFI.setEdgeWeights(true);
    DFI.setRawEdgeWeights(false);
    // FIXME: I think this dumps something to the stdout (or stderr?) that in
    // any case gets
    //   sent to the client and shows in the trace log, eg. I see messages
    //   like this: "writing to the newly created file
    //   /remote-home/jjecmen/irviz-2.0/test/Artifacts-foo/main.dot" We should
    //   prevent that.
    WriteGraph(&DFI, Func->getName(), false, "CFG for " + Func->getName(),
               DotFilePath.string());

    // Generate SVG file
    generateSVGFromDot(DotFilePath, Func);

    DotFileList[Func] = DotFilePath;
  }

  std::optional<IrOptArgs> loadIrOptArgs() const {
    // Load ir opt args into json
    auto IrOptArgsPath =
        getArtifactsFolderPath().parent_path() / IrOptArgsFilename;
    if (!std::filesystem::exists(IrOptArgsPath)) {
      lsp::Logger::info("No ir opt args found");
      return std::nullopt;
    }
    lsp::Logger::info("Loading ir opt args from {}", IrOptArgsPath.string());
    // Read file
    auto BufferOrErr = llvm::MemoryBuffer::getFile(IrOptArgsPath.string());

    // 2. Check for errors (file not found, permissions, etc.)
    if (std::error_code EC = BufferOrErr.getError()) {
      llvm::errs() << "Could not open file: " << EC.message() << "\n";
      return std::nullopt;
    }

    // 3. Extract the unique_ptr and get the buffer reference
    std::unique_ptr<llvm::MemoryBuffer> &Buffer = *BufferOrErr;

    // 4. Access the file content as an LLVM StringRef
    llvm::StringRef IrOptArgsFileContents = Buffer->getBuffer();

    auto ParseResult = llvm::json::parse<IrOptArgs>(IrOptArgsFileContents);
    if (!ParseResult) {
      lsp::Logger::error("Error reading ir opt args: {}",
                         fmt_consume(ParseResult.takeError()));
      return std::nullopt;
    }
    return *ParseResult;
  }

  std::string getArtifactsFolderName(StringRef Pipeline, StringRef PassName,
                                     unsigned PassNum, StringRef Options) {
    std::string Out;
    raw_string_ostream SS(Out);
    if (!Pipeline.empty()) {
      SS << Pipeline << "-";
    }
    SS << PassName << "-";
    SS << llvm::format("%x", (uint64_t)llvm::hash_combine(Pipeline, PassName,
                                                          PassNum, Options));
    return Out;
  }

  std::string getPassListFileName(StringRef Pipeline, StringRef Options) {
    std::string Out = "passes-";
    raw_string_ostream SS(Out);
    if (!Pipeline.empty()) {
      SS << Pipeline << "-";
    }
    SS << llvm::format("%x", (uint64_t)llvm::hash_combine(Pipeline, Options));
    return Out;
  }

  void addIntermediateIR(const std::filesystem::path &IRFile, unsigned PassNum,
                         StringRef PassName, StringRef Pipeline = "",
                         StringRef Options = "") {
    auto IRFolder =
        getArtifactsFolderPath() /
        getArtifactsFolderName(Pipeline, PassName, PassNum, Options);
    if (!std::filesystem::exists(IRFolder))
      std::filesystem::create_directory(IRFolder);
    lsp::Logger::info("Created directory for intermediate IR artifacts!");

    auto IRFilepath = IRFolder / IrLLFilename;
    if (!std::filesystem::exists(IRFilepath)) {
      lsp::Logger::info("Copying IR file to intermediate IR: {} -> {}", IRFile,
                        IRFilepath.string());
      std::filesystem::copy_file(IRFile, IRFilepath);
      lsp::Logger::info("Finished copying IR file");
    } else {
      lsp::Logger::info("IR File path already exists: {}", IRFilepath.string());
    }
    auto IrOptArgsJsonPath = IRFile.parent_path() / IrOptArgsFilename;
    if (std::filesystem::exists(IrOptArgsJsonPath)) {
      auto DestIrOptArgsJsonPath = IRFolder / IrOptArgsFilename;
      if (!std::filesystem::exists(DestIrOptArgsJsonPath)) {
        lsp::Logger::info(
            "Copying ir_opt_args.json to intermediate IR: {} -> {}",
            IrOptArgsJsonPath.string(), DestIrOptArgsJsonPath.string());
        std::filesystem::copy_file(IrOptArgsJsonPath, DestIrOptArgsJsonPath);
        lsp::Logger::info("Finished copying ir_opt_args.json");
      }
    }
  }

  std::optional<std::filesystem::path>
  getIRBeforePassNumber(StringRef Pipeline, StringRef PassName,
                        unsigned PassNumber, StringRef Options) {
    auto FolderName =
        getArtifactsFolderName(Pipeline, PassName, PassNumber, Options);
    if (!std::filesystem::exists(getArtifactsFolderPath() / FolderName /
                                 IrLLFilename)) {
      lsp::Logger::info("Did not find IR!");
      return std::nullopt;
    }
    return getArtifactsFolderPath() / FolderName / IrLLFilename;
  }

  void
  addPassList(StringRef Pipeline, StringRef Options,
              SmallVector<std::pair<std::string, std::string>, 256> Passlist) {
    auto FileName =
        getArtifactsFolderPath() / getPassListFileName(Pipeline, Options);
    lsp::Logger::info("Saving passlist to {}", FileName);
    llvm::json::Array JSONCache;
    for (const auto &KV : Passlist) {
      // Store each pair as a 2-element array
      JSONCache.push_back(llvm::json::Array{KV.first, KV.second});
    }

    std::error_code EC;
    llvm::raw_fd_ostream OS(FileName.c_str(), EC, llvm::sys::fs::OF_None);
    if (!EC) {
      // Write out the JSON structure
      OS << llvm::json::Value(std::move(JSONCache));
    }
  }

  std::optional<SmallVector<std::pair<std::string, std::string>, 256>>
  getPassList(StringRef Pipeline, StringRef Options) {
    SmallVector<std::pair<std::string, std::string>> Output;
    auto FileName =
        getArtifactsFolderPath() / getPassListFileName(Pipeline, Options);
    lsp::Logger::info("Loading passlist from {}", FileName);
    auto BufOrErr = llvm::MemoryBuffer::getFile(FileName.c_str());
    if (!BufOrErr)
      return std::nullopt;

    // Parse the JSON
    auto ParsedOrErr = llvm::json::parse(BufOrErr.get()->getBuffer());
    if (!ParsedOrErr) {
      llvm::consumeError(ParsedOrErr.takeError()); // Ignore parsing errors
      return std::nullopt;
    }

    // Extract the arrays
    if (auto *OuterArr = ParsedOrErr->getAsArray()) {
      for (const auto &Item : *OuterArr) {
        if (auto *InnerArr = Item.getAsArray()) {
          if (InnerArr->size() == 2) {
            auto Key = (*InnerArr)[0].getAsString();
            auto Val = (*InnerArr)[1].getAsString();
            if (Key && Val) {
              Output.emplace_back(Key->str(), Val->str());
            }
          }
        }
      }
    }
    return Output;
  }

  std::optional<std::string> getDotFilePath(Function *F) {
    if (DotFileList.contains(F)) {
      return DotFileList[F].string();
    }
    return std::nullopt;
  }

  std::optional<lsp::URIForFile>
  getSVGFilePath(Function *F, const AsmParserContext &ParserContext) {
    if (!SVGFileList.contains(F)) {
      generateGraphsForFunc(F, ParserContext);
    }
    if (auto Ret = lsp::URIForFile::fromFile(SVGFileList[F].string())) {
      return *Ret;
    }
    return std::nullopt;
  }

private:
  void generateSVGFromDot(std::filesystem::path Dotpath, Function *F) {
    std::filesystem::path SVGFilePath =
        std::filesystem::path(Dotpath).replace_extension(".svg");
    std::string Cmd = "dot -Tsvg '" + Dotpath.string() + "' -o '" +
                      SVGFilePath.string() + "'";
    lsp::Logger::info("Running command: {}", Cmd);
    int Result = std::system(Cmd.c_str());

    if (Result == 0) {

      lsp::Logger::info("SVG Generated : {}", SVGFilePath.string());
      SVGFileList[F] = SVGFilePath;
    } else
      lsp::Logger::error("Failed to generate SVG!");
  }
};

/**
  Creates OptRunner for LSP requests.
  This factory is responsible for creating OptRunner instances with the correct
  set of arguments. It handles loading "hereditary" arguments from an
  `ir_opt_args.json` file, which allows optimization settings to persist across
  different stages of IR generation within the LSP session.
*/
class OptRunnerFactory {
  const std::filesystem::path File;
  const std::optional<std::string> OptPath = std::nullopt;
  IRArtifacts *IRA = nullptr;

public:
  OptRunnerFactory(const std::filesystem::path &File, IRArtifacts *IRA,
                   std::optional<std::string> OptPath = std::nullopt)
      : File(File), OptPath(OptPath), IRA(IRA) {}
  // Create new OptRunner. Arguments are default args. HediritaryArgs are
  // replaced by args found in IrOptArgs file, if the file exists.
  OptRunner create(const std::vector<std::string> &PipelineArgs,
                   const std::vector<std::string> &PassArgs) {
    if (auto OptArgs = IRA->loadIrOptArgs())
      return OptRunner(File, OptPath, OptArgs->pipeline_opt_args,
                       PassArgs);
    return OptRunner(File, OptPath, PipelineArgs, PassArgs);
  }
};

// LSP Server will use this class to query details about the IR file.
// Represents the in-memory state of a single, open .ll file. It holds the
// parsed LLVM Module and the AsmParserContext, which is crucial for mapping
// source locations to IR values. It also provides an API for higher-level
// operations like generating CFGs or running optimization passes via the
// OptRunner.
class IRDocument {
  LLVMContext C;
  std::unique_ptr<Module> ParsedModule;
  std::filesystem::path Filepath;
  std::string Text;

  std::unique_ptr<OptRunnerFactory> OptimizerFactory;
  std::unique_ptr<IRArtifacts> IRA;
  std::optional<std::string> OpenError = std::nullopt;
  SMDiagnostic FileDiagnostic;

public:
  AsmParserContext ParserContext;

  IRDocument(const std::filesystem::path &PathToIRFile, const std::string &Text,
             std::optional<std::string> OptPath = std::nullopt)
      : Filepath(PathToIRFile), Text(Text) {
    lsp::Logger::debug("Trying to open {}", PathToIRFile);
    auto MaybeParsedModule =
        loadModuleFromIRText(Text, C, ParserContext, FileDiagnostic);
    if (!MaybeParsedModule) {
      std::string ErrMsg;
      raw_string_ostream OS(ErrMsg);
      logAllUnhandledErrors(MaybeParsedModule.takeError(), OS);
      lsp::Logger::error("Error while parsing IR: {}", ErrMsg);
      OpenError = ErrMsg;
      return;
    }
    ParsedModule = std::move(*MaybeParsedModule);
    IRA = std::make_unique<IRArtifacts>(Filepath, *ParsedModule);
    OptimizerFactory =
        std::make_unique<OptRunnerFactory>(Filepath, IRA.get(), OptPath);
    lsp::Logger::info("Finished setting up IR Document: {}", PathToIRFile);
  }

  std::optional<SMDiagnostic> error() {
    if (OpenError)
      return FileDiagnostic;
    return std::nullopt;
  }

  // ---------------- APIs that the Language Server can use  -----------------

  void makeGraphs() { IRA->generateGraphs(ParserContext); }

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

  std::optional<lsp::URIForFile> getPathForSVGFile(Function *F) {
    return IRA->getSVGFilePath(F, ParserContext);
  }

  auto &getFunctions() { return ParsedModule->getFunctionList(); }

  Function *getFunctionAtLocation(unsigned Line, unsigned Col) {
    FileLoc FL(Line, Col);
    return ParserContext.getFunctionAtLocation(FL);
  }

  BasicBlock *getBlockAtLocation(unsigned Line, unsigned Col) {
    FileLoc FL(Line, Col);
    return ParserContext.getBlockAtLocation(FL);
  }

  Value *getInstructionOrArgumentAtLocation(unsigned Line, unsigned Col) {
    FileLoc FL(Line, Col);
    lsp::Logger::debug("Geting instruction or function argument at location");
    auto R = ParserContext.getInstructionOrArgumentAtLocation(FL);
    lsp::Logger::debug("Got instruction or function argument at location");
    return R;
  }

  llvm::Expected<std::filesystem::path> getIRBeforePassNumber(
      const std::string &Pipeline, unsigned N,
      std::optional<std::vector<std::string>> PipelineOptArgs = std::nullopt,
      std::optional<std::vector<std::string>> PassOptArgs = std::nullopt) {
    auto Optimizer = OptimizerFactory->create(
        PipelineOptArgs.value_or(std::vector<std::string>()),
        PassOptArgs.value_or(std::vector<std::string>()));
    auto PassesResult =
        getPassList(Pipeline, std::move(Optimizer.getAllArgs()));
    if (!PassesResult)
      return PassesResult.takeError();
    auto *PassNameIt =
        std::lower_bound(PassesResult->begin(), PassesResult->end(), N,
                         [](const auto &A, const auto &B) {
                           std::stringstream SS(A);
                           unsigned N;
                           SS >> N;
                           return N < B;
                         });
    if (PassNameIt == PassesResult->end())
      return createStringError("Pass number {} was not found", N);
    auto PassName = *PassNameIt;
    lsp::Logger::info("Found Pass name for pass number {} as {}",
                      std::to_string(N), PassName);

    auto ExistingIR = IRA->getIRBeforePassNumber(Pipeline, PassName, N,
                                                 Optimizer.getArgsString());
    if (ExistingIR) {
      lsp::Logger::info("Found Existing IR");
      return *ExistingIR;
    }

    auto IntermediateIR = Optimizer.getModuleBeforePass(Pipeline, N);
    if (!IntermediateIR) {
      lsp::Logger::info("Error while getting intermediate IR");
      return IntermediateIR.takeError();
    }
    IRA->addIntermediateIR(*IntermediateIR, N, PassName, Pipeline,
                           Optimizer.getArgsString());
    return *IRA->getIRBeforePassNumber(Pipeline, PassName, N,
                                       Optimizer.getArgsString());
  }

  llvm::Expected<std::filesystem::path> runPass(
      const std::string &PassName,
      std::optional<std::vector<std::string>> PipelineOptArgs = std::nullopt,
      std::optional<std::vector<std::string>> PassOptArgs = std::nullopt,
      std::optional<StringRef> StderrPath = std::nullopt) {

    auto Optimizer = OptimizerFactory->create(
        PipelineOptArgs.value_or(std::vector<std::string>()),
        PassOptArgs.value_or(std::vector<std::string>()));

    auto IntermediateIR = Optimizer.runPass(PassName, StderrPath);
    if (!IntermediateIR) {
      lsp::Logger::info("Error while getting intermediate IR");
      return IntermediateIR.takeError();
    }
    IRA->addIntermediateIR(*IntermediateIR, 0, PassName, "",
                           Optimizer.getArgsString());
    return *IRA->getIRBeforePassNumber("", PassName, 0,
                                       Optimizer.getArgsString());
  }

  llvm::Expected<SmallVector<std::pair<std::string, std::string>, 256>>
  getPassDescriptionList(const std::string &Pipeline,
                         const std::optional<std::vector<std::string>>
                             &AdditionalOptArgs = std::nullopt) {
    auto Optimizer = OptimizerFactory->create(
        AdditionalOptArgs.value_or(std::vector<std::string>()), {});
    auto Options = Optimizer.getArgsString();
    auto CachedPasses = IRA->getPassList(Pipeline, Options);
    if (!CachedPasses) {
      auto PassNameAndDescriptionListResult =
          Optimizer.getPassListAndDescription(Pipeline);

      if (!PassNameAndDescriptionListResult) {
        lsp::Logger::info("Handling error in getPassDescriptionList()");
        return PassNameAndDescriptionListResult.takeError();
      }
      IRA->addPassList(Pipeline, Options,
                       PassNameAndDescriptionListResult.get());
      return PassNameAndDescriptionListResult.get();
    }
    return CachedPasses.value();
  }
  // FIXME: We are doing some redundant work here in below functions, which can
  // be fused together.
  llvm::Expected<SmallVector<std::string, 256>>
  getPassList(const std::string &Pipeline,
              const std::optional<std::vector<std::string>> &AdditionalOptArgs =
                  std::nullopt) {
    SmallVector<std::string, 256> PassList;
    auto ListResult = getPassDescriptionList(Pipeline, AdditionalOptArgs);
    if (!ListResult)
      return ListResult.takeError();
    for (auto &P : ListResult.get())
      PassList.push_back(P.first);

    return PassList;
  }
  llvm::Expected<SmallVector<std::string, 256>> getPassDescriptions(
      const std::string &Pipeline,
      const std::optional<std::vector<std::string>> &AdditionalOptArgs =
          std::nullopt) {
    SmallVector<std::string, 256> PassDesc;
    auto ListResult = getPassDescriptionList(Pipeline, AdditionalOptArgs);
    if (!ListResult)
      return ListResult.takeError();
    for (auto &P : ListResult.get())
      PassDesc.push_back(P.second);

    return PassDesc;
  }

private:
  static llvm::Expected<std::unique_ptr<Module>> loadModuleFromIR(
      StringRef Filepath, LLVMContext &C, AsmParserContext &ParserContext,
      std::optional<std::reference_wrapper<SMDiagnostic>> MaybeErr =
          std::nullopt) {
    SMDiagnostic DefaultErr;
    SMDiagnostic &Err = MaybeErr ? *MaybeErr : std::ref(DefaultErr);
    // Try to parse as textual IR
    auto M = parseIRFile(Filepath, Err, C, {}, &ParserContext);
    if (!M) {
      // If parsing failed, print the error and return it
      lsp::Logger::error("Failed parsing IR file: {} at: {}",
                         Err.getMessage().str(), Err.getLineContents());
      return llvm::createStringError(Err.getMessage().str());
    }
    return M;
  }
  static llvm::Expected<std::unique_ptr<Module>> loadModuleFromIRText(
      StringRef Text, LLVMContext &C, AsmParserContext &ParserContext,
      std::optional<std::reference_wrapper<SMDiagnostic>> MaybeErr =
          std::nullopt) {
    SMDiagnostic DefaultErr;
    SMDiagnostic &Err = MaybeErr ? *MaybeErr : std::ref(DefaultErr);
    // Try to parse as textual IR
    auto M = parseAssemblyString(Text, Err, C, {}, &ParserContext);
    if (!M) {
      // If parsing failed, print the error and return it
      lsp::Logger::error("Failed parsing IR file as: {}\n{}",
                         Err.getMessage().str(), Err.getLineContents());
      return llvm::createStringError(Err.getMessage().str());
    }
    return M;
  }
};

} // namespace llvm

#endif // LLVM_TOOLS_LLVM_LSP_IRDOCUMENT_H
