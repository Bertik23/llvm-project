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
#include <memory>
#include <string>

#include "Logger.h"

namespace llvm {

// FIXME: Maybe a better name?
// TODO: Currently we eagerly generate all intermediate IRs, Replace this with
// Lazy generation of all Intermediate IRs.
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
  SmallVector<std::string, 256> PassDescription;
  SmallVector<std::unique_ptr<Module>, 256> IntermediateIRList;
  unsigned PassNumber = 0;

public:
  OptRunner(Module &IIR, Logger &LO, const std::string PipelineText = "verify")
      : LoggerObj(LO), InitialIR(IIR), PassList() {
    // Callback to record PassNames
    PIC.registerAfterPassCallback([this](const StringRef PassName, Any IR,
                                         const PreservedAnalyses &PA) {
      PassNumber++;
      this->PassList.push_back(std::to_string(PassNumber) + "-" +
                               PassName.str());
      if (auto *M = any_cast<const Module *>(&IR)) {
        std::string Desc = "Module Pass";
        this->PassDescription.push_back(Desc);
        auto MClone = CloneModule(**M);
        IntermediateIRList.push_back(std::move(MClone));
      } else if (auto *F = any_cast<const Function *>(&IR)) {
        std::string Desc = "Function Pass on \"" + (**F).getName().str() + "\"";
        this->PassDescription.push_back(Desc);
        auto MClone = CloneModule(*(**F).getParent());
        IntermediateIRList.push_back(std::move(MClone));
      } else if (auto *L = any_cast<const Loop *>(&IR)) {
        Function *F = (*L)->getHeader()->getParent();
        std::string Desc = "Loop Pass in Function \"" + F->getName().str() +
                           "\" on loop with Header \"" +
                           (*L)->getHeader()->getName().str() + "\"";
        this->PassDescription.push_back(Desc);
        auto MClone = CloneModule(*F->getParent());
        IntermediateIRList.push_back(std::move(MClone));
      } else if (auto *SCC = any_cast<const LazyCallGraph::SCC *>(&IR)) {
        Function &F = (**SCC).begin()->getFunction();
        std::string Desc =
            "CGSCC Pass on Function \"" + F.getName().str() + "\"";
        auto MClone = CloneModule(*F.getParent());
        IntermediateIRList.push_back(std::move(MClone));
      } else
        LoggerObj.error("Unknown Pass Type \"" + PassName.str() + "\"!");
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
    if (PassNumber == 0)
      runOpt();
    return PassList;
  }

  const SmallVectorImpl<std::string> &getPassDescriptionList() {
    if (PassNumber == 0)
      runOpt();
    return PassDescription;
  }

  void runOpt() {
    // FIXME: Return existing module if Opt has already run?
    PassList.clear();
    PassDescription.clear();
    IntermediateIRList.clear();
    PassNumber = 0;
    FinalIR = CloneModule(InitialIR);
    MPM.run(*FinalIR, MAM);
  }

  // TODO: Check if N lies with in bounds for below methods. And to verify that
  // they are populated.
  Module &getModuleAfterPass(unsigned N) {
    return *IntermediateIRList[N].get();
  }
  std::string getPassName(unsigned N) { return PassList[N]; }
  std::string getPassDescription(unsigned N) { return PassDescription[N]; }
  Module &getOptimizedModule() {
    if (!FinalIR)
      runOpt();
    return *FinalIR;
  }
  unsigned getNumPasses() { return PassList.size(); }
};

} // namespace llvm

#endif // OPTRUNNER_H
