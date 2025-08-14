//===-- llvm-lsp-server.cpp -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/BasicBlock.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/Program.h"

#include "IRDocument.h"
#include "Protocol.h"
#include "llvm-lsp-server.h"
#include "lsp-server-support/Logging.h"
#include "lsp-server-support/Protocol.h"
#include "lsp-server-support/Transport.h"
#include "llvm/ADT/StringRef.h"
#include <string>

using namespace llvm;

static cl::OptionCategory LlvmLspServerCategory("llvm-lsp-server options");
static cl::opt<std::string> LogFilePath("log-file",
                                        cl::desc("Path to log file"),
                                        cl::init("/tmp/llvm-lsp-server.log"),
                                        cl::cat(LlvmLspServerCategory));

static lsp::Position llvmFileLocToLspPosition(const FileLoc &Pos) {
  return lsp::Position(Pos.Line, Pos.Col);
}

static lsp::Range llvmFileLocRangeToLspRange(const FileLocRange &Range) {
  return lsp::Range(llvmFileLocToLspPosition(Range.Start),
                    llvmFileLocToLspPosition(Range.End));
}

llvm::Error LspServer::run() {
  registerMessageHandlers();
  return MessageHandler.run();
}

void LspServer::sendInfo(const std::string &Message) {
  ShowMessageSender(lsp::ShowMessageParams(lsp::MessageType::Info, Message));
}

void LspServer::sendError(const std::string &Message) {
  ShowMessageSender(lsp::ShowMessageParams(lsp::MessageType::Error, Message));
}

void LspServer::handleRequestInitialize(
    const lsp::InitializeParams &Params,
    lsp::Callback<llvm::json::Value> Reply) {
  lsp::Logger::info("Received Initialize Message!");
  sendInfo("Hello! Welcome to LLVM IR Language Server!");

  // clang-format off
  json::Object ResponseParams{
    {"capabilities",
      json::Object{
          {"textDocumentSync",
          json::Object{
              {"openClose", true},
              {"change", 0}, // We dont want to sync the documents.
          }
        },
        {"referencesProvider", true},
        {"codeActionProvider", true},
        {"documentSymbolProvider", true},
      }
    }
  };
  // clang-format on
  Reply(json::Value(std::move(ResponseParams)));
}

void LspServer::handleNotificationTextDocumentDidOpen(
    const lsp::DidOpenTextDocumentParams &Params) {
  lsp::Logger::info("Received didOpen Message!");
  StringRef Filepath = Params.textDocument.uri.file();
  sendInfo("LLVM Language Server Recognized that you opened " + Filepath.str());

  // Prepare IRDocument for Queries
  lsp::Logger::info("Creating IRDocument for {}", Filepath.str());
  OpenDocuments[Filepath.str()] = std::make_unique<IRDocument>(Filepath.str());
}

void LspServer::handleRequestGetReferences(
    const lsp::ReferenceParams &Params,
    lsp::Callback<std::vector<lsp::Location>> Reply) {
  auto Filepath = Params.textDocument.uri.file();
  auto Line = Params.position.line;
  auto Character = Params.position.character;
  assert(Line >= 0);
  assert(Character >= 0);
  std::stringstream SS;
  // SS << "Requested references for token: " << Filepath.str() << ":" << *Line
  //    << ":" << *Character;
  // sendInfo(SS.str());
  std::vector<lsp::Location> Result;
  const auto &Doc = OpenDocuments[Filepath.str()];
  if (Instruction *MaybeI = Doc->getInstructionAtLocation(Line, Character)) {
    auto AddReference = [&Result, &Params, &Doc](Instruction *I) {
      // FIXME: very hacky way to remove the newline from the reference...
      //   we need to have the parser set the proper end
      auto Start = Doc->ParserContext.getInstructionLocation(I).value().Start;
      auto End = Doc->ParserContext.getInstructionLocation(I).value().End;
      End.Line--;
      End.Col = 10000;
      Result.emplace_back(
          lsp::Location(Params.textDocument.uri,
                        lsp::Range(lsp::Position(Start.Line, Start.Col),
                                   lsp::Position(End.Line, End.Col))));
    };
    AddReference(MaybeI);
    for (User *U : MaybeI->users()) {
      if (auto *UserInst = dyn_cast<Instruction>(U)) {
        if (Doc->ParserContext.getInstructionLocation(UserInst))
          AddReference(UserInst);
      }
    }
  }

  Reply(std::move(Result));
}

