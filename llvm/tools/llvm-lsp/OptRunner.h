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

namespace llvm {

// FIXME: Maybe a better name?
class OptRunner {
  Logger &LoggerObj;
  LLVMContext Context;
  const Module &InitialIR;
  std::unique_ptr<Module> FinalIR = nullptr;

  // Analysis Managers
  LoopAnalysisManager LAM;
  FunctionAnalysisManager FAM;
  CGSCCAnalysisManager CGAM;
  ModuleAnalysisManager MAM;

  ModulePassManager MPM;
  PassBuilder PB;
  PassInstrumentationCallbacks PIC;

  SmallVector<std::string, 256> PassList;

public:
  OptRunner(const Module &IR, Logger &LO,
            const std::string PipelineText = "verify")
      : LoggerObj(LO), InitialIR(IR), PassList() {
    // Callback to record PassNames
    PIC.registerAfterPassCallback(
        [this](const StringRef PassName, Any, const PreservedAnalyses &PA) {
          this->PassList.push_back(PassName.str());
        });

    PB = PassBuilder(nullptr, PipelineTuningOptions(), std::nullopt, &PIC);

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

  const SmallVectorImpl<std::string> &getPassList() {
    if (PassList.empty())
      runOpt();
    return PassList;
  }

  void runOpt() {
    // FIXME: Return existing module if Opt has already run?
    PassList.clear();
    FinalIR = CloneModule(InitialIR);
    MPM.run(*FinalIR, MAM);
  }
};

} // namespace llvm

#endif // OPTRUNNER_H
