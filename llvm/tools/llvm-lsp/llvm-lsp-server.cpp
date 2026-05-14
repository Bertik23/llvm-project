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
#include "llvm/Support/FormatAdapters.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/LSP/Protocol.h"
#include "llvm/Support/Program.h"

#include "IRDocument.h"
#include "Protocol.h"
#include "llvm-lsp-server.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include <atomic>
#include <optional>
#include <string>
#include <utility>

using namespace llvm;

static cl::OptionCategory LlvmLspServerCategory("llvm-lsp-server options");
static cl::opt<std::string> LogFilePath("log-file",
                                        cl::desc("Path to log file"),
                                        cl::init("/tmp/llvm-lsp-server.log"),
                                        cl::cat(LlvmLspServerCategory));

static cl::opt<std::string> OptPath("opt-path", cl::desc("Path to opt file"),
                                    cl::init(""),
                                    cl::cat(LlvmLspServerCategory));

static lsp::Position llvmFileLocToLspPosition(const FileLoc &Pos) {
  return lsp::Position(Pos.Line, Pos.Col);
}

static lsp::Range llvmFileLocRangeToLspRange(const FileLocRange &Range) {
  return lsp::Range(llvmFileLocToLspPosition(Range.Start),
                    llvmFileLocToLspPosition(Range.End));
}

static FileLoc lspPositionToLlvmFileLoc(const lsp::Position &Pos) {
  return FileLoc(Pos.line, Pos.character);
}

static FileLocRange lspRangeToLlvmFileLocRange(const lsp::Range &Range) {
  return FileLocRange(lspPositionToLlvmFileLoc(Range.start),
                      lspPositionToLlvmFileLoc(Range.end));
}

llvm::Error LspServer::run() {
  registerMessageHandlers();
  return Transport.run(MessageHandler);
}

void LspServer::sendInfo(const std::string &Message) {
  ShowMessageSender(lsp::ShowMessageParams(lsp::MessageType::Info, Message));
}

void LspServer::sendError(const std::string &Message) {
  ShowMessageSender(lsp::ShowMessageParams(lsp::MessageType::Error, Message));
}

void LspServer::sendDiagnostics(
    const lsp::URIForFile &File,
    const std::vector<lsp::Diagnostic> &Diagnostics) {
  static std::atomic_int DiagnosticsVersion = 0;
  DiagnosticsVersion += 1;
  auto Params = lsp::PublishDiagnosticsParams(File, DiagnosticsVersion);
  Params.diagnostics = Diagnostics;

  PublishDiagnosticsSender(Params);
}

void LspServer::handleRequestInitialize(
    const lsp::InitializeParams &Params,
    lsp::Callback<llvm::json::Value> Reply) {
  lsp::Logger::info("Received Initialize Message!");
  sendInfo("Hello! Welcome to LLVM IR Language Server!");

  // Advertise server capabilities. We support full text sync and specific
  // language features like references, definitions, and code actions.
  // clang-format off
  json::Object ResponseParams{
    {"capabilities",
      json::Object{
          {"textDocumentSync",
          json::Object{
              {"openClose", true},
              {"change", 1},
              {"save", true},
          }
        },
        {"referencesProvider", true},
        {"codeActionProvider", true},
        {"documentSymbolProvider", true},
        {"definitionProvider", true},
        {"hoverProvider", true},
      }
    }
  };
  // clang-format on
  Reply(json::Value(std::move(ResponseParams)));
}

