#ifndef LLVM_LSP_SERVER_H
#define LLVM_LSP_SERVER_H

#include <sstream>

#include "llvm/Support/JSON.h"

#include "Logger.h"

class LspServer {
  Logger LoggerObj;

  enum class LspServerState {
    Starting,
    Initializing,
    Ready,
    ShuttingDown, // Received 'shutdown' message
    Exitted,      // Received 'exit' message
  } State = LspServerState::Starting;

  std::string stateToString(LspServerState S) {
    switch (S) {
    case LspServerState::Starting:
      return "Starting";
    case LspServerState::Initializing:
      return "Initializing";
    case LspServerState::Ready:
      return "Ready";
    case LspServerState::ShuttingDown:
      return "ShuttingDown";
    case LspServerState::Exitted:
      return "Exitted";
    }
    return "<UNKNOWN STATE>";
  }

  void switchToState(LspServerState NewState) {
    std::stringstream SS;
    SS << "Changing State from " << stateToString(State) << " to "
       << stateToString(NewState);
    LoggerObj.log(SS.str());
    State = NewState;
  }

  enum LspErrorCode {
    RequestDuringInitialization = -32002,
    ParseError = -32700,
    InvalidRequest = -32600,
    MethodNotFound = -32601,
    InvalidParams = -32602,
    InternalError = -32603,
  };

  llvm::DenseMap<llvm::StringRef, std::string> SVGToIRMap;

public:
  LspServer(const std::string Logfile) : LoggerObj(Logfile) {
    LoggerObj.log("Starting LLVM LSP Server");
  }

  // Receives one message via stdin and responds to it.
  bool processRequest();

  // Sends a message to client as INFO notification
  void sendInfo(const std::string &Message);

  // The process exit code, should be success only if the State is Exitted
  int getExitCode() { return State == LspServerState::Exitted ? 0 : 1; }

private:
  // Returns the JSON String encoded in the message
  std::string readMessage();

  // Send message (response with either success or error)
  void sendMessage(const llvm::json::Value &ID, const std::string &Kind,
                   const llvm::json::Value &Payload);

  // Given a Response message as JSON value, send it over stdout.
  void sendResponse(const llvm::json::Value &ID,
                    const llvm::json::Value &Response);
  void sendErrorResponse(const llvm::json::Value &ID, const int Code,
                         const std::string &Message);

  // Given a Notification message as JSON value, send it over stdout.
  void sendNotification(const std::string &RPCMethod,
                        const llvm::json::Value &Params);

  // Given a path into a JSON object, retrieve the sub-object.
  const llvm::json::Value *queryJSON(const llvm::json::Value *JSONObject,
                                     llvm::StringRef Query);

  // Specifically retrieve a String Object
  llvm::StringRef queryJSONForString(const llvm::json::Value *JSONObject,
                                     llvm::StringRef Query) {
    const llvm::json::Value *StrValue = queryJSON(JSONObject, Query);
    assert(StrValue && "Expected valid Query Object");

    auto StrOpt = StrValue->getAsString();
    assert(StrOpt && "Expected Query result to be string");

    return *StrOpt;
  }

  // Retrieve a String Object and check if it is a filepath.
  llvm::StringRef queryJSONForFilePath(const llvm::json::Value *JSONObject,
                                       llvm::StringRef Query) {
    llvm::StringRef PathValue = queryJSONForString(JSONObject, Query);

    constexpr llvm::StringLiteral FileScheme = "file://";
    assert(PathValue.starts_with(FileScheme) &&
           "Uri For file must start with 'file://'");

    llvm::StringRef Filepath = PathValue.drop_front(FileScheme.size());
    return Filepath;
  }

  // ---------- Functions to handle various RPC calls -----------------------

  // initialize
  void handleRequestInitialize(const llvm::json::Value *Id,
                               const llvm::json::Value *Params);
  // textDocument/didOpen
  void handleNotificationTextDocumentDidOpen(const llvm::json::Value *Id,
                                             const llvm::json::Value *Params);
  // llvm/getCfg
  void handleRequestCFGGen(const llvm::json::Value *Id,
                           const llvm::json::Value *Params);

  // llvm/cfgNode
  void handleRequestGetCFGNode(const llvm::json::Value *Id,
                               const llvm::json::Value *Params);

  // llvm/bbLocation
  void handleRequestGetBBLocation(const llvm::json::Value *Id,
                                  const llvm::json::Value *Params);

  // Identifies RPC Call and dispatches the handling to other methods
  bool handleMessage(const std::string &JsonStr);
};

#endif // LLVM_LSP_SERVER_H
