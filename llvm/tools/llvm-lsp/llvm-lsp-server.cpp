#include <iostream>

#include "llvm/Support/CommandLine.h"

#include "OptRunner.h"
#include "llvm-lsp-server.h"

using namespace llvm;

static cl::OptionCategory LlvmLspServerCategory("llvm-lsp-server options");
static cl::opt<std::string> LogFilePath("log-file",
                                        cl::desc("Path to log file"),
                                        cl::init("/tmp/llvm-lsp-server.log"),
                                        cl::cat(LlvmLspServerCategory));

bool LspServer::processRequest() {
  std::string Msg = readMessage();
  LoggerObj.log("Received Message from Client: " + Msg);
  // TODO: Should we really quit on empty message?
  if (Msg.empty())
    return false;
  return handleMessage(Msg);
}

void LspServer::sendInfo(const std::string &Message) {
  json::Object NotificationParams{{"type", 3}, // Info
                                  {"message", Message}};
  sendNotification(std::string("window/showMessage"),
                   json::Value(std::move(NotificationParams)));
}

std::string LspServer::readMessage() {
  std::string Line;
  size_t ContentLength = 0;
  // Read headers
  while (std::getline(std::cin, Line) && !Line.empty()) {
    LoggerObj.log("Received Message from Client: " + Line);
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

void LspServer::sendErrorResponse(const llvm::json::Value &ID, const int Code,
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

void LspServer::handleRequestInitialize(const json::Value *Id,
                                        const json::Value *Params) {
  LoggerObj.log("Received Initialize Message!");
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
      }
    }
  };
  // clang-format on
  sendResponse(*Id, json::Value(std::move(ResponseParams)));
}

void LspServer::handleNotificationTextDocumentDidOpen(
    const json::Value *Id, const json::Value *Params) {
  LoggerObj.log("Received didOpen Message!");
  StringRef Filepath = queryJSONForFilePath(Params, "textDocument.uri");
  sendInfo("LLVM Language Server Recognized that you opened " + Filepath.str());
}

void LspServer::handleRequestCFGGen(const json::Value *Id,
                                    const json::Value *Params) {
  StringRef Filepath = queryJSONForFilePath(Params, "uri");
  // Run Opt on the above file
  OptRunner OR(Filepath.str(), LoggerObj);

  // Generate Dot and SVG Graphs
  OR.generateGraphs();

  // clang-format off
  json::Object ResponseParams{
  {"result",
    json::Object{
        {"uri", OR.getSVGFilePath()}
      }
    }
  };
  // clang-format on

  sendResponse(*Id, json::Value(std::move(ResponseParams)));

  SVGToIRMap[OR.getSVGFilePath()] = Filepath.str();
}

void LspServer::handleRequestGetCFGNode(const json::Value *Id,
                                        const json::Value *Params) {
  StringRef Filepath = queryJSONForFilePath(Params, "uri");
  StringRef LineNoStr = queryJSONForString(Params, "line");
  StringRef ColNoStr = queryJSONForString(Params, "col");

  sendInfo("LLVM Language Server Recognized request to get CFG Node "
           "corresponding to IR file " +
           Filepath.str() + " , Line No: " + LineNoStr.str() +
           ", Col No: " + ColNoStr.str());

  OptRunner OR(Filepath.str(), LoggerObj);
  OR.generateGraphs();

  // TODO: Insert logic to get Location in SVG File.

  // clang-format off
  json::Object ResponseParams{
  {"result",
    json::Object{
        {"nodeId", "0x000000"},
        {"uri", OR.getSVGFilePath()}
      }
    }
  };
  // clang-format on
  sendResponse(*Id, json::Value(std::move(ResponseParams)));
}