lsp::Diagnostic parserDiagnosticsToLSPDiagnostics(const SMDiagnostic &Diag) {
  // This function was generated using Google AI
  lsp::Diagnostic LspDiag;

  // 1. Map Severity
  // LLVM SourceMgr kinds: Error, Warning, Note, Remark
  switch (Diag.getKind()) {
  case llvm::SourceMgr::DK_Error:
    LspDiag.severity = lsp::DiagnosticSeverity::Error;
    break;
  case llvm::SourceMgr::DK_Warning:
    LspDiag.severity = lsp::DiagnosticSeverity::Warning;
    break;
  case llvm::SourceMgr::DK_Note:
    LspDiag.severity = lsp::DiagnosticSeverity::Information;
    break;
  case llvm::SourceMgr::DK_Remark:
    LspDiag.severity = lsp::DiagnosticSeverity::Hint;
    break;
  default:
    LspDiag.severity = lsp::DiagnosticSeverity::Error;
    break;
  }

  // 2. Map Message
  LspDiag.message = Diag.getMessage().str();

  // 3. Set Source
  LspDiag.source = "llvm";

  // 4. Map Range
  // LLVM internals use 1-based indexing for lines and columns (usually),
  // whereas LSP uses 0-based indexing.
  const llvm::SourceMgr *SM = Diag.getSourceMgr();
  auto Ranges = Diag.getRanges();

  if (!Ranges.empty() && SM) {
    // If the diagnostic comes with an explicit range (highlighting), use it.
    // We generally pick the first range if multiple are provided.
    auto R = Ranges.front();

    LspDiag.range.start.line = Diag.getLineNo();
    LspDiag.range.start.character = R.first;

    LspDiag.range.end.line = Diag.getLineNo();
    LspDiag.range.end.character = R.second;
  } else {
    // Fallback: If no ranges are provided, use the caret location
    // (LineNo/ColumnNo). SMDiagnostic::getLineNo() is 1-based.
    // SMDiagnostic::getColumnNo() is 0-based or 1-based depending on internal
    // flags, but typically represents the offset.

    int Line = Diag.getLineNo();
    int Col = Diag.getColumnNo();

    // Handle cases where location is unknown (-1)
    if (Line > 0) {
      LspDiag.range.start.line = Line - 1;
      LspDiag.range.start.character = (Col >= 0) ? Col : 0;

      // Create a range of length 1 so the squiggle is visible in the editor
      LspDiag.range.end.line = Line - 1;
      LspDiag.range.end.character = LspDiag.range.start.character + 1;
    } else {
      // If completely unknown location, default to 0,0
      LspDiag.range.start.line = 0;
      LspDiag.range.start.character = 0;
      LspDiag.range.end.line = 0;
      LspDiag.range.end.character = 0;
    }
  }

  return LspDiag;
}

void LspServer::handleNotificationTextDocumentDidOpen(
    const lsp::DidOpenTextDocumentParams &Params) {
  lsp::Logger::info("Received didOpen Message!");
  auto Filepath = Params.textDocument.uri;
  sendInfo("LLVM Language Server Recognized that you opened " +
           Filepath.uri().str());

  // Create the IRDocument state. This parses the module and keeps it in memory.
  lsp::Logger::info("Creating IRDocument for {}", Filepath);
  OpenDocuments[Filepath] = std::make_unique<IRDocument>(
      Filepath.file().str(), Params.textDocument.text,
      OptPath == "" ? std::nullopt : std::optional(OptPath.getValue()));
  if (auto &Doc = OpenDocuments[Filepath]; Doc->error()) {
    sendError(Doc->error()->getMessage().str());
    sendDiagnostics(
        Params.textDocument.uri,
        std::vector{parserDiagnosticsToLSPDiagnostics(*Doc->error())});
    OpenDocuments.erase(Filepath);
  }
  sendDiagnostics(Params.textDocument.uri, {});
}

