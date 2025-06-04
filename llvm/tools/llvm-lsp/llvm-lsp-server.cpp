#include "llvm-lsp-server.h"

// const std::string LogFilePath =
//     "/Users/manishkh/Documents/Hackathon/IRViz_2.0/lsp_server.log";
// const std::string LogFilePath = "/dev/stderr";
const std::string LogFilePath = "/tmp/llvm-lsp.log";

bool LspServer::processRequest() {
  std::string Msg = readMessage();
  LoggerObj.log("Received Message from Client: " + Msg);
  if (Msg.empty())
    return false;
  handleMessage(Msg);
  return true;
}

void LspServer::sendInfo(const std::string &Message) {
  llvm::json::Object NotificationParams{{"type", 3}, // Info
                                        {"message", Message}};
  sendNotification(std::string("window/showMessage"),
                   llvm::json::Value(std::move(NotificationParams)));
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

void LspServer::sendResponse(const llvm::json::Value &ID,
                             const llvm::json::Value &Response) {
  llvm::json::Object ResponseObj{
      {"jsonrpc", "2.0"}, {"id", ID}, {"result", Response}};
  std::string Output;
  llvm::raw_string_ostream OutStr(Output);
  OutStr << llvm::json::Value(std::move(ResponseObj));
  std::cout << "Content-Length: " << Output.size() << "\r\n\r\n" << Output;
  std::cout.flush();
}

void LspServer::sendNotification(const std::string &RPCMethod,
                                 const llvm::json::Value &Params) {
  llvm::json::Object NotificationObj{
      {"jsonrpc", "2.0"}, {"method", RPCMethod}, {"params", Params}};
  std::string Output;
  llvm::raw_string_ostream OutStr(Output);
  OutStr << llvm::json::Value(std::move(NotificationObj));
  std::cout << "Content-Length: " << Output.size() << "\r\n\r\n" << Output;
  std::cout.flush();
}

const llvm::json::Value *
LspServer::queryJSON(const llvm::json::Value *JSONObject,
                     llvm::StringRef Query) {
  llvm::SmallVector<std::string, 8> QueryComponents;
  auto SplitQuery = [&QueryComponents](std::string Query) {
    std::istringstream SS(Query);
    std::string Token;
    while (std::getline(SS, Token, '.')) {
      QueryComponents.push_back(Token);
    }
  };
  SplitQuery(Query.str());
  const llvm::json::Value *Current = JSONObject;
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

void LspServer::handleRequestInitialize(const llvm::json::Value *Id,
                                        const llvm::json::Value *Params) {
  LoggerObj.log("Received Initialize Message!");
  sendInfo("Hello! Welcome to LLVM IR Language Server!");

  // clang-format off
  llvm::json::Object ResponseParams{
    {"capabilities",
      llvm::json::Object{
          {"textDocumentSync",
          llvm::json::Object{
              {"openClose", true},
              {"change", 0}, // We dont want to sync the documents.
          }
        }
      }
    }
  };
  // clang-format on
  sendResponse(*Id, llvm::json::Value(std::move(ResponseParams)));
}

void LspServer::handleNotificationTextDocumentDidOpen(
    const llvm::json::Value *Id, const llvm::json::Value *Params) {
  LoggerObj.log("Received didOpen Message!");
  llvm::StringRef Filepath = queryJSONForFilePath(Params, "textDocument.uri");
  sendInfo("LLVM Language Server Recognized that you opened " + Filepath.str());
}

void LspServer::handleRequestCFGGen(const llvm::json::Value *Id,
                                    const llvm::json::Value *Params) {
  llvm::StringRef Filepath = queryJSONForFilePath(Params, "uri");
  // Run Opt on the above file
  OptRunner OR(Filepath.str(), LoggerObj);

  // Generate Dot and SVG Graphs
  OR.generateGraphs();

  // clang-format off
  llvm::json::Object ResponseParams{
  {"result",
    llvm::json::Object{
        {"uri", OR.getSVGFilePath()}
      }
    }
  };
  // clang-format on

  sendResponse(*Id, llvm::json::Value(std::move(ResponseParams)));

  SVGToIRMap[OR.getSVGFilePath()] = Filepath.str();
}

void LspServer::handleRequestGetCFGNode(const llvm::json::Value *Id,
                                        const llvm::json::Value *Params) {
  llvm::StringRef Filepath = queryJSONForFilePath(Params, "uri");
  llvm::StringRef LineNoStr = queryJSONForString(Params, "line");
  llvm::StringRef ColNoStr = queryJSONForString(Params, "col");

  sendInfo("LLVM Language Server Recognized request to get CFG Node "
           "corresponding to IR file " +
           Filepath.str() + " , Line No: " + LineNoStr.str() +
           ", Col No: " + ColNoStr.str());

  OptRunner OR(Filepath.str(), LoggerObj);
  OR.generateGraphs();

  // TODO: Insert logic to get Location in SVG File.

  // clang-format off
  llvm::json::Object ResponseParams{
  {"result",
    llvm::json::Object{
        {"nodeId", "0x000000"},
        {"uri", OR.getSVGFilePath()}
      }
    }
  };
  // clang-format on
  sendResponse(*Id, llvm::json::Value(std::move(ResponseParams)));
}

void LspServer::handleRequestGetBBLocation(const llvm::json::Value *Id,
                                           const llvm::json::Value *Params) {
  llvm::StringRef Filepath = queryJSONForFilePath(Params, "uri");
  llvm::StringRef NodeIDStr = queryJSONForString(Params, "nodeID");

  sendInfo("LLVM Language Server Recognized request to get Basicblock "
           "corresponding to SVG file " +
           Filepath.str() + " , Node ID: " + NodeIDStr.str());

  // We assume the query to SVGToIRMap would not fail.
  std::string IRFileName = SVGToIRMap[Filepath.str()];

  // TODO: Insert logic to get Location in IR file.

  // clang-format off
  llvm::json::Object ResponseParams{
  {"result",
    llvm::json::Object{
        {"from_line", "0"},
        {"from_col", "0"},
        {"to_line", "5"},
        {"to_col", "5"},
        {"uri", IRFileName}
      }
    }
  };
  // clang-format on
  sendResponse(*Id, llvm::json::Value(std::move(ResponseParams)));
}

void LspServer::handleMessage(const std::string &JsonStr) {
  auto Val = llvm::json::parse(JsonStr);
  assert(Val && "Error Parsing JSON String!");

  const llvm::json::Object *Obj = Val->getAsObject();
  assert(Obj && "Expected valid JSON Object!");

  std::string Method = Obj->getString("method")->str();

  const llvm::json::Value *Params = Obj->get("params");
  assert(Params && "Expected valid Params field!");

  const llvm::json::Value *Id = Obj->get("id");

  if (Method == "initialize") {
    assert(Id && "Expected valid ID field!");
    handleRequestInitialize(Id, Params);
  } else if (Method == "textDocument/didOpen") {
    handleNotificationTextDocumentDidOpen(Id, Params);
  } else if (Method == "llvm/getCfg") {
    handleRequestCFGGen(Id, Params);
  } else if (Method == "llvm/cfgNode") {
    sendInfo("Reminder: llvm/cfgNode is work in progress, sending dummy data!");
    handleRequestGetCFGNode(Id, Params);
  } else if (Method == "llvm/bbLocation") {
    sendInfo(
        "Reminder: llvm/bbLocation is work in progress, sending dummy data!");
    handleRequestGetBBLocation(Id, Params);
  } else {
    // TODO: handle other LSP methods
    sendInfo("[WIP] Unhandled RPC call : " + Method);
  }
}

int main() {
  LspServer LS(LogFilePath);

  while (LS.processRequest())
    ;
  return 0;
}
