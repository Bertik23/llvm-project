#include "OptRunner.h"
#include "llvm/Support/JSON.h"
#include <iostream>


class LspServer {
  Logger LoggerObj;

  llvm::DenseMap<llvm::StringRef, std::string> SVGToIRMap;

public:
  LspServer(const std::string Logfile) : LoggerObj(Logfile) {
    LoggerObj.log("Starting LLVM LSP Server\n");
  }

  // Receives one message via stdin and responds to it.
  bool processRequest();

  // Sends a message to client as INFO notification
  void sendInfo(const std::string &Message);

private:
  // Returns the JSON String encoded in the message
  std::string readMessage();

  // Given a Response message as JSON value, send it over stdout.
  void sendResponse(const llvm::json::Value &ID,
                    const llvm::json::Value &Response);

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
  void handleRequestCFGGen(const llvm::json::Value *Id, const llvm::json::Value *Params);

  // llvm/cfgNode                                     
  void handleRequestGetCFGNode(const llvm::json::Value *Id, const llvm::json::Value *Params);

  // llvm/bbLocation
  void handleRequestGetBBLocation(const llvm::json::Value *Id, const llvm::json::Value *Params);

  // Identifies RPC Call and dispatches the handling to other methods
  void handleMessage(const std::string &JsonStr);
};