void LspServer::handleNotificationTextDocumentDidChange(
    const lsp::DidChangeTextDocumentParams &Params) {
  lsp::Logger::info("Received didChange Message!");
  auto Filepath = Params.textDocument.uri;
  // Re-parse the module on update. Since we requested Full sync (change=1),
  // we get the complete text content here.
  lsp::Logger::info("Replacing IRDocument for {}", Filepath);
  OpenDocuments[Filepath] = std::make_unique<IRDocument>(
      Filepath.file().str(), Params.contentChanges.front().text,
      OptPath == "" ? std::nullopt : std::optional(OptPath.getValue()));
  if (auto &Doc = OpenDocuments[Filepath]; Doc->error()) {
    sendDiagnostics(
        Params.textDocument.uri,
        std::vector{parserDiagnosticsToLSPDiagnostics(*Doc->error())});
    OpenDocuments.erase(Filepath);
  } else {
    // Send empty diagnostic to clear error
    sendDiagnostics(Params.textDocument.uri, {});
  }
}

void LspServer::handleNotificationTextDocumentDidSave(
    const lsp::DidSaveTextDocumentParams &Params) {
  lsp::Logger::info("Received didSave Message!");
  auto Filepath = Params.textDocument.uri;

  // Regenerate auxiliary graphs (CFG, etc.) whenever the source is saved.
  lsp::Logger::info("Making graphs for {}", Filepath);
  OpenDocuments[Filepath]->makeGraphs();
}

void LspServer::handleRequestGetReferences(
    const lsp::ReferenceParams &Params,
    lsp::Callback<std::vector<lsp::Location>> Reply) {
  auto Filepath = Params.textDocument.uri;
  auto Line = Params.position.line;
  auto Character = Params.position.character;
  assert(Line >= 0);
  assert(Character >= 0);
  std::stringstream SS;
  std::vector<lsp::Location> Result;
  const auto &Doc = OpenDocuments[Filepath];
  if (Value *MaybeIA =
          Doc->getInstructionOrArgumentAtLocation(Line, Character)) {
    auto TryAddReference = [&Result, &Params, &Doc](Value *IA) {
      auto MaybeIALocation =
          Doc->ParserContext.getInstructionOrArgumentLocation(IA);
      if (!MaybeIALocation)
        return;
      Result.emplace_back(
          lsp::Location(Params.textDocument.uri,
                        llvmFileLocRangeToLspRange(MaybeIALocation.value())));
    };
    TryAddReference(MaybeIA);
    for (User *U : MaybeIA->users()) {
      if (auto *UserInst = dyn_cast<Instruction>(U)) {
        TryAddReference(UserInst);
      }
    }
  }

  Reply(std::move(Result));
}

void LspServer::handleRequestTextDocumentDocumentSymbol(
    const lsp::DocumentSymbolParams &Params,
    lsp::Callback<std::vector<lsp::DocumentSymbol>> Reply) {
  if (!OpenDocuments.contains(Params.textDocument.uri)) {
    lsp::Logger::error(
        "Document in textDocument/documentSymbol request not open: {}",
        Params.textDocument.uri.file());
    return Reply(
        make_error<lsp::LSPError>(formatv("Did not open file previously {}",
                                          Params.textDocument.uri.file()),
                                  lsp::ErrorCode::InvalidParams));
  }
  auto &Doc = OpenDocuments[Params.textDocument.uri];
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
      Block.kind =
          lsp::SymbolKind::Namespace; // Using namespace as there is no block
                                      // kind, and namespace is the closest
      Block.detail = "basic block";
      auto MaybeLoc = Doc->ParserContext.getBlockLocation(&BB);
      if (!MaybeLoc)
        continue;
      Block.range = llvmFileLocRangeToLspRange(*MaybeLoc);
      Block.selectionRange = Block.range;
      for (const auto &I : BB) {
        lsp::DocumentSymbol Inst;
        Inst.name = I.getNameOrAsOperand();
        Inst.kind = lsp::SymbolKind::Variable;
        {
          raw_string_ostream Ss(Inst.detail);
          I.print(Ss);
        }
        auto MaybeLoc = Doc->ParserContext.getInstructionOrArgumentLocation(&I);
        if (!MaybeLoc)
          continue;
        Inst.range = llvmFileLocRangeToLspRange(*MaybeLoc);
        Inst.selectionRange = Inst.range;
        Block.children.emplace_back(std::move(Inst));
      }
      Func.children.emplace_back(std::move(Block));
    }
    Result.emplace_back(std::move(Func));
  }
  Reply(std::move(Result));
}

