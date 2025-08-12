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
#include "llvm/Support/JSON.h"
#include "llvm/Support/Program.h"

#include "IRDocument.h"
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
  StringRef Filepath = Params.TextDocument.Uri.file();
  sendInfo("LLVM Language Server Recognized that you opened " + Filepath.str());

  // Prepare IRDocument for Queries
  lsp::Logger::info("Creating IRDocument for {}", Filepath.str());
  OpenDocuments[Filepath.str()] = std::make_unique<IRDocument>(Filepath.str());
}

void LspServer::handleRequestGetReferences(
    const lsp::ReferenceParams &Params,
    lsp::Callback<std::vector<lsp::Location>> Reply) {
  auto Filepath = Params.TextDocument.Uri.file();
  auto Line = Params.Position.Line;
  auto Character = Params.Position.Character;
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
          lsp::Location(Params.TextDocument.Uri,
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
  if (!OpenDocuments.contains(Params.TextDocument.Uri.file().str())) {
    lsp::Logger::error(
        "Document in textDocument/documentSymbol request not open: {}",
        Params.TextDocument.Uri.file());
    return;
  }
  auto &Doc = OpenDocuments[Params.TextDocument.Uri.file().str()];
  std::vector<lsp::DocumentSymbol> Result;
  for (const auto &Fn : Doc->getFunctions()) {
    lsp::DocumentSymbol Func;
    Func.Name = Fn.getNameOrAsOperand();
    Func.Kind = lsp::SymbolKind::Function;
    auto MaybeLoc = Doc->ParserContext.getFunctionLocation(&Fn);
    if (!MaybeLoc)
      continue;
    Func.Range = llvmFileLocRangeToLspRange(*MaybeLoc);
    Func.SelectionRange = Func.Range;
    for (const auto &BB : Fn) {
      lsp::DocumentSymbol Block;
      Block.Name = BB.getNameOrAsOperand();
      Block.Kind = lsp::SymbolKind::Namespace;
      Block.Detail = "basic block";
      auto MaybeLoc = Doc->ParserContext.getBlockLocation(&BB);
      if (!MaybeLoc)
        continue;
      Block.Range = llvmFileLocRangeToLspRange(*MaybeLoc);
      Block.SelectionRange = Block.Range;
      Func.Children.emplace_back(std::move(Block));
    }
    Result.emplace_back(std::move(Func));
  }
  Reply(std::move(Result));
}

bool LspServer::registerMessageHandlers() {
  MessageHandler.method("initialize", this,
                        &LspServer::handleRequestInitialize);

  // Ignored for now

  MessageHandler.notification(
      "textDocument/didOpen", this,
      &LspServer::handleNotificationTextDocumentDidOpen);
  MessageHandler.method("textDocument/references", this,
                        &LspServer::handleRequestGetReferences);
  MessageHandler.method("textDocument/documentSymbol", this,
                        &LspServer::handleRequestTextDocumentDocumentSymbol);

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

  auto LSResult = LS.run();
  if (!LSResult)
    lsp::Logger::error("Error while running Language Server: {}", LSResult);

  return LS.getExitCode();
}
