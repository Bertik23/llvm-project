//===-- llvm-lsp-server.cpp -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <iostream>

#include "llvm/IR/BasicBlock.h"
#include "llvm/Support/CommandLine.h"
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

llvm::Error LspServer::run() {
  handleMessage();
  return MessageHandler.run();
}

void LspServer::sendInfo(const std::string &Message) {
  json::Object NotificationParams{{"type", 3}, // Info
                                  {"message", Message}};
  sendNotification(std::string("window/showMessage"),
                   json::Value(std::move(NotificationParams)));
}

void LspServer::sendError(const std::string &Message) {
  json::Object NotificationParams{{"type", 1}, // Error
                                  {"message", Message}};
  sendNotification(std::string("window/showMessage"),
                   json::Value(std::move(NotificationParams)));
}

std::string LspServer::readMessage() {
  std::string Line;
  size_t ContentLength = 0;
  // Read headers
  while (std::getline(std::cin, Line) && !Line.empty()) {
    lsp::Logger::info("Received Message from Client: {}", Line);
    if (!Line.empty() && Line.back() == '\r')
      Line.pop_back();
    if (Line.empty())
      break; // End of headers

    if (Line.find("Content-Length:") == 0)
      ContentLength = std::stoi(Line.substr(15));
    if (Line.find("Content-Type") == 0)
      continue; // TODO: Handle Content-Type header
  }
  // Read body
  std::string JsonStr(ContentLength, '\0');
  std::cin.read(&JsonStr[0], ContentLength);
  return JsonStr;
}

void LspServer::sendMessage(const json::Value &ID, const std::string &Kind,
                            const json::Value &Payload) {
  json::Object ResponseObj{{"jsonrpc", "2.0"}, {"id", ID}, {Kind, Payload}};
  std::string Output;
  raw_string_ostream OutStr(Output);
  OutStr << json::Value(std::move(ResponseObj));
  std::cout << "Content-Length: " << Output.size() << "\r\n\r\n" << Output;
  std::cout.flush();
}

void LspServer::sendResponse(const json::Value &ID,
                             const json::Value &Response) {
  sendMessage(ID, "result", Response);
}

void LspServer::sendErrorResponse(const json::Value &ID, const int Code,
                                  const std::string &Message) {
  sendMessage(ID, "error", json::Object{{"code", Code}, {"message", Message}});
}

void LspServer::sendNotification(const std::string &RPCMethod,
                                 const json::Value &Params) {
  json::Object NotificationObj{
      {"jsonrpc", "2.0"}, {"method", RPCMethod}, {"params", Params}};
  std::string Output;
  raw_string_ostream OutStr(Output);
  OutStr << json::Value(std::move(NotificationObj));
  std::cout << "Content-Length: " << Output.size() << "\r\n\r\n" << Output;
  std::cout.flush();
}

const json::Value *LspServer::queryJSON(const json::Value *JSONObject,
                                        StringRef Query) {
  SmallVector<std::string, 8> QueryComponents;
  auto SplitQuery = [&QueryComponents](std::string Query) {
    std::istringstream SS(Query);
    std::string Token;
    while (std::getline(SS, Token, '.')) {
      QueryComponents.push_back(Token);
    }
  };
  SplitQuery(Query.str());
  const json::Value *Current = JSONObject;
  for (const auto &Key : QueryComponents) {
    if (const auto *Obj = Current->getAsObject()) {
      auto It = Obj->find(Key);
      if (It == Obj->end())
        return nullptr;
      Current = &It->getSecond();
    } else {
      return nullptr;
    }
  }
  return Current;
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
        {"hoverProvider", true},
        {"codeActionProvider", true},
        {"definitionProvider", true},
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

static json::Object fileLocToJSON(FileLoc FL) {
  return json::Object{{"line", FL.Line}, {"character", FL.Col}};
}

static json::Object fileLocRangeToJSON(FileLocRange FLR) {
  return json::Object{{"start", fileLocToJSON(FLR.Start)},
                      {"end", fileLocToJSON(FLR.End)}};
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

void LspServer::handleRequestCodeAction(
    const lsp::CodeActionParams &Params,
    lsp::Callback<llvm::json::Value> Reply) {
  Reply(json::Array{
      json::Object{{"title", "Open CFG Preview"}, {"command", "llvm.cfg"}}});
}

void LspServer::handleRequestTextDocumentDefinition(
    const lsp::TextDocumentPositionParams &Params,
    lsp::Callback<std::vector<lsp::Location>> Reply) {
  StringRef Filepath = Params.textDocument.uri.file();
  unsigned Line = Params.position.line;
  unsigned Col = Params.position.character;

  lsp::Logger::info("Recognized request : {}, Line: {}, Col: {}",
                    Filepath.str(), std::to_string(Line), std::to_string(Col));

  if (OpenDocuments.find(Filepath.str()) == OpenDocuments.end())
    lsp::Logger::error("Did not open file previously {}", Filepath.str());
  IRDocument &Doc = *OpenDocuments[Filepath.str()];

  const Function *F = Doc.getFunctionAtLocation(Line, Col);
  if (!F)
    sendInfo("You clicked on a region that is not inside any function!");
  else
    sendInfo("You clicked on Function : " + F->getName().str());

  // clang-format off
  // Sending path to same file
  json::Object ResponseParams{
    {"uri", "file://" + Filepath.str()},
    {"range",
      json::Object{
        {"start", json::Object{{"line", 0}, {"character", 0}}},
        {"end", json::Object{{"line", 5}, {"character", 0}}}
        }  
      }
  };
  // clang-format on
  Reply(std::vector<lsp::Location>());
}

bool LspServer::handleMessage() {
  MessageHandler.method("initialize", this,
                        &LspServer::handleRequestInitialize);

  // Ignored for now

  MessageHandler.notification(
      "textDocument/didOpen", this,
      &LspServer::handleNotificationTextDocumentDidOpen);
  MessageHandler.method("textDocument/references", this,
                        &LspServer::handleRequestGetReferences);
  MessageHandler.method("textDocument/codeAction", this,
                        &LspServer::handleRequestCodeAction);
  MessageHandler.method("textDocument/definition", this,
                        &LspServer::handleRequestTextDocumentDefinition);

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