void LspServer::handleRequestCodeAction(const lsp::CodeActionParams &Params,
                                        lsp::Callback<json::Value> Reply) {
  if (auto It = OpenDocuments.find(Params.textDocument.uri);
      It != OpenDocuments.end() &&
      It->second->getFunctionAtLocation(Params.range.start.line,
                                        Params.range.start.character))
    return Reply(json::Array{
        json::Object{{"title", "Open CFG Preview"}, {"command", "llvm.cfg"}}});
  return Reply(json::Array());
}

void LspServer::handleRequestTextDocumentDefinition(
    const lsp::TextDocumentPositionParams &Params,
    lsp::Callback<std::optional<lsp::Location>> Reply) {
  auto DocIt = OpenDocuments.find(Params.textDocument.uri);
  if (DocIt == OpenDocuments.end()) {
    lsp::Logger::error(
        "Document in textDocument/documentSymbol request not open: {}",
        Params.textDocument.uri.file());
    return Reply(
        make_error<lsp::LSPError>(formatv("Did not open file previously {}",
                                          Params.textDocument.uri.file()),
                                  lsp::ErrorCode::InvalidParams));
  }
  auto &Doc = DocIt->second;
  auto Query = lspPositionToLlvmFileLoc(Params.position);
  Value *Val = Doc->ParserContext.getValueReferencedAtLocation(Query);

  if (!Val)
    return Reply(std::nullopt);

  std::optional<FileLocRange> Loc = std::nullopt;

  if (auto *I = dyn_cast<Instruction>(Val))
    Loc = Doc->ParserContext.getInstructionOrArgumentLocation(I);
  else if (auto *A = dyn_cast<Argument>(Val))
    Loc = Doc->ParserContext.getInstructionOrArgumentLocation(A);
  else if (auto *BB = dyn_cast<BasicBlock>(Val))
    Loc = Doc->ParserContext.getBlockLocation(BB);
  if (Loc)
    return Reply(lsp::Location(Params.textDocument.uri,
                               llvmFileLocRangeToLspRange(Loc.value())));
  return Reply(std::nullopt);
}

void LspServer::handleRequestTextDocumentHover(
    const lsp::TextDocumentPositionParams &Params,
    lsp::Callback<std::optional<lsp::Hover>> Reply) {
  if (!OpenDocuments.contains(Params.textDocument.uri)) {
    lsp::Logger::error(
        "Document in textDocument/documentSymbol request not open: {}",
        Params.textDocument.uri.file());
    return Reply(
        make_error<lsp::LSPError>(formatv("Did not open file previously {}",
                                          Params.textDocument.uri.file()),
                                  lsp::ErrorCode::InvalidParams));
  }
  auto &ParserContext = OpenDocuments[Params.textDocument.uri]->ParserContext;
  auto Loc = lspPositionToLlvmFileLoc(Params.position);
  auto *V = ParserContext.getValueReferencedAtLocation(Loc);
  if (!V || !(isa<Instruction>(V) || isa<Argument>(V)))
    return Reply(std::nullopt);
  lsp::Hover Result(lsp::Range(Params.position));
  auto Type = V->getType();
  std::string TypeStr;
  raw_string_ostream SS(TypeStr);
  Type->print(SS);
  // Create the high-level managers
  LoopAnalysisManager LAM;
  FunctionAnalysisManager FAM;
  CGSCCAnalysisManager CGAM;
  ModuleAnalysisManager MAM;

  // Use PassBuilder to populate them with default analyses
  // This includes ScalarEvolution, AliasAnalysis, DominatorTree, etc.
  PassBuilder PB;

  // Register the cross-manager proxies
  // This allows a Function Analysis to request a Module Analysis and vice versa
  PB.registerModuleAnalyses(MAM);
  PB.registerCGSCCAnalyses(CGAM);
  PB.registerFunctionAnalyses(FAM);
  PB.registerLoopAnalyses(LAM);
  PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);
  Result.contents = {
      lsp::MarkupKind::Markdown,
      formatv("```\n{0} {1}\n```", TypeStr, V->getNameOrAsOperand())};
  return Reply(Result);
}

