#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/CGSCCPassManager.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/GraphWriter.h"
#include "llvm/Support/JSON.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

const std::string LogFilePath = "/tmp/llvm-lsp.log";

class Logger {
  std::ofstream logFile;

  std::string getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto now_c = std::chrono::system_clock::to_time_t(now);
    std::tm now_tm;
    localtime_r(&now_c, &now_tm);
    std::stringstream ss;
    ss << std::put_time(&now_tm, "%Y-%m-%d %H:%M:%S");
    return ss.str();
  }

public:
  Logger(const std::string &filename) : logFile(filename, std::ios::app) {
    assert(logFile.is_open() && "Failed to open log file");
  }
  ~Logger() { logFile.close(); }

  void log(const std::string &message) {
    logFile << "[" << getCurrentTimestamp() << "] " << message << std::endl;
  }
};

class OptRunner {
  Logger &logger;
  llvm::LLVMContext Context;
  std::unique_ptr<llvm::Module> InitialIR;
  std::optional<llvm::Module> FinalIR = std::nullopt;

  // Analysis Managers
  llvm::LoopAnalysisManager LAM;
  llvm::FunctionAnalysisManager FAM;
  llvm::CGSCCAnalysisManager CGAM;
  llvm::ModuleAnalysisManager MAM;

  llvm::ModulePassManager MPM;
  llvm::PassBuilder PB;
  llvm::PassInstrumentationCallbacks PIC;

  llvm::SmallVector<std::string, 256> PassList;

  std::filesystem::path ArtifactsFolderPath;

public:
  OptRunner(const std::string filename, Logger &logger,
            const std::string pipelineText)
      : logger(logger), PassList() {
    InitialIR = loadModuleFromIR(filename, Context);

    PIC.registerAfterPassCallback([this](const llvm::StringRef PassName,
                                         llvm::Any,
                                         const llvm::PreservedAnalyses &PA) {
      this->PassList.push_back(PassName.str());
    });

    PB = llvm::PassBuilder(nullptr, llvm::PipelineTuningOptions(), std::nullopt,
                           &PIC);

    PB.registerModuleAnalyses(MAM);
    PB.registerCGSCCAnalyses(CGAM);
    PB.registerFunctionAnalyses(FAM);
    PB.registerLoopAnalyses(LAM);
    PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

    // Parse Pipeline text
    auto ParseError = PB.parsePassPipeline(MPM, pipelineText);

    if (ParseError)
      logger.log("Error parsing pipeline text!");

    // Make Artifacts folder, if it does not exist
    std::filesystem::path filepath(filename);
    ArtifactsFolderPath = filepath.parent_path().string() + "/Artifacts-" +
                          filepath.stem().string() + "/";
    if (!std::filesystem::exists(ArtifactsFolderPath))
      std::filesystem::create_directory(ArtifactsFolderPath);
  }

  const llvm::SmallVectorImpl<std::string> &getPassList() {
    if (PassList.empty())
      runOpt();

    return PassList;
  }

  void runOpt() {
    PassList.clear();
    MPM.run(*InitialIR, MAM);
  }

  void generateGraphs() {
    for (auto &F : *InitialIR) {
      std::filesystem::path DotFilePath =
          ArtifactsFolderPath /
          std::filesystem::path(F.getName().str() + ".dot");
      // if (!std::filesystem::exists(DotFilePath))
      // FIXME: This generates an empty dot graph. This needs to fixed.
      llvm::WriteGraph(&F, F.getName(), false, "CFG for " + F.getName().str(),
                       DotFilePath.string());
      generateSVGFromDot(DotFilePath);
    }
  }

  std::string getArtifactsPath() { return ArtifactsFolderPath.string(); }

private:
  std::unique_ptr<llvm::Module> loadModuleFromIR(const std::string &filepath,
                                                 llvm::LLVMContext &context) {
    llvm::SMDiagnostic err;
    // Try to parse as textual IR
    auto module = llvm::parseIRFile(filepath, err, context);
    if (!module) {
      // If parsing failed, print the error
      logger.log("Failed parsing IR file: " + err.getMessage().str());
      return nullptr;
    }
    return module;
  }

  void generateSVGFromDot(std::filesystem::path Dotpath) {
    std::filesystem::path Svgpath =
        std::filesystem::path(Dotpath).replace_extension(".svg");
    std::string Cmd =
        "dot -Tsvg " + Dotpath.string() + " -o " + Svgpath.string();
    logger.log("Running command: " + Cmd);
    int result = std::system(Cmd.c_str());

    if (result == 0)
      logger.log("SVG Generated : " + Svgpath.string());
    else
      logger.log("Failed to generate SVG!");
  }
};

class LspServer {
  Logger logger;

public:
  LspServer(const std::string logfile) : logger(logfile) {
    logger.log("Starting LLVM LSP Server\n");
  }