void LspServer::handleRequestTextDocumentDocumentSymbol(
    const lsp::DocumentSymbolParams &Params,
    lsp::Callback<std::vector<lsp::DocumentSymbol>> Reply) {
  if (!OpenDocuments.contains(Params.textDocument.uri.file().str())) {
    lsp::Logger::error(
        "Document in textDocument/documentSymbol request not open: {}",
        Params.textDocument.uri.file());
    return Reply(
        make_error<lsp::LSPError>(formatv("Did not open file previously {}",
                                          Params.textDocument.uri.file()),
                                  lsp::ErrorCode::InvalidParams));
  }
  auto &Doc = OpenDocuments[Params.textDocument.uri.file().str()];
  std::vector<lsp::DocumentSymbol> Result;
  for (const auto &Fn : Doc->getFunctions()) {
    lsp::DocumentSymbol Func;
    Func.name = Fn.getNameOrAsOperand();
    Func.kind = lsp::SymbolKind::Function;
    auto MaybeLoc = Doc->ParserContext.getFunctionLocation(&Fn);
    if (!MaybeLoc)
      continue;
    Func.range = llvmFileLocRangeToLspRange(*MaybeLoc);
    Func.selectionRange = Func.range;
    for (const auto &BB : Fn) {
      lsp::DocumentSymbol Block;
      Block.name = BB.getNameOrAsOperand();
      Block.kind = lsp::SymbolKind::Namespace;
      Block.detail = "basic block";
      auto MaybeLoc = Doc->ParserContext.getBlockLocation(&BB);
      if (!MaybeLoc)
        continue;
      Block.range = llvmFileLocRangeToLspRange(*MaybeLoc);
      Block.selectionRange = Block.range;
      Func.children.emplace_back(std::move(Block));
    }
    Result.emplace_back(std::move(Func));
  }
  Reply(std::move(Result));
}

void LspServer::handleRequestCodeAction(const lsp::CodeActionParams &Params,
                                        lsp::Callback<json::Value> Reply) {
  Reply(json::Array{
      json::Object{{"title", "Open CFG Preview"}, {"command", "llvm.cfg"}}});
}

void LspServer::handleRequestGetCFG(const lsp::GetCfgParams &Params,
                                    lsp::Callback<lsp::CFG> Reply) {
  // TODO: have a flag to force regenerating the artifacts
  std::string Filepath = Params.uri.file().str();
  auto Line = Params.position.line;
  auto Character = Params.position.character;

  for (const auto &[K, _] : OpenDocuments) {
    lsp::Logger::debug("OpenDocuments: {}", K);
  }
  if (OpenDocuments.find(Filepath) == OpenDocuments.end()) {
    lsp::Logger::error("Did not open file previously {}", Filepath);
    return Reply(make_error<lsp::LSPError>(
        formatv("Did not open file previously {}", Filepath),
        lsp::ErrorCode::InvalidParams));
  }
  IRDocument &Doc = *OpenDocuments[Filepath];

  Function *F = nullptr;
  BasicBlock *BB = nullptr;
  if (Instruction *MaybeI =
          OpenDocuments[Filepath]->getInstructionAtLocation(Line, Character)) {
    BB = MaybeI->getParent();
    F = BB->getParent();
  } else {
    F = Doc.getFirstFunction();
    BB = &F->getEntryBlock();
  }

  auto PathOpt = Doc.getPathForSVGFile(F);
  if (!PathOpt)
    lsp::Logger::info("Did not find Path for SVG file for {}", Filepath);

  lsp::CFG Result;
  auto MaybeURI = lsp::URIForFile::fromFile(*PathOpt);
  if (!MaybeURI) {
    Reply(MaybeURI.takeError());
    return;
  }
  Result.uri = *MaybeURI;
  Result.node_id = Doc.getNodeId(Doc.getBlockAtLocation(Line, Character));
  Result.function = F->getName();

  Reply(Result);

  SVGToIRMap[*PathOpt] = Filepath;
}

void LspServer::handleRequestBBLocation(const lsp::BbLocationParams &Params,
                                        lsp::Callback<lsp::BbLocation> Reply) {
  auto Filepath = Params.uri.file();
  auto NodeIDStr = Params.node_id;

  // sendInfo("LLVM Language Server Recognized request to get Basicblock "
  //          "corresponding to SVG file " +
  //          Filepath.str() + ", Node ID: " + NodeIDStr->str());

  // We assume the query to SVGToIRMap would not fail.
  auto IR = SVGToIRMap[Filepath.str()];
  IRDocument &Doc = *OpenDocuments[IR];
  lsp::BbLocation Result;
  Result.range = llvmFileLocRangeToLspRange(Doc.parseNodeId(NodeIDStr));
  auto MaybeURI = lsp::URIForFile::fromFile(IR);
  if (!MaybeURI)
    return Reply(MaybeURI.takeError());
  Result.uri = *MaybeURI;
  return Reply(Result);
}