void LspServer::handleRequestGetCFG(const lsp::GetCfgParams &Params,
                                    lsp::Callback<lsp::CFG> Reply) {
  // TODO: have a flag to force regenerating the artifacts
  auto Filepath = Params.uri;
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
  lsp::Logger::debug("Opened doc");

  Function *F = nullptr;
  BasicBlock *BB = nullptr;
  if (BasicBlock *MaybeBB = Doc.getBlockAtLocation(Line, Character)) {
    // If cursor is in a block, show CFG for the function and highlight the
    // block.
    BB = MaybeBB;
    F = BB->getParent();
  } else {
    F = Doc.getFunctionAtLocation(Line, Character);
    if (!F) {
      sendError("Not a location of a function.");
      return Reply(make_error<lsp::LSPError>("Not a location of a function",
                                             lsp::ErrorCode::InvalidRequest));
    }
    if (F->isDeclaration())
      return Reply(
          make_error<lsp::LSPError>("Cannot display CFG for declaration",
                                    lsp::ErrorCode::InvalidRequest));
    BB = &F->getEntryBlock();
  }

  lsp::Logger::debug("Found objects");

  auto PathOpt = Doc.getPathForSVGFile(F);
  if (!PathOpt)
    lsp::Logger::info("Did not find Path for SVG file for {}", Filepath);

  lsp::CFG Result;
  Result.uri = *PathOpt;
  Result.node_id = Doc.getNodeId(Doc.getBlockAtLocation(Line, Character));
  Result.function = F->getName();

  Reply(Result);

  SVGToIRMap[*PathOpt] = Filepath;
}

void LspServer::handleRequestBBLocation(const lsp::BbLocationParams &Params,
                                        lsp::Callback<lsp::BbLocation> Reply) {
  auto Filepath = Params.uri;
  auto NodeIDStr = Params.node_id;

  // We assume the query to SVGToIRMap would not fail.
  auto IR = SVGToIRMap[Filepath];
  IRDocument &Doc = *OpenDocuments[IR];
  lsp::BbLocation Result;
  Result.range = llvmFileLocRangeToLspRange(Doc.parseNodeId(NodeIDStr));
  Result.uri = IR;
  return Reply(Result);
}