void LspServer::handleRequestGetBBLocation(const json::Value *Id,
                                           const json::Value *Params) {
  StringRef Filepath = queryJSONForFilePath(Params, "uri");
  StringRef NodeIDStr = queryJSONForString(Params, "nodeID");

  sendInfo("LLVM Language Server Recognized request to get Basicblock "
           "corresponding to SVG file " +
           Filepath.str() + " , Node ID: " + NodeIDStr.str());

  // We assume the query to SVGToIRMap would not fail.
  std::string IRFileName = SVGToIRMap[Filepath.str()];

  // TODO: Insert logic to get Location in IR file.

  // clang-format off
  json::Object ResponseParams{
  {"result",
    json::Object{
        {"from_line", "0"},
        {"from_col", "0"},
        {"to_line", "5"},
        {"to_col", "5"},
        {"uri", IRFileName}
      }
    }
  };
  // clang-format on
  sendResponse(*Id, json::Value(std::move(ResponseParams)));
}

bool LspServer::handleMessage(const std::string &JsonStr) {
  auto Val = json::parse(JsonStr);
  assert(Val && "Error Parsing JSON String!");

  const json::Object *Obj = Val->getAsObject();
  assert(Obj && "Expected valid JSON Object!");

  const std::string Method = Obj->getString("method")->str();
  const json::Value *Id = Obj->get("id");
  const json::Value *Params = Obj->get("params");

  // TODO: some methods can only be sent once, enforce it
  //   these include: initialize, initialized, shutdown, exit
  switch (State) {

  case LspServerState::Starting: {
    if (Method == "initialize") {
      assert(Id && "Expected valid ID field!");
      assert(Params && "Expected valid Params field!");
      switchToState(LspServerState::Initializing);
      handleRequestInitialize(Id, Params);
      return true;
    }
    // For requests, reply with error code
    if (Id) {
      sendErrorResponse(*Id, LspErrorCode::RequestDuringInitialization, "");
      return true;
    }
    // For notifications, only process 'exit'
    if (Method == "exit") {
      switchToState(LspServerState::Exitted);
      return false;
    }
    // Ignore the rest
    return true;
  }

  case LspServerState::Initializing: {
    if (Method == "initialized") {
      switchToState(LspServerState::Ready);
      return true;
    }
    // TODO: shouldn't be getting anything, for now just ignore it if we do
    return true;
  }

  case LspServerState::Ready: {
    if (Method == "shutdown") {
      switchToState(LspServerState::ShuttingDown);
      sendErrorResponse(*Id, LspErrorCode::InvalidRequest, "");
      return true;
    }

    // Ignored for now
    if (Method == "textDocument/hover")
      return true;
    if (Method == "$/cancelRequest")
      return true;

    if (Method == "textDocument/didOpen") {
      handleNotificationTextDocumentDidOpen(Id, Params);
      return true;
    }
    if (Method == "textDocument/references") {
      // TODO
      return true;
    }
    if (Method == "llvm/getCfg") {
      handleRequestCFGGen(Id, Params);
      return true;
    }
    if (Method == "llvm/cfgNode") {
      sendInfo(
          "Reminder: llvm/cfgNode is work in progress, sending dummy data!");
      handleRequestGetCFGNode(Id, Params);
      return true;
    }
    if (Method == "llvm/bbLocation") {
      sendInfo(
          "Reminder: llvm/bbLocation is work in progress, sending dummy data!");
      handleRequestGetBBLocation(Id, Params);
      return true;
    }
    // TODO: handle other LSP methods
    sendInfo("[WIP] Unhandled RPC call : " + Method);
    return true;
  }

  case LspServerState::ShuttingDown: {
    if (Method == "exit") {
      switchToState(LspServerState::Exitted);
      // TODO: not sure what's wrong but restarting the lsp from vscode never
      // gets here... figure out why
      LoggerObj.log("Bye!");
      return false;
    }
    // TODO: shouldn't be getting anything, for now just quit if we do
    return false;
  }

  case LspServerState::Exitted: {
    // TODO: shouldn't be getting anything, for now just quit if we do
    return false;
  }
  }
  // silence warning
  return true;
}

int main(int argc, char **argv) {
  cl::HideUnrelatedOptions(LlvmLspServerCategory);
  cl::ParseCommandLineOptions(argc, argv);

  LspServer LS(LogFilePath);

  while (LS.processRequest())
    ;

  return LS.getExitCode();
}
