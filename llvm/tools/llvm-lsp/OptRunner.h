#ifndef OPTRUNNER_H
#define OPTRUNNER_H

#include "Logger.h"
#include "llvm/Analysis/CGSCCPassManager.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Transforms/Utils/Cloning.h"

#include "Logger.h"

// FIXME: Maybe a better name?
class OptRunner {
  Logger &LoggerObj;
  llvm::LLVMContext Context;
  const llvm::Module &InitialIR;
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

public:
  OptRunner(const llvm::Module &IR, Logger &LO,
            const std::string PipelineText = "verify")
      : LoggerObj(LO), InitialIR(IR), PassList() {
    // Callback to record PassNames
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
  }

  const llvm::SmallVectorImpl<std::string> &getPassList() {
    if (PassList.empty())
      runOpt();
    return PassList;
  }

  void runOpt() {
    // FIXME: Return existing module if Opt has already run?
    PassList.clear();
    FinalIR = llvm::CloneModule(InitialIR);
    MPM.run(*FinalIR, MAM);
  }
};

#endif // OPTRUNNER_H