void LspServer::handleRequestGetPassList(const lsp::GetPassListParams &Params,
                                         lsp::Callback<lsp::PassList> Reply) {

  auto Filepath = Params.uri;
  std::string Pipeline = Params.pipeline;

  if (OpenDocuments.find(Filepath) == OpenDocuments.end()) {
    lsp::Logger::error("Did not open file previously {}", Filepath);
    return Reply(make_error<lsp::LSPError>(
        formatv("Did not open file previously {}", Filepath),
        lsp::ErrorCode::InvalidParams));
  }
  IRDocument &Doc = *OpenDocuments[Filepath];

  lsp::Logger::info("Opened IR file to get pass list {}", Filepath);

  auto PassListResult = Doc.getPassList(Pipeline, Params.additional_opt_args);

  if (!PassListResult) {
    return Reply(PassListResult.takeError());
  }

  auto PassList = PassListResult.get();

  auto PassDescriptionsResult =
      Doc.getPassDescriptions(Pipeline, Params.additional_opt_args);

  if (!PassDescriptionsResult) {
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

  Reply(ResponseParams);
}

void LspServer::handleRequestGetIRBeforePass(
    const lsp::GetIRBeforePassParams &Params, lsp::Callback<lsp::IR> Reply) {
  auto Filepath = Params.uri;
  std::string Pipeline = Params.pipeline;

  if (OpenDocuments.find(Filepath) == OpenDocuments.end()) {
    lsp::Logger::error("Did not open file previously {}", Filepath);
    return Reply(make_error<lsp::LSPError>(
        formatv("Did not open file previously {}", Filepath),
        lsp::ErrorCode::InvalidParams));
  }
  IRDocument &Doc = *OpenDocuments[Filepath];

  unsigned PassNum = Params.passnumber;
  auto IRFilePathResult = Doc.getIRBeforePassNumber(
      Pipeline, PassNum, std::move(Params.pipeline_opt_args),
      std::move(Params.pass_opt_args));

  if (!IRFilePathResult) {
    return Reply(IRFilePathResult.takeError());
  }

  auto IRFilePath = IRFilePathResult.get();

  if (auto MaybeIRUri = lsp::URIForFile::fromFile(IRFilePath.string())) {
    lsp::IR Return;
    Return.uri = *MaybeIRUri;
    Reply(Return);
  } else {
    return Reply(MaybeIRUri.takeError());
  }

  return;
}

void LspServer::handleRequestRunPassOnIR(const lsp::RunPassOnIRParams &Params,
                                         lsp::Callback<lsp::IR> Reply) {
  auto Filepath = Params.uri;

  if (OpenDocuments.find(Filepath) == OpenDocuments.end()) {
    lsp::Logger::error("Did not open file previously {}", Filepath);
    return Reply(make_error<lsp::LSPError>(
        formatv("Did not open file previously {}", Filepath),
        lsp::ErrorCode::InvalidParams));
  }
  IRDocument &Doc = *OpenDocuments[Filepath];

  // Run a single pass on the current IR and return the path to the result.
  auto IRFilePathResult =
      Doc.runPass(Params.pass, std::move(Params.pipeline_opt_args),
                  std::move(Params.pass_opt_args), Params.stderr_path);

  if (!IRFilePathResult) {
    return Reply(IRFilePathResult.takeError());
  }

  auto IRFilePath = IRFilePathResult.get();

  if (auto MaybeIRUri = lsp::URIForFile::fromFile(IRFilePath.string())) {
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
  MessageHandler.notification(
      "textDocument/didChange", this,
      &LspServer::handleNotificationTextDocumentDidChange);
  MessageHandler.notification(
      "textDocument/didSave", this,
      &LspServer::handleNotificationTextDocumentDidSave);
  MessageHandler.method("textDocument/references", this,
                        &LspServer::handleRequestGetReferences);
  MessageHandler.method("textDocument/documentSymbol", this,
                        &LspServer::handleRequestTextDocumentDocumentSymbol);
  MessageHandler.method("textDocument/codeAction", this,
                        &LspServer::handleRequestCodeAction);
  MessageHandler.method("textDocument/definition", this,
                        &LspServer::handleRequestTextDocumentDefinition);
  MessageHandler.method("textDocument/hover", this,
                        &LspServer::handleRequestTextDocumentHover);
  // Custom messages
  MessageHandler.method("llvm/getCfg", this, &LspServer::handleRequestGetCFG);
  MessageHandler.method("llvm/bbLocation", this,
                        &LspServer::handleRequestBBLocation);
  MessageHandler.method("llvm/getPassList", this,
                        &LspServer::handleRequestGetPassList);
  MessageHandler.method("llvm/getIRBeforePass", this,
                        &LspServer::handleRequestGetIRBeforePass);
  MessageHandler.method("llvm/runPassOnIR", this,
                        &LspServer::handleRequestRunPassOnIR);

  ShowMessageSender =
      MessageHandler.outgoingNotification<lsp::ShowMessageParams>(
          "window/showMessage");
  PublishDiagnosticsSender =
      MessageHandler.outgoingNotification<lsp::PublishDiagnosticsParams>(
          "textDocument/publishDiagnostics");

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