void LspServer::handleRequestGetPassList(const lsp::GetPassListParams &Params,
                                         lsp::Callback<lsp::PassList> Reply) {

  StringRef Filepath = Params.uri.file();
  std::string Pipeline = Params.pipeline;

  if (OpenDocuments.find(Filepath.str()) == OpenDocuments.end()) {
    lsp::Logger::error("Did not open file previously {}", Filepath.str());
    return Reply(make_error<lsp::LSPError>(
        formatv("Did not open file previously {}", Filepath.str()),
        lsp::ErrorCode::InvalidParams));
  }
  IRDocument &Doc = *OpenDocuments[Filepath.str()];

  lsp::Logger::info("Opened IR file to get pass list {}", Filepath.str());

  auto PassListResult = Doc.getPassList(Pipeline);

  if (!PassListResult) {
    // sendErrorResponse(*Id, InvalidParams,
    //                   "Error while getting pass list:" +
    //                       toString(PassListResult.takeError()));
    return Reply(PassListResult.takeError());
  }

  auto PassList = PassListResult.get();

  auto PassDescriptionsResult = Doc.getPassDescriptions(Pipeline);

  if (!PassDescriptionsResult) {
    // sendErrorResponse(*Id, InvalidParams,
    //                   "Error while getting pass descriptions:" +
    //                       toString(PassDescriptionsResult.takeError()));
    return Reply(PassDescriptionsResult.takeError());
  }

  auto PassDescriptions = PassDescriptionsResult.get();

  if (PassList.size() != PassDescriptions.size()) {
    lsp::Logger::error("Size mismatch between the objects!");
    return Reply(make_error<lsp::LSPError>("Size mismatch between the objects!",
                                           lsp::ErrorCode::InvalidParams));
  }

  // Build the response object
  lsp::PassList ResponseParams;
  ResponseParams.list.insert(ResponseParams.list.begin(), PassList.begin(),
                             PassList.end());
  ResponseParams.descriptions.insert(ResponseParams.descriptions.begin(),
                                     PassDescriptions.begin(),
                                     PassDescriptions.end());
  ResponseParams.status = "success";

  Reply(ResponseParams);
}

void LspServer::handleRequestGetIRAfterPass(
    const lsp::GetIRAfterPassParams &Params, lsp::Callback<lsp::IR> Reply) {
  StringRef Filepath = Params.uri.file();
  std::string Pipeline = Params.pipeline;

  if (OpenDocuments.find(Filepath.str()) == OpenDocuments.end()) {
    lsp::Logger::error("Did not open file previously {}", Filepath.str());
    return Reply(make_error<lsp::LSPError>(
        formatv("Did not open file previously {}", Filepath.str()),
        lsp::ErrorCode::InvalidParams));
  }
  IRDocument &Doc = *OpenDocuments[Filepath.str()];

  unsigned PassNum = Params.passnumber;
  auto IRFilePathResult = Doc.getIRAfterPassNumber(Pipeline, PassNum);

  if (!IRFilePathResult) {
    return Reply(IRFilePathResult.takeError());
  }

  auto IRFilePath = IRFilePathResult.get();

  if (auto MaybeIRUri = lsp::URIForFile::fromFile(IRFilePath)) {
    lsp::IR Return;
    Return.uri = *MaybeIRUri;
    Reply(Return);
  } else {
    return Reply(MaybeIRUri.takeError());
  }

  return;
}

bool LspServer::registerMessageHandlers() {
  MessageHandler.method("initialize", this,
                        &LspServer::handleRequestInitialize);

  MessageHandler.notification(
      "textDocument/didOpen", this,
      &LspServer::handleNotificationTextDocumentDidOpen);
  MessageHandler.method("textDocument/references", this,
                        &LspServer::handleRequestGetReferences);
  MessageHandler.method("textDocument/documentSymbol", this,
                        &LspServer::handleRequestTextDocumentDocumentSymbol);
  MessageHandler.method("textDocument/codeAction", this,
                        &LspServer::handleRequestCodeAction);
  // Custom messages
  MessageHandler.method("llvm/getCfg", this, &LspServer::handleRequestGetCFG);
  MessageHandler.method("llvm/bbLocation", this,
                        &LspServer::handleRequestBBLocation);
  MessageHandler.method("llvm/getPassList", this,
                        &LspServer::handleRequestGetPassList);
  MessageHandler.method("llvm/getIRAfterPass", this,
                        &LspServer::handleRequestGetIRAfterPass);

  ShowMessageSender =
      MessageHandler.outgoingNotification<lsp::ShowMessageParams>(
          "window/showMessage");

  // Return true to indicate handlers were registered successfully
  return true;
}

int main(int argc, char **argv) {
  cl::HideUnrelatedOptions(LlvmLspServerCategory);
  cl::ParseCommandLineOptions(argc, argv, "LLVM LSP Language Server");

  llvm::sys::ChangeStdinToBinary();
  lsp::JSONTransport Transport(stdin, llvm::outs());

  LspServer LS(Transport);

  lsp::Logger::setLogLevel(lsp::Logger::Level::Debug);

  auto LSResult = LS.run();
  if (!LSResult)
    lsp::Logger::error("Error while running Language Server: {}", LSResult);

  return LS.getExitCode();
}