  bool processRequest() {
    std::string msg = readMessage();
    logger.log("Received Message from Client: " + msg);
    if (msg.empty())
      return false;
    handleMessage(msg);
    return true;
  }

  void sendNotification(const std::string &method,
                        const llvm::json::Value &params) {
    llvm::json::Object notification{
        {"jsonrpc", "2.0"}, {"method", method}, {"params", params}};
    std::string output;
    llvm::raw_string_ostream outStr(output);
    outStr << llvm::json::Value(std::move(notification));
    std::cout << "Content-Length: " << output.size() << "\r\n\r\n" << output;
    std::cout.flush();
  }

private:
  // Returns the JSON String encoded in the message
  std::string readMessage() {
    std::string line;
    size_t contentLength = 0;
    // Read headers
    while (std::getline(std::cin, line) && !line.empty()) {
      logger.log("Received Message from Client: " + line);
      if (!line.empty() && line.back() == '\r')
        line.pop_back();
      if (line.empty())
        break; // End of headers

      if (line.find("Content-Length:") == 0)
        contentLength = std::stoi(line.substr(15));
      if (line.find("Content-Type") == 0)
        continue; // TODO: Handle Content-Type header
    }
    // Read body
    std::string jsonStr(contentLength, '\0');
    std::cin.read(&jsonStr[0], contentLength);
    return jsonStr;
  }

  void sendResponse(const llvm::json::Value &id,
                    const llvm::json::Value &result) {
    llvm::json::Object response{
        {"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
    std::string output;
    llvm::raw_string_ostream outStr(output);
    outStr << llvm::json::Value(std::move(response));
    std::cout << "Content-Length: " << output.size() << "\r\n\r\n" << output;
    std::cout.flush();
  }

  // Functions to handle various RPC calls
  void handleRequestInitialize(const llvm::json::Value *Id,
                               const llvm::json::Value *Params) {
    logger.log("Received Initialize Message!");
    llvm::json::Object notificationParams{
        {"type", 3}, // Info
        {"message", "Hello from LLVM IR LSP Server!"}};
    sendNotification(std::string("window/showMessage"),
                     llvm::json::Value(std::move(notificationParams)));

    llvm::json::Object responseParams{
        {"capabilities",
         llvm::json::Object{
             {"textDocumentSync",
              llvm::json::Object{
                  {"openClose", true},
                  {"change", 0}, // We dont want to sync the documents.
              }}}}};
    sendResponse(*Id, llvm::json::Value(std::move(responseParams)));
  }

  void handleRequestTextDocumentDidOpen(const llvm::json::Value *Id,
                                        const llvm::json::Value *Params) {
    logger.log("Received didOpen Message!");

    // Handle file open
    const llvm::json::Object *ParamsObj = Params->getAsObject();

    const llvm::json::Object *TextDocumentObj =
        ParamsObj->getObject("textDocument");
    assert(TextDocumentObj && "Expected valid textDocument field");

    const llvm::json::Value *UriValue = TextDocumentObj->get("uri");
    assert(UriValue && "Expected valid uri field");

    auto UriOpt = UriValue->getAsString();
    assert(UriOpt && "Expected valid uri value");

    constexpr llvm::StringLiteral FileScheme = "file://";
    assert(UriOpt->starts_with(FileScheme) &&
           "Uri For file must start with 'file://'");

    llvm::StringRef Filepath = UriOpt->drop_front(FileScheme.size());

    // Run Opt on the above file
    OptRunner optRunner(Filepath.str(), logger, "default<O3>");
    auto &PassList = optRunner.getPassList();

    std::string FirstFivePasses;
    for (unsigned i = 0; i < PassList.size(); ++i) {
      // logger.log("Ran Pass "+ std::to_string(i) +" '" + PassList[i] + "'");

      if (i < 5)
        FirstFivePasses = FirstFivePasses + PassList[i] + "\n";
    }

    // Get pass list when given IR is run with opt -O3.
    llvm::json::Object notificationParams{{"type", 3}, // Info
                                          {"message", FirstFivePasses}};
    sendNotification("window/showMessage",
                     llvm::json::Value(std::move(notificationParams)));

    // Generate Dot Graphs
    optRunner.generateGraphs();
    llvm::json::Object dotNotificationParams{
        {"type", 3}, // Info
        {"message", optRunner.getArtifactsPath()}};
    sendNotification("window/showMessage",
                     llvm::json::Value(std::move(dotNotificationParams)));
  }

  // Identifies RPC Call and dispatches the handling to other methods
  void handleMessage(const std::string &JsonStr) {
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
      handleRequestTextDocumentDidOpen(Id, Params);
    }
    // ... handle other LSP methods
  }
};

int main(int argc, char **argv) {
  LspServer lspServer(LogFilePath);

  while (lspServer.processRequest())
    ;
  return 0;
}
