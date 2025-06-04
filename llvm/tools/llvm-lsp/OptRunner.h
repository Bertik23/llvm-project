#ifndef OPTRUNNER_H
#define OPTRUNNER_H

#include "Logger.h"
#include "llvm/Analysis/CFGPrinter.h"
#include "llvm/Analysis/CGSCCPassManager.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/GraphWriter.h"
#include "llvm/Transforms/Utils/Cloning.h"

// FIXME: Maybe a better name?
class OptRunner {
  Logger &LoggerObj;
  llvm::LLVMContext Context;
  std::unique_ptr<llvm::Module> InitialIR;
  std::unique_ptr<llvm::Module> FinalIR = nullptr;

  // Analysis Managers
  llvm::LoopAnalysisManager LAM;
  llvm::FunctionAnalysisManager FAM;
  llvm::CGSCCAnalysisManager CGAM;
  llvm::ModuleAnalysisManager MAM;

  llvm::ModulePassManager MPM;
  llvm::PassBuilder PB;
  llvm::PassInstrumentationCallbacks PIC;

  llvm::SmallVector<std::string, 256> PassList;

  std::filesystem::path ArtifactsFolderPath;

  // Location of Graphs associated with this IR file
  std::filesystem::path DotFilePath;
  std::filesystem::path SVGFilePath;

public:
  OptRunner(const std::string Filename, Logger &LO,
            const std::string PipelineText = "verify")
      : LoggerObj(LO), PassList() {
    InitialIR = loadModuleFromIR(Filename, Context);

    PIC.registerAfterPassCallback([this](const llvm::StringRef PassName,
                                         llvm::Any,
                                         const llvm::PreservedAnalyses &PA) {
      this->PassList.push_back(PassName.str());
    });

    PB = llvm::PassBuilder(nullptr, llvm::PipelineTuningOptions(), std::nullopt,
                           &PIC);

    PB.registerModuleAnalyses(MAM);
    PB.registerCGSCCAnalyses(CGAM);
    PB.registerFunctionAnalyses(FAM);
    PB.registerLoopAnalyses(LAM);
    PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

    // Parse Pipeline text
    auto ParseError = PB.parsePassPipeline(MPM, PipelineText);

    if (ParseError)
      LoggerObj.log("Error parsing pipeline text!");

    // Make Artifacts folder, if it does not exist
    std::filesystem::path Filepath(Filename);
    ArtifactsFolderPath = Filepath.parent_path().string() + "/Artifacts-" +
                          Filepath.stem().string() + "/";
    if (!std::filesystem::exists(ArtifactsFolderPath))
      std::filesystem::create_directory(ArtifactsFolderPath);
  }

  const llvm::SmallVectorImpl<std::string> &getPassList() {
    if (PassList.empty())
      runOpt();

    return PassList;
  }

  void runOpt() {
    PassList.clear();
    FinalIR = llvm::CloneModule(*InitialIR);
    MPM.run(*FinalIR, MAM);
  }

  void generateGraphs() {
    // We Assume that InitialIR has only 1 function
    assert(InitialIR->getFunctionList().size() == 1);

    llvm::Function &F = InitialIR->getFunctionList().front();

    DotFilePath =
        ArtifactsFolderPath / std::filesystem::path(F.getName().str() + ".dot");
    // if (!std::filesystem::exists(DotFilePath)) // TODO: Uncomment line once
    // generation of graphs is stable.
    // FIXME: This generates an empty dot graph. This needs to fixed.
    llvm::WriteGraph((const llvm::Function *)&F, F.getName(), false,
                     "CFG for " + F.getName().str(), DotFilePath.string());
    generateSVGFromDot(DotFilePath);
  }

  std::string getDotFilePath() { return DotFilePath.string(); }
  std::string getSVGFilePath() { return SVGFilePath.string(); }

private:
  std::unique_ptr<llvm::Module> loadModuleFromIR(const std::string &Filepath,
                                                 llvm::LLVMContext &C) {
    llvm::SMDiagnostic Err;
    // Try to parse as textual IR
    auto M = llvm::parseIRFile(Filepath, Err, C);
    if (!M) {
      // If parsing failed, print the error
      LoggerObj.log("Failed parsing IR file: " + Err.getMessage().str());
      return nullptr;
    }
    return M;
  }

  void generateSVGFromDot(std::filesystem::path Dotpath) {
    SVGFilePath = std::filesystem::path(Dotpath).replace_extension(".svg");
    std::string Cmd =
        "dot -Tsvg " + Dotpath.string() + " -o " + SVGFilePath.string();
    LoggerObj.log("Running command: " + Cmd);
    int Result = std::system(Cmd.c_str());

    if (Result == 0)
      LoggerObj.log("SVG Generated : " + SVGFilePath.string());
    else
      LoggerObj.log("Failed to generate SVG!");
  }
};

#endif // OPTRUNNER_H