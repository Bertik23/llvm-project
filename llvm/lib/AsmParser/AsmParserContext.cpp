#include "llvm/AsmParser/AsmParserContext.h"

namespace llvm {

std::optional<FileLocRange>
AsmParserContext::getFunctionLocation(const Function *F) const {
  if (!Functions.contains(F))
    return std::nullopt;
  return Functions.at(F);
}

std::optional<FileLocRange>
AsmParserContext::getBlockLocation(const BasicBlock *BB) const {
  if (!Blocks.contains(BB))
    return std::nullopt;
  return Blocks.at(BB);
}

std::optional<FileLocRange>
AsmParserContext::getInstructionLocation(const Instruction *I) const {
  if (!Instructions.contains(I))
    return std::nullopt;
  return Instructions.at(I);
}

std::optional<Function *>
AsmParserContext::getFunctionAtLocation(const FileLocRange &Querry) const {
  for ( auto &[F, Loc] : Functions) {
    if (Loc.contains(Querry))
      return F;
  }
  return std::nullopt;
}

std::optional<Function *>
AsmParserContext::getFunctionAtLocation(const FileLoc &Querry) const {
  return getFunctionAtLocation(FileLocRange(Querry, Querry));
}

std::optional<BasicBlock *>
AsmParserContext::getBlockAtLocation(const FileLocRange &Querry) const {
  for ( auto &[BB, Loc] : Blocks) {
    if (Loc.contains(Querry))
      return BB;
  }
  return std::nullopt;
}

std::optional<BasicBlock *>
AsmParserContext::getBlockAtLocation(const FileLoc &Querry) const {
  return getBlockAtLocation(FileLocRange(Querry, Querry));
}

std::optional<Instruction *>
AsmParserContext::getInstructionAtLocation(const FileLocRange &Querry) const {
  for ( auto &[I, Loc] : Instructions) {
    if (Loc.contains(Querry))
      return I;
  }
  return std::nullopt;
}

std::optional<Instruction *>
AsmParserContext::getInstructionAtLocation(const FileLoc &Querry) const {
  return getInstructionAtLocation(FileLocRange(Querry, Querry));
}

bool AsmParserContext::addFunctionLocation(Function *F,
                                           const FileLocRange &Loc) {
  return Functions.insert({F, Loc}).second;
}

bool AsmParserContext::addBlockLocation(BasicBlock *BB,
                                        const FileLocRange &Loc) {
  return Blocks.insert({BB, Loc}).second;
}

bool AsmParserContext::addInstructionLocation(Instruction *I,
                                              const FileLocRange &Loc) {
  return Instructions.insert({I, Loc}).second;
}

} // namespace llvm