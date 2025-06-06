#ifndef IRDOCUMENT_H
#define IRDOCUMENT_H

#include "Logger.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/Analysis/CFGPrinter.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/GraphWriter.h"
#include "llvm/Support/SourceMgr.h"

#include <filesystem>
#include <memory>

namespace llvm {

class IRArtifacts {
  Logger &LoggerObj;
  const Module &IR;
  std::filesystem::path ArtifactsFolderPath;

  // FIXME: Can perhaps maintain a single list of only SVG/Dot files
  DenseMap<Function *, std::filesystem::path> DotFileList;
  DenseMap<Function *, std::filesystem::path> SVGFileList;

  // TODO: Add support to store locations of Intermediate IR file locations

public:
  IRArtifacts(StringRef Filepath, Logger &LO, Module &M)
      : LoggerObj(LO), IR(M) {
    // Make Artifacts folder, if it does not exist
    LoggerObj.log("Creating IRArtifacts Directory for " + Filepath.str());
    std::filesystem::path FilepathObj(Filepath.str());
    ArtifactsFolderPath = FilepathObj.parent_path().string() + "/Artifacts-" +
                          FilepathObj.stem().string();
    if (!std::filesystem::exists(ArtifactsFolderPath)) {
      std::filesystem::create_directory(ArtifactsFolderPath);
      LoggerObj.log("Finished creating IR Artifacts Directory " +
                    ArtifactsFolderPath.string() + " for " + Filepath.str());
    } else
      LoggerObj.log("Directory " + ArtifactsFolderPath.string() +
                    " already exists");
  }

  void generateGraphs() {
    for (auto &F : IR.getFunctionList())
      if (!F.isDeclaration())
        generateGraphsForFunc(F.getName());
  }

  void generateGraphsForFunc(StringRef FuncName) {
    Function *F = IR.getFunction(FuncName);
    assert(F && "Function does not exist to generate Dot file");

    // Generate Dot file
    std::filesystem::path DotFilePath =
        ArtifactsFolderPath / std::filesystem::path(FuncName.str() + ".dot");
    if (!std::filesystem::exists(DotFilePath)) {
      PassBuilder PB;
      FunctionAnalysisManager FAM;
      PB.registerFunctionAnalyses(FAM);
      auto &BFI = FAM.getResult<BlockFrequencyAnalysis>(*F);
      auto &BPI = FAM.getResult<BranchProbabilityAnalysis>(*F);
      DOTFuncInfo DFI(F, &BFI, &BPI, getMaxFreq(*F, &BFI));
      DFI.setHeatColors(true);
      DFI.setEdgeWeights(true);
      DFI.setRawEdgeWeights(false);
      WriteGraph(&DFI, FuncName, false, "CFG for " + FuncName.str(),
                 DotFilePath.string());
    }

    // Generate SVG file
    generateSVGFromDot(DotFilePath, F);

    DotFileList[F] = DotFilePath;
  }

  std::optional<std::string> getDotFilePath(Function *F) {
    if (DotFileList.contains(F)) {
      return DotFileList[F].string();
    }
    return std::nullopt;
  }

  std::optional<std::string> getSVGFilePath(Function *F) {
    if (SVGFileList.contains(F)) {
      return SVGFileList[F].string();
    }
    return std::nullopt;
  }

private:
  void generateSVGFromDot(std::filesystem::path Dotpath, Function *F) {
    std::filesystem::path SVGFilePath =
        std::filesystem::path(Dotpath).replace_extension(".svg");
    std::string Cmd =
        "dot -Tsvg " + Dotpath.string() + " -o " + SVGFilePath.string();
    LoggerObj.log("Running command: " + Cmd);
    int Result = std::system(Cmd.c_str());

    if (Result == 0) {
      LoggerObj.log("SVG Generated : " + SVGFilePath.string());
      SVGFileList[F] = SVGFilePath;
    } else
      LoggerObj.log("Failed to generate SVG!");
  }
};

// LSP Server will use this class to query details about the IR file.
class IRDocument {
  LLVMContext C;
  std::unique_ptr<Module> ParsedModule;
  Logger &LoggerObj;
  StringRef Filepath;

  std::unique_ptr<IRArtifacts> IRA;

public:
  IRDocument(StringRef PathToIRFile, Logger &LO)
      : LoggerObj(LO), Filepath(PathToIRFile) {
    ParsedModule = loadModuleFromIR(PathToIRFile, C);
    IRA = std::make_unique<IRArtifacts>(PathToIRFile, LO, *ParsedModule);
    // Eagerly generate all CFGs
    IRA->generateGraphs();
    LoggerObj.log("Finished setting up IR Document: " + PathToIRFile.str());
  }

  // ---------------- APIs that the Language Server can use  -----------------

  Function *getFirstFunction() {
    return &ParsedModule->getFunctionList().front();
  }
  void generateCFGs() { IRA->generateGraphs(); }

  std::optional<std::string> getPathForSVGFile(Function *F) {
    return IRA->getSVGFilePath(F);
  }

  Function *getFunctionAtLocation(unsigned Line, unsigned Col) {
    FileLoc FL(Line, Col);
    for (auto &F : *ParsedModule) {
      auto FuncRangeOpt = F.getLocRange();
      if (!FuncRangeOpt)
        LoggerObj.error("Could not find Location for Function!");
      if (FuncRangeOpt->contains(FL))
        return &F;
    }
    return nullptr;
  }

private:
  std::unique_ptr<Module> loadModuleFromIR(StringRef Filepath, LLVMContext &C) {
    SMDiagnostic Err;
    // Try to parse as textual IR
    auto M = parseIRFile(Filepath, Err, C);
    if (!M)
      // If parsing failed, print the error and crash
      LoggerObj.error("Failed parsing IR file: " + Err.getMessage().str());
    return M;
  }
};

} // namespace llvm

#endif // IRDOCUMENT_H