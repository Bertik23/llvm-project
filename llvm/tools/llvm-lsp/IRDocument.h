#ifndef IRDOCUMENT_H
#define IRDOCUMENT_H

#include "Logger.h"
#include "llvm/Analysis/CFGPrinter.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/GraphWriter.h"
#include "llvm/Support/SourceMgr.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <regex>

// struct Loc {
//   unsigned Line;
//   unsigned Col;

//   bool operator<=(const Loc &RHS) const {
//     return Line < RHS.Line || (Line == RHS.Line && Col <= RHS.Col);
//   }

//   bool operator<(const Loc &RHS) const {
//     return Line < RHS.Line || (Line == RHS.Line && Col < RHS.Col);
//   }

//   Loc(unsigned L, unsigned C) : Line(L), Col(C) {}
// };

// struct LocRange {
//   Loc Start;
//   Loc End;

//   LocRange() : Start(0, 0), End(0, 0) {}

//   LocRange(Loc S, Loc E) : Start(S), End(E) { assert(Start <= End); }

//   bool contains(Loc L) { return Start <= L && L <= End; }

//   bool contains(LocRange LR) { return contains(LR.Start) && contains(LR.End);
//   }
// };

// FIXME: Replace this with IR Parser based location-tracking, instead of using
// Regexes.
class IRLocationsMap {

  Logger &LoggerObj;
  llvm::DenseMap<llvm::Function *, llvm::LocRange> FuncToLocation;
  // TODO: Add support for Basic Blocks and Instructions
public:
  IRLocationsMap(llvm::Module &M, llvm::StringRef Filepath, Logger &LO)
      : LoggerObj(LO) {
    for (auto &F : M.getFunctionList()) {
      auto FLoc = F.getLocRange();
      if (!FLoc)
        LoggerObj.error("Could not find Location for Function!");
      FuncToLocation[&F] = *F.getLocRange();
    }
  }

  llvm::Function *getFunctionAtLocation(llvm::FileLoc L) {
    for (auto &P : FuncToLocation) {
      LoggerObj.log("Checking function " + P.getFirst()->getName().str());
      if (P.second.contains(L)) {
        LoggerObj.log("Returning function " + P.getFirst()->getName().str());
        return P.first;
      }
    }
    return nullptr;
  }

  std::optional<llvm::LocRange> getLocationForFunction(llvm::Function *F) {
    if (FuncToLocation.contains(F))
      return FuncToLocation[F];
    return std::nullopt;
  }
};

class IRArtifacts {
  Logger &LoggerObj;
  const llvm::Module &IR;
  std::filesystem::path ArtifactsFolderPath;

  // FIXME: Can perhaps maintain a single list of only SVG/Dot files
  llvm::DenseMap<llvm::Function *, std::filesystem::path> DotFileList;
  llvm::DenseMap<llvm::Function *, std::filesystem::path> SVGFileList;

  // TODO: Add support to store locations of Intermediate IR file locations

public:
  IRArtifacts(llvm::StringRef Filepath, Logger &LO, llvm::Module &M)
      : LoggerObj(LO), IR(M) {
    // Make Artifacts folder, if it does not exist
    LoggerObj.log("Creating IRArtifacts Directory for " + Filepath.str());
    std::filesystem::path FilepathObj(Filepath.str());
    ArtifactsFolderPath = FilepathObj.parent_path().string() + "/Artifacts-" +
                          FilepathObj.stem().string();
    if (!std::filesystem::exists(ArtifactsFolderPath))
      std::filesystem::create_directory(ArtifactsFolderPath);
    LoggerObj.log("Finished creating IR Artifacts Directory for" +
                  Filepath.str());
  }

  void generateGraphs() {
    for (auto &F : IR.getFunctionList())
      generateGraphsForFunc(F.getName());
  }

  void generateGraphsForFunc(llvm::StringRef FuncName) {
    llvm::Function *F = IR.getFunction(FuncName);
    assert(F && "Function does not exist to generate Dot file");

    // Generate Dot file
    std::filesystem::path DotFilePath =
        ArtifactsFolderPath / std::filesystem::path(FuncName.str() + ".dot");
    if (!std::filesystem::exists(DotFilePath)) {
      llvm::DOTFuncInfo DFI(F);
      llvm::WriteGraph(&DFI, FuncName, false, "CFG for " + FuncName.str(),
                       DotFilePath.string());
    }

    // Generate SVG file
    generateSVGFromDot(DotFilePath, F);

    DotFileList[F] = DotFilePath;
  }

  std::optional<std::string> getDotFilePath(llvm::Function *F) {
    if (DotFileList.contains(F)) {
      return DotFileList[F].string();
    }
    return std::nullopt;
  }

  std::optional<std::string> getSVGFilePath(llvm::Function *F) {
    if (SVGFileList.contains(F)) {
      return SVGFileList[F].string();
    }
    return std::nullopt;
  }

private:
  void generateSVGFromDot(std::filesystem::path Dotpath, llvm::Function *F) {
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
  llvm::LLVMContext C;
  std::unique_ptr<llvm::Module> ParsedModule;
  Logger &LoggerObj;
  llvm::StringRef Filepath;

  std::unique_ptr<IRLocationsMap> IRLM;
  std::unique_ptr<IRArtifacts> IRA;

public:
  IRDocument(llvm::StringRef PathToIRFile, Logger &LO)
      : LoggerObj(LO), Filepath(PathToIRFile) {
    ParsedModule = loadModuleFromIR(PathToIRFile, C);
    IRLM = std::make_unique<IRLocationsMap>(*ParsedModule, PathToIRFile, LO);
    IRA = std::make_unique<IRArtifacts>(PathToIRFile, LO, *ParsedModule);
    // Eagerly generate all CFGs
    IRA->generateGraphs();
    LoggerObj.log("Finished setting up IR Document :" + PathToIRFile.str());
  }

  // ---------------- APIs that the Language Server can use  -----------------

  llvm::Function *getFirstFunction() {
    return &ParsedModule->getFunctionList().front();
  }
  void generateCFGs() { IRA->generateGraphs(); }

  std::optional<std::string> getPathForSVGFile(llvm::Function *F) {
    return IRA->getSVGFilePath(F);
  }

  llvm::Function *getFunctionAtLocation(unsigned Line, unsigned Col) {
    return IRLM->getFunctionAtLocation({Line, Col});
  }

private:
  std::unique_ptr<llvm::Module> loadModuleFromIR(llvm::StringRef Filepath,
                                                 llvm::LLVMContext &C) {
    llvm::SMDiagnostic Err;
    // Try to parse as textual IR
    auto M = llvm::parseIRFile(Filepath, Err, C);
    if (!M)
      // If parsing failed, print the error and crash
      LoggerObj.error("Failed parsing IR file: " + Err.getMessage().str());
    return M;
  }
};

#endif // IRDOCUMENT_H