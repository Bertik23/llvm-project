#include "llvm/AsmParser/AsmParserState.h"

namespace llvm {

std::optional<FileLocRange>
AsmParserState::getFunctionLocation(const Function *F) const {
  if (!Functions.contains(F))
    return std::nullopt;
  return Functions.at(F);
}

std::optional<FileLocRange>
AsmParserState::getBlockLocation(const BasicBlock *BB) const {
  if (!Blocks.contains(BB))
    return std::nullopt;
  return Blocks.at(BB);
}

std::optional<FileLocRange>
AsmParserState::getInstructionLocation(const Instruction *I) const {
  if (!Instructions.contains(I))
    return std::nullopt;
  return Instructions.at(I);
}

std::optional< Function *>
AsmParserState::getFunctionAtLocation(const FileLocRange &Querry) const {
  for ( auto &[F, Loc] : Functions) {
    if (Loc.contains(Querry))
      return F;
  }
  return std::nullopt;
}

std::optional< Function *>
AsmParserState::getFunctionAtLocation(const FileLoc &Querry) const {
    return getFunctionAtLocation(FileLocRange(Querry, Querry));
}

std::optional< BasicBlock *> AsmParserState::getBlockAtLocation(
    const FileLocRange &Querry) const {
  for ( auto &[BB, Loc] : Blocks) {
    if (Loc.contains(Querry))
      return BB;
  }
  return std::nullopt;
}

std::optional< BasicBlock *>
AsmParserState::getBlockAtLocation(const FileLoc & Querry) const {
    return getBlockAtLocation(FileLocRange(Querry, Querry));
}

std::optional< Instruction *>
AsmParserState::getInstructionAtLocation(const FileLocRange &Querry) const {
  for ( auto &[I, Loc] : Instructions) {
    if (Loc.contains(Querry))
      return I;
  }
  return std::nullopt;
}

std::optional< Instruction *>
AsmParserState::getInstructionAtLocation(const FileLoc & Querry) const {
    return getInstructionAtLocation(FileLocRange(Querry, Querry));
}

bool AsmParserState::addFunctionLocation( Function * F, const FileLocRange & Loc) {
    return Functions.insert({F, Loc}).second;
}

bool AsmParserState::addBlockLocation( BasicBlock * BB, const FileLocRange & Loc) {
    return Blocks.insert({BB, Loc}).second;
}

bool AsmParserState::addInstructionLocation( Instruction * I, const FileLocRange & Loc) {
    return Instructions.insert({I, Loc}).second;
}

} // namespace llvm