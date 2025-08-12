//===--- Protocol.h - Language Server Protocol Implementation ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains structs based on the LSP specification at
// https://microsoft.github.io/language-server-protocol/specification
//
// This is not meant to be a complete implementation, new interfaces are added
// when they're needed.
//
// Each struct has a toJSON and fromJSON function, that converts between
// the struct and a JSON representation. (See JSON.h)
//
// Some structs also have operator<< serialization. This is for debugging and
// tests, and is not generally machine-readable.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_LSP_LSP_SERVER_SUPPORT_PROTOCOL_H
#define LLVM_TOOLS_LLVM_LSP_LSP_SERVER_SUPPORT_PROTOCOL_H

#include "llvm/Support/JSON.h"
#include "llvm/Support/LogicalResult.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include <bitset>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace llvm {
namespace lsp {

enum class ErrorCode {
  // Defined by JSON RPC.
  ParseError = -32700,
  InvalidRequest = -32600,
  MethodNotFound = -32601,
  InvalidParams = -32602,
  InternalError = -32603,

  ServerNotInitialized = -32002,
  UnknownErrorCode = -32001,

  // Defined by the protocol.
  RequestCancelled = -32800,
  ContentModified = -32801,
  RequestFailed = -32803,
};

/// Defines how the host (editor) should sync document changes to the language
/// server.
enum class TextDocumentSyncKind {
  /// Documents should not be synced at all.
  None = 0,

  /// Documents are synced by always sending the full content of the document.
  Full = 1,

  /// Documents are synced by sending the full content on open. After that only
  /// incremental updates to the document are sent.
  Incremental = 2,
};

//===----------------------------------------------------------------------===//
// LSPError
//===----------------------------------------------------------------------===//

/// This class models an LSP error as an llvm::Error.
class LSPError : public llvm::ErrorInfo<LSPError> {
public:
  std::string Message;
  ErrorCode Code;
  static char ID;

  LSPError(std::string Message, ErrorCode Code)
      : Message(std::move(Message)), Code(Code) {}

  void log(raw_ostream &Os) const override {
    Os << int(Code) << ": " << Message;
  }
  std::error_code convertToErrorCode() const override {
    return llvm::inconvertibleErrorCode();
  }
};

//===----------------------------------------------------------------------===//
// URIForFile
//===----------------------------------------------------------------------===//

/// URI in "file" scheme for a file.
class URIForFile {
public:
  URIForFile() = default;

  /// Try to build a URIForFile from the given URI string.
  static llvm::Expected<URIForFile> fromURI(StringRef Uri);

  /// Try to build a URIForFile from the given absolute file path and optional
  /// scheme.
  static llvm::Expected<URIForFile> fromFile(StringRef AbsoluteFilepath,
                                             StringRef Scheme = "file");

  /// Returns the absolute path to the file.
  StringRef file() const { return FilePath; }

  /// Returns the original uri of the file.
  StringRef uri() const { return UriStr; }

  /// Return the scheme of the uri.
  StringRef scheme() const;

  explicit operator bool() const { return !FilePath.empty(); }

  friend bool operator==(const URIForFile &Lhs, const URIForFile &Rhs) {
    return Lhs.FilePath == Rhs.FilePath;
  }
  friend bool operator!=(const URIForFile &Lhs, const URIForFile &Rhs) {
    return !(Lhs == Rhs);
  }
  friend bool operator<(const URIForFile &Lhs, const URIForFile &Rhs) {
    return Lhs.FilePath < Rhs.FilePath;
  }

  /// Register a supported URI scheme. The protocol supports `file` by default,
  /// so this is only necessary for any additional schemes that a server wants
  /// to support.
  static void registerSupportedScheme(StringRef Scheme);

private:
  explicit URIForFile(std::string &&FilePath, std::string &&UriStr)
      : FilePath(std::move(FilePath)), UriStr(UriStr) {}

  std::string FilePath;
  std::string UriStr;
};

/// Add support for JSON serialization.
llvm::json::Value toJSON(const URIForFile &Value);
bool fromJSON(const llvm::json::Value &Value, URIForFile &Result,
              llvm::json::Path Path);
raw_ostream &operator<<(raw_ostream &Os, const URIForFile &Value);

//===----------------------------------------------------------------------===//
// ClientCapabilities
//===----------------------------------------------------------------------===//

struct ClientCapabilities {
  /// Client supports hierarchical document symbols.
  /// textDocument.documentSymbol.hierarchicalDocumentSymbolSupport
  bool HierarchicalDocumentSymbol = false;

  /// Client supports CodeAction return value for textDocument/codeAction.
  /// textDocument.codeAction.codeActionLiteralSupport.
  bool CodeActionStructure = false;
};

/// Add support for JSON serialization.
bool fromJSON(const llvm::json::Value &Value, ClientCapabilities &Result,
              llvm::json::Path Path);

//===----------------------------------------------------------------------===//
// ClientInfo
//===----------------------------------------------------------------------===//

struct ClientInfo {
  /// The name of the client as defined by the client.
  std::string Name;

  /// The client's version as defined by the client.
  std::optional<std::string> Version;
};

/// Add support for JSON serialization.
bool fromJSON(const llvm::json::Value &Value, ClientInfo &Result,
              llvm::json::Path Path);

//===----------------------------------------------------------------------===//
// InitializeParams
//===----------------------------------------------------------------------===//

enum class TraceLevel {
  Off = 0,
  Messages = 1,
  Verbose = 2,
};

/// Add support for JSON serialization.
bool fromJSON(const llvm::json::Value &Value, TraceLevel &Result,
              llvm::json::Path Path);

struct InitializeParams {
  /// The capabilities provided by the client (editor or tool).
  ClientCapabilities Capabilities;

  /// Information about the client.
  std::optional<ClientInfo> ClientInfo;

  /// The initial trace setting. If omitted trace is disabled ('off').
  std::optional<TraceLevel> Trace;
};

/// Add support for JSON serialization.
bool fromJSON(const llvm::json::Value &Value, InitializeParams &Result,
              llvm::json::Path Path);

//===----------------------------------------------------------------------===//
// InitializedParams
//===----------------------------------------------------------------------===//

struct NoParams {};
inline bool fromJSON(const llvm::json::Value &, NoParams &, llvm::json::Path) {
  return true;
}
using InitializedParams = NoParams;

//===----------------------------------------------------------------------===//
// TextDocumentItem
//===----------------------------------------------------------------------===//

struct TextDocumentItem {
  /// The text document's URI.
  URIForFile Uri;

  /// The text document's language identifier.
  std::string LanguageId;

  /// The content of the opened text document.
  std::string Text;

  /// The version number of this document.
  int64_t Version;
};

/// Add support for JSON serialization.
bool fromJSON(const llvm::json::Value &Value, TextDocumentItem &Result,
              llvm::json::Path Path);

//===----------------------------------------------------------------------===//
// TextDocumentIdentifier
//===----------------------------------------------------------------------===//

struct TextDocumentIdentifier {
  /// The text document's URI.
  URIForFile Uri;
};

/// Add support for JSON serialization.
llvm::json::Value toJSON(const TextDocumentIdentifier &Value);
bool fromJSON(const llvm::json::Value &Value, TextDocumentIdentifier &Result,
              llvm::json::Path Path);

//===----------------------------------------------------------------------===//
// VersionedTextDocumentIdentifier
//===----------------------------------------------------------------------===//

struct VersionedTextDocumentIdentifier {
  /// The text document's URI.
  URIForFile Uri;
  /// The version number of this document.
  int64_t Version;
};

/// Add support for JSON serialization.
llvm::json::Value toJSON(const VersionedTextDocumentIdentifier &Value);
bool fromJSON(const llvm::json::Value &Value,
              VersionedTextDocumentIdentifier &Result, llvm::json::Path Path);

//===----------------------------------------------------------------------===//
// Position
//===----------------------------------------------------------------------===//

struct Position {
  Position(int Line = 0, int Character = 0)
      : Line(Line), Character(Character) {}

  /// Construct a position from the given source location.
  Position(llvm::SourceMgr &Mgr, SMLoc Loc) {
    std::pair<unsigned, unsigned> LineAndCol = Mgr.getLineAndColumn(Loc);
    Line = LineAndCol.first - 1;
    Character = LineAndCol.second - 1;
  }

  /// Line position in a document (zero-based).
  int Line = 0;

  /// Character offset on a line in a document (zero-based).
  int Character = 0;

  friend bool operator==(const Position &Lhs, const Position &Rhs) {
    return std::tie(Lhs.Line, Lhs.Character) ==
           std::tie(Rhs.Line, Rhs.Character);
  }
  friend bool operator!=(const Position &Lhs, const Position &Rhs) {
    return !(Lhs == Rhs);
  }
  friend bool operator<(const Position &Lhs, const Position &Rhs) {
    return std::tie(Lhs.Line, Lhs.Character) <
           std::tie(Rhs.Line, Rhs.Character);
  }
  friend bool operator<=(const Position &Lhs, const Position &Rhs) {
    return std::tie(Lhs.Line, Lhs.Character) <=
           std::tie(Rhs.Line, Rhs.Character);
  }

  /// Convert this position into a source location in the main file of the given
  /// source manager.
  SMLoc getAsSMLoc(llvm::SourceMgr &Mgr) const {
    return Mgr.FindLocForLineAndColumn(Mgr.getMainFileID(), Line + 1,
                                       Character + 1);
  }
};

/// Add support for JSON serialization.
bool fromJSON(const llvm::json::Value &Value, Position &Result,
              llvm::json::Path Path);
llvm::json::Value toJSON(const Position &Value);
raw_ostream &operator<<(raw_ostream &Os, const Position &Value);

//===----------------------------------------------------------------------===//
// Range
//===----------------------------------------------------------------------===//

struct Range {
  Range() = default;
  Range(Position Start, Position End) : Start(Start), End(End) {}
  Range(Position Loc) : Range(Loc, Loc) {}

  /// Construct a range from the given source range.
  Range(llvm::SourceMgr &Mgr, SMRange Range)
      : lsp::Range(Position(Mgr, Range.Start), Position(Mgr, Range.End)) {}

  /// The range's start position.
  Position Start;

  /// The range's end position.
  Position End;

  friend bool operator==(const Range &Lhs, const Range &Rhs) {
    return std::tie(Lhs.Start, Lhs.End) == std::tie(Rhs.Start, Rhs.End);
  }
  friend bool operator!=(const Range &Lhs, const Range &Rhs) {
    return !(Lhs == Rhs);
  }
  friend bool operator<(const Range &Lhs, const Range &Rhs) {
    return std::tie(Lhs.Start, Lhs.End) < std::tie(Rhs.Start, Rhs.End);
  }

  bool contains(Position Pos) const { return Start <= Pos && Pos < End; }
  bool contains(Range Range) const {
    return Start <= Range.Start && Range.End <= End;
  }

  /// Convert this range into a source range in the main file of the given
  /// source manager.
  SMRange getAsSMRange(llvm::SourceMgr &Mgr) const {
    SMLoc StartLoc = Start.getAsSMLoc(Mgr);
    SMLoc EndLoc = End.getAsSMLoc(Mgr);
    // Check that the start and end locations are valid.
    if (!StartLoc.isValid() || !EndLoc.isValid() ||
        StartLoc.getPointer() > EndLoc.getPointer())
      return SMRange();
    return SMRange(StartLoc, EndLoc);
  }
};

/// Add support for JSON serialization.
bool fromJSON(const llvm::json::Value &Value, Range &Result,
              llvm::json::Path Path);
llvm::json::Value toJSON(const Range &Value);
raw_ostream &operator<<(raw_ostream &Os, const Range &Value);

//===----------------------------------------------------------------------===//
// Location
//===----------------------------------------------------------------------===//

struct Location {
  Location() = default;
  Location(const URIForFile &Uri, Range Range) : Uri(Uri), Range(Range) {}

  /// Construct a Location from the given source range.
  Location(const URIForFile &Uri, llvm::SourceMgr &Mgr, SMRange Range)
      : Location(Uri, lsp::Range(Mgr, Range)) {}

  /// The text document's URI.
  URIForFile Uri;
  Range Range;

  friend bool operator==(const Location &Lhs, const Location &Rhs) {
    return Lhs.Uri == Rhs.Uri && Lhs.Range == Rhs.Range;
  }

  friend bool operator!=(const Location &Lhs, const Location &Rhs) {
    return !(Lhs == Rhs);
  }

  friend bool operator<(const Location &Lhs, const Location &Rhs) {
    return std::tie(Lhs.Uri, Lhs.Range) < std::tie(Rhs.Uri, Rhs.Range);
  }
};

/// Add support for JSON serialization.
bool fromJSON(const llvm::json::Value &Value, Location &Result,
              llvm::json::Path Path);
llvm::json::Value toJSON(const Location &Value);
raw_ostream &operator<<(raw_ostream &Os, const Location &Value);

//===----------------------------------------------------------------------===//
// TextDocumentPositionParams
//===----------------------------------------------------------------------===//

struct TextDocumentPositionParams {
  /// The text document.
  TextDocumentIdentifier TextDocument;

  /// The position inside the text document.
  Position Position;
};

/// Add support for JSON serialization.
bool fromJSON(const llvm::json::Value &Value,
              TextDocumentPositionParams &Result, llvm::json::Path Path);

//===----------------------------------------------------------------------===//
// ReferenceParams
//===----------------------------------------------------------------------===//

struct ReferenceContext {
  /// Include the declaration of the current symbol.
  bool IncludeDeclaration = false;
};

/// Add support for JSON serialization.
bool fromJSON(const llvm::json::Value &Value, ReferenceContext &Result,
              llvm::json::Path Path);

struct ReferenceParams : public TextDocumentPositionParams {
  ReferenceContext Context;
};

/// Add support for JSON serialization.
bool fromJSON(const llvm::json::Value &Value, ReferenceParams &Result,
              llvm::json::Path Path);

//===----------------------------------------------------------------------===//
// DidOpenTextDocumentParams
//===----------------------------------------------------------------------===//

struct DidOpenTextDocumentParams {
  /// The document that was opened.
  TextDocumentItem TextDocument;
};

/// Add support for JSON serialization.
bool fromJSON(const llvm::json::Value &Value, DidOpenTextDocumentParams &Result,
              llvm::json::Path Path);

//===----------------------------------------------------------------------===//
// DidCloseTextDocumentParams
//===----------------------------------------------------------------------===//

struct DidCloseTextDocumentParams {
  /// The document that was closed.
  TextDocumentIdentifier TextDocument;
};

/// Add support for JSON serialization.
bool fromJSON(const llvm::json::Value &Value,
              DidCloseTextDocumentParams &Result, llvm::json::Path Path);

//===----------------------------------------------------------------------===//
// DidChangeTextDocumentParams
//===----------------------------------------------------------------------===//

struct TextDocumentContentChangeEvent {
  /// Try to apply this change to the given contents string.
  LogicalResult applyTo(std::string &Contents) const;
  /// Try to apply a set of changes to the given contents string.
  static LogicalResult applyTo(ArrayRef<TextDocumentContentChangeEvent> Changes,
                               std::string &Contents);

  /// The range of the document that changed.
  std::optional<Range> Range;

  /// The length of the range that got replaced.
  std::optional<int> RangeLength;

  /// The new text of the range/document.
  std::string Text;
};

/// Add support for JSON serialization.
bool fromJSON(const llvm::json::Value &Value,
              TextDocumentContentChangeEvent &Result, llvm::json::Path Path);

struct DidChangeTextDocumentParams {
  /// The document that changed.
  VersionedTextDocumentIdentifier TextDocument;

  /// The actual content changes.
  std::vector<TextDocumentContentChangeEvent> ContentChanges;
};

/// Add support for JSON serialization.
bool fromJSON(const llvm::json::Value &Value,
              DidChangeTextDocumentParams &Result, llvm::json::Path Path);

//===----------------------------------------------------------------------===//
// MarkupContent
//===----------------------------------------------------------------------===//

/// Describes the content type that a client supports in various result literals
/// like `Hover`.
enum class MarkupKind {
  PlainText,
  Markdown,
};
raw_ostream &operator<<(raw_ostream &Os, MarkupKind Kind);

struct MarkupContent {
  MarkupKind Kind = MarkupKind::PlainText;
  std::string Value;
};

/// Add support for JSON serialization.
llvm::json::Value toJSON(const MarkupContent &Mc);

//===----------------------------------------------------------------------===//
// Hover
//===----------------------------------------------------------------------===//

struct Hover {
  /// Construct a default hover with the given range that uses Markdown content.
  Hover(Range Range) : Contents{MarkupKind::Markdown, ""}, Range(Range) {}

  /// The hover's content.
  MarkupContent Contents;

  /// An optional range is a range inside a text document that is used to
  /// visualize a hover, e.g. by changing the background color.
  std::optional<Range> Range;
};

/// Add support for JSON serialization.
llvm::json::Value toJSON(const Hover &Hover);

//===----------------------------------------------------------------------===//
// SymbolKind
//===----------------------------------------------------------------------===//

enum class SymbolKind {
  File = 1,
  Module = 2,
  Namespace = 3,
  Package = 4,
  Class = 5,
  Method = 6,
  Property = 7,
  Field = 8,
  Constructor = 9,
  Enum = 10,
  Interface = 11,
  Function = 12,
  Variable = 13,
  Constant = 14,
  String = 15,
  Number = 16,
  Boolean = 17,
  Array = 18,
  Object = 19,
  Key = 20,
  Null = 21,
  EnumMember = 22,
  Struct = 23,
  Event = 24,
  Operator = 25,
  TypeParameter = 26
};

//===----------------------------------------------------------------------===//
// DocumentSymbol
//===----------------------------------------------------------------------===//

/// Represents programming constructs like variables, classes, interfaces etc.
/// that appear in a document. Document symbols can be hierarchical and they
/// have two ranges: one that encloses its definition and one that points to its
/// most interesting range, e.g. the range of an identifier.
struct DocumentSymbol {
  DocumentSymbol() = default;
  DocumentSymbol(DocumentSymbol &&) = default;
  DocumentSymbol(const Twine &Name, SymbolKind Kind, Range Range,
                 lsp::Range SelectionRange)
      : Name(Name.str()), Kind(Kind), Range(Range),
        SelectionRange(SelectionRange) {}

  /// The name of this symbol.
  std::string Name;

  /// More detail for this symbol, e.g the signature of a function.
  std::string Detail;

  /// The kind of this symbol.
  SymbolKind Kind;

  /// The range enclosing this symbol not including leading/trailing whitespace
  /// but everything else like comments. This information is typically used to
  /// determine if the clients cursor is inside the symbol to reveal in the
  /// symbol in the UI.
  Range Range;

  /// The range that should be selected and revealed when this symbol is being
  /// picked, e.g the name of a function. Must be contained by the `range`.
  lsp::Range SelectionRange;

  /// Children of this symbol, e.g. properties of a class.
  std::vector<DocumentSymbol> Children;
};

/// Add support for JSON serialization.
llvm::json::Value toJSON(const DocumentSymbol &Symbol);

//===----------------------------------------------------------------------===//
// DocumentSymbolParams
//===----------------------------------------------------------------------===//

struct DocumentSymbolParams {
  // The text document to find symbols in.
  TextDocumentIdentifier TextDocument;
};

/// Add support for JSON serialization.
bool fromJSON(const llvm::json::Value &Value, DocumentSymbolParams &Result,
              llvm::json::Path Path);

//===----------------------------------------------------------------------===//
// DiagnosticRelatedInformation
//===----------------------------------------------------------------------===//

/// Represents a related message and source code location for a diagnostic.
/// This should be used to point to code locations that cause or related to a
/// diagnostics, e.g. when duplicating a symbol in a scope.
struct DiagnosticRelatedInformation {
  DiagnosticRelatedInformation() = default;
  DiagnosticRelatedInformation(Location Location, std::string Message)
      : Location(std::move(Location)), Message(std::move(Message)) {}

  /// The location of this related diagnostic information.
  Location Location;
  /// The message of this related diagnostic information.
  std::string Message;
};

/// Add support for JSON serialization.
bool fromJSON(const llvm::json::Value &Value,
              DiagnosticRelatedInformation &Result, llvm::json::Path Path);
llvm::json::Value toJSON(const DiagnosticRelatedInformation &Info);

//===----------------------------------------------------------------------===//
// Diagnostic
//===----------------------------------------------------------------------===//

enum class DiagnosticSeverity {
  /// It is up to the client to interpret diagnostics as error, warning, info or
  /// hint.
  Undetermined = 0,
  Error = 1,
  Warning = 2,
  Information = 3,
  Hint = 4
};

enum class DiagnosticTag {
  Unnecessary = 1,
  Deprecated = 2,
};

/// Add support for JSON serialization.
llvm::json::Value toJSON(DiagnosticTag Tag);
bool fromJSON(const llvm::json::Value &Value, DiagnosticTag &Result,
              llvm::json::Path Path);

struct Diagnostic {
  /// The source range where the message applies.
  Range Range;

  /// The diagnostic's severity. Can be omitted. If omitted it is up to the
  /// client to interpret diagnostics as error, warning, info or hint.
  DiagnosticSeverity Severity = DiagnosticSeverity::Undetermined;

  /// A human-readable string describing the source of this diagnostic, e.g.
  /// 'typescript' or 'super lint'.
  std::string Source;

  /// The diagnostic's message.
  std::string Message;

  /// An array of related diagnostic information, e.g. when symbol-names within
  /// a scope collide all definitions can be marked via this property.
  std::optional<std::vector<DiagnosticRelatedInformation>> RelatedInformation;

  /// Additional metadata about the diagnostic.
  std::vector<DiagnosticTag> Tags;

  /// The diagnostic's category. Can be omitted.
  /// An LSP extension that's used to send the name of the category over to the
  /// client. The category typically describes the compilation stage during
  /// which the issue was produced, e.g. "Semantic Issue" or "Parse Issue".
  std::optional<std::string> Category;
};

/// Add support for JSON serialization.
llvm::json::Value toJSON(const Diagnostic &Diag);
bool fromJSON(const llvm::json::Value &Value, Diagnostic &Result,
              llvm::json::Path Path);

//===----------------------------------------------------------------------===//
// PublishDiagnosticsParams
//===----------------------------------------------------------------------===//

struct PublishDiagnosticsParams {
  PublishDiagnosticsParams(URIForFile Uri, int64_t Version)
      : Uri(std::move(Uri)), Version(Version) {}

  /// The URI for which diagnostic information is reported.
  URIForFile Uri;
  /// The list of reported diagnostics.
  std::vector<Diagnostic> Diagnostics;
  /// The version number of the document the diagnostics are published for.
  int64_t Version;
};

/// Add support for JSON serialization.
llvm::json::Value toJSON(const PublishDiagnosticsParams &Params);

//===----------------------------------------------------------------------===//
//  ShowMessageParams
//===----------------------------------------------------------------------===//

enum class MessageType { Error = 1, Warning = 2, Info = 3, Log = 4, Debug = 5 };

struct ShowMessageParams {
  ShowMessageParams(MessageType Type, std::string Message)
      : Type(Type), Message(Message) {}
  MessageType Type;
  /**
   * The actual message.
   */
  std::string Message;
};

/// Add support for JSON serialization.
llvm::json::Value toJSON(const ShowMessageParams &Params);

//===----------------------------------------------------------------------===//
// TextEdit
//===----------------------------------------------------------------------===//

struct TextEdit {
  /// The range of the text document to be manipulated. To insert
  /// text into a document create a range where start === end.
  Range Range;

  /// The string to be inserted. For delete operations use an
  /// empty string.
  std::string NewText;
};

inline bool operator==(const TextEdit &Lhs, const TextEdit &Rhs) {
  return std::tie(Lhs.NewText, Lhs.Range) == std::tie(Rhs.NewText, Rhs.Range);
}

bool fromJSON(const llvm::json::Value &Value, TextEdit &Result,
              llvm::json::Path Path);
llvm::json::Value toJSON(const TextEdit &Value);
raw_ostream &operator<<(raw_ostream &Os, const TextEdit &Value);

//===----------------------------------------------------------------------===//
// CompletionItemKind
//===----------------------------------------------------------------------===//

/// The kind of a completion entry.
enum class CompletionItemKind {
  Missing = 0,
  Text = 1,
  Method = 2,
  Function = 3,
  Constructor = 4,
  Field = 5,
  Variable = 6,
  Class = 7,
  Interface = 8,
  Module = 9,
  Property = 10,
  Unit = 11,
  Value = 12,
  Enum = 13,
  Keyword = 14,
  Snippet = 15,
  Color = 16,
  File = 17,
  Reference = 18,
  Folder = 19,
  EnumMember = 20,
  Constant = 21,
  Struct = 22,
  Event = 23,
  Operator = 24,
  TypeParameter = 25,
};
bool fromJSON(const llvm::json::Value &Value, CompletionItemKind &Result,
              llvm::json::Path Path);

constexpr auto KCompletionItemKindMin =
    static_cast<size_t>(CompletionItemKind::Text);
constexpr auto KCompletionItemKindMax =
    static_cast<size_t>(CompletionItemKind::TypeParameter);
using CompletionItemKindBitset = std::bitset<KCompletionItemKindMax + 1>;
bool fromJSON(const llvm::json::Value &Value, CompletionItemKindBitset &Result,
              llvm::json::Path Path);

CompletionItemKind
adjustKindToCapability(CompletionItemKind Kind,
                       CompletionItemKindBitset &SupportedCompletionItemKinds);

//===----------------------------------------------------------------------===//
// CompletionItem
//===----------------------------------------------------------------------===//

/// Defines whether the insert text in a completion item should be interpreted
/// as plain text or a snippet.
enum class InsertTextFormat {
  Missing = 0,
  /// The primary text to be inserted is treated as a plain string.
  PlainText = 1,
  /// The primary text to be inserted is treated as a snippet.
  ///
  /// A snippet can define tab stops and placeholders with `$1`, `$2`
  /// and `${3:foo}`. `$0` defines the final tab stop, it defaults to the end
  /// of the snippet. Placeholders with equal identifiers are linked, that is
  /// typing in one will update others too.
  ///
  /// See also:
  /// https//github.com/Microsoft/vscode/blob/master/src/vs/editor/contrib/snippet/common/snippet.md
  Snippet = 2,
};

struct CompletionItem {
  CompletionItem() = default;
  CompletionItem(const Twine &Label, CompletionItemKind Kind,
                 StringRef SortText = "")
      : Label(Label.str()), Kind(Kind), SortText(SortText.str()),
        InsertTextFormat(InsertTextFormat::PlainText) {}

  /// The label of this completion item. By default also the text that is
  /// inserted when selecting this completion.
  std::string Label;

  /// The kind of this completion item. Based of the kind an icon is chosen by
  /// the editor.
  CompletionItemKind Kind = CompletionItemKind::Missing;

  /// A human-readable string with additional information about this item, like
  /// type or symbol information.
  std::string Detail;

  /// A human-readable string that represents a doc-comment.
  std::optional<MarkupContent> Documentation;

  /// A string that should be used when comparing this item with other items.
  /// When `falsy` the label is used.
  std::string SortText;

  /// A string that should be used when filtering a set of completion items.
  /// When `falsy` the label is used.
  std::string FilterText;

  /// A string that should be inserted to a document when selecting this
  /// completion. When `falsy` the label is used.
  std::string InsertText;

  /// The format of the insert text. The format applies to both the `insertText`
  /// property and the `newText` property of a provided `textEdit`.
  InsertTextFormat InsertTextFormat = InsertTextFormat::Missing;

  /// An edit which is applied to a document when selecting this completion.
  /// When an edit is provided `insertText` is ignored.
  ///
  /// Note: The range of the edit must be a single line range and it must
  /// contain the position at which completion has been requested.
  std::optional<TextEdit> TextEdit;

  /// An optional array of additional text edits that are applied when selecting
  /// this completion. Edits must not overlap with the main edit nor with
  /// themselves.
  std::vector<lsp::TextEdit> AdditionalTextEdits;

  /// Indicates if this item is deprecated.
  bool Deprecated = false;
};

/// Add support for JSON serialization.
llvm::json::Value toJSON(const CompletionItem &Value);
raw_ostream &operator<<(raw_ostream &Os, const CompletionItem &Value);
bool operator<(const CompletionItem &Lhs, const CompletionItem &Rhs);

//===----------------------------------------------------------------------===//
// CompletionList
//===----------------------------------------------------------------------===//

/// Represents a collection of completion items to be presented in the editor.
struct CompletionList {
  /// The list is not complete. Further typing should result in recomputing the
  /// list.
  bool IsIncomplete = false;

  /// The completion items.
  std::vector<CompletionItem> Items;
};

/// Add support for JSON serialization.
llvm::json::Value toJSON(const CompletionList &Value);

//===----------------------------------------------------------------------===//
// CompletionContext
//===----------------------------------------------------------------------===//

enum class CompletionTriggerKind {
  /// Completion was triggered by typing an identifier (24x7 code
  /// complete), manual invocation (e.g Ctrl+Space) or via API.
  Invoked = 1,

  /// Completion was triggered by a trigger character specified by
  /// the `triggerCharacters` properties of the `CompletionRegistrationOptions`.
  TriggerCharacter = 2,

  /// Completion was re-triggered as the current completion list is incomplete.
  TriggerTriggerForIncompleteCompletions = 3
};

struct CompletionContext {
  /// How the completion was triggered.
  CompletionTriggerKind TriggerKind = CompletionTriggerKind::Invoked;

  /// The trigger character (a single character) that has trigger code complete.
  /// Is undefined if `triggerKind !== CompletionTriggerKind.TriggerCharacter`
  std::string TriggerCharacter;
};

/// Add support for JSON serialization.
bool fromJSON(const llvm::json::Value &Value, CompletionContext &Result,
              llvm::json::Path Path);

//===----------------------------------------------------------------------===//
// CompletionParams
//===----------------------------------------------------------------------===//

struct CompletionParams : TextDocumentPositionParams {
  CompletionContext Context;
};

/// Add support for JSON serialization.
bool fromJSON(const llvm::json::Value &Value, CompletionParams &Result,
              llvm::json::Path Path);

//===----------------------------------------------------------------------===//
// ParameterInformation
//===----------------------------------------------------------------------===//

/// A single parameter of a particular signature.
struct ParameterInformation {
  /// The label of this parameter. Ignored when labelOffsets is set.
  std::string LabelString;

  /// Inclusive start and exclusive end offsets withing the containing signature
  /// label.
  std::optional<std::pair<unsigned, unsigned>> LabelOffsets;

  /// The documentation of this parameter. Optional.
  std::string Documentation;
};

/// Add support for JSON serialization.
llvm::json::Value toJSON(const ParameterInformation &Value);

//===----------------------------------------------------------------------===//
// SignatureInformation
//===----------------------------------------------------------------------===//

/// Represents the signature of something callable.
struct SignatureInformation {
  /// The label of this signature. Mandatory.
  std::string Label;

  /// The documentation of this signature. Optional.
  std::string Documentation;

  /// The parameters of this signature.
  std::vector<ParameterInformation> Parameters;
};

/// Add support for JSON serialization.
llvm::json::Value toJSON(const SignatureInformation &Value);
raw_ostream &operator<<(raw_ostream &Os, const SignatureInformation &Value);

//===----------------------------------------------------------------------===//
// SignatureHelp
//===----------------------------------------------------------------------===//

/// Represents the signature of a callable.
struct SignatureHelp {
  /// The resulting signatures.
  std::vector<SignatureInformation> Signatures;

  /// The active signature.
  int ActiveSignature = 0;

  /// The active parameter of the active signature.
  int ActiveParameter = 0;
};

/// Add support for JSON serialization.
llvm::json::Value toJSON(const SignatureHelp &Value);

//===----------------------------------------------------------------------===//
// DocumentLinkParams
//===----------------------------------------------------------------------===//

/// Parameters for the document link request.
struct DocumentLinkParams {
  /// The document to provide document links for.
  TextDocumentIdentifier TextDocument;
};

/// Add support for JSON serialization.
bool fromJSON(const llvm::json::Value &Value, DocumentLinkParams &Result,
              llvm::json::Path Path);

//===----------------------------------------------------------------------===//
// DocumentLink
//===----------------------------------------------------------------------===//

/// A range in a text document that links to an internal or external resource,
/// like another text document or a web site.
struct DocumentLink {
  DocumentLink() = default;
  DocumentLink(Range Range, URIForFile Target)
      : Range(Range), Target(std::move(Target)) {}

  /// The range this link applies to.
  Range Range;

  /// The uri this link points to. If missing a resolve request is sent later.
  URIForFile Target;

  // TODO: The following optional fields defined by the language server protocol
  // are unsupported:
  //
  // data?: any - A data entry field that is preserved on a document link
  //              between a DocumentLinkRequest and a
  //              DocumentLinkResolveRequest.

  friend bool operator==(const DocumentLink &Lhs, const DocumentLink &Rhs) {
    return Lhs.Range == Rhs.Range && Lhs.Target == Rhs.Target;
  }

  friend bool operator!=(const DocumentLink &Lhs, const DocumentLink &Rhs) {
    return !(Lhs == Rhs);
  }
};

/// Add support for JSON serialization.
llvm::json::Value toJSON(const DocumentLink &Value);

//===----------------------------------------------------------------------===//
// InlayHintsParams
//===----------------------------------------------------------------------===//

/// A parameter literal used in inlay hint requests.
struct InlayHintsParams {
  /// The text document.
  TextDocumentIdentifier TextDocument;

  /// The visible document range for which inlay hints should be computed.
  Range Range;
};

/// Add support for JSON serialization.
bool fromJSON(const llvm::json::Value &Value, InlayHintsParams &Result,
              llvm::json::Path Path);

//===----------------------------------------------------------------------===//
// InlayHintKind
//===----------------------------------------------------------------------===//

/// Inlay hint kinds.
enum class InlayHintKind {
  /// An inlay hint that for a type annotation.
  ///
  /// An example of a type hint is a hint in this position:
  ///    auto var ^ = expr;
  /// which shows the deduced type of the variable.
  Type = 1,

  /// An inlay hint that is for a parameter.
  ///
  /// An example of a parameter hint is a hint in this position:
  ///    func(^arg);
  /// which shows the name of the corresponding parameter.
  Parameter = 2,
};

//===----------------------------------------------------------------------===//
// InlayHint
//===----------------------------------------------------------------------===//

/// Inlay hint information.
struct InlayHint {
  InlayHint(InlayHintKind Kind, Position Pos) : Position(Pos), Kind(Kind) {}

  /// The position of this hint.
  Position Position;

  /// The label of this hint. A human readable string or an array of
  /// InlayHintLabelPart label parts.
  ///
  /// *Note* that neither the string nor the label part can be empty.
  std::string Label;

  /// The kind of this hint. Can be omitted in which case the client should fall
  /// back to a reasonable default.
  InlayHintKind Kind;

  /// Render padding before the hint.
  ///
  /// Note: Padding should use the editor's background color, not the
  /// background color of the hint itself. That means padding can be used
  /// to visually align/separate an inlay hint.
  bool PaddingLeft = false;

  /// Render padding after the hint.
  ///
  /// Note: Padding should use the editor's background color, not the
  /// background color of the hint itself. That means padding can be used
  /// to visually align/separate an inlay hint.
  bool PaddingRight = false;
};

/// Add support for JSON serialization.
llvm::json::Value toJSON(const InlayHint &);
bool operator==(const InlayHint &Lhs, const InlayHint &Rhs);
bool operator<(const InlayHint &Lhs, const InlayHint &Rhs);
llvm::raw_ostream &operator<<(llvm::raw_ostream &Os, InlayHintKind Value);

//===----------------------------------------------------------------------===//
// CodeActionContext
//===----------------------------------------------------------------------===//

struct CodeActionContext {
  /// An array of diagnostics known on the client side overlapping the range
  /// provided to the `textDocument/codeAction` request. They are provided so
  /// that the server knows which errors are currently presented to the user for
  /// the given range. There is no guarantee that these accurately reflect the
  /// error state of the resource. The primary parameter to compute code actions
  /// is the provided range.
  std::vector<Diagnostic> Diagnostics;

  /// Requested kind of actions to return.
  ///
  /// Actions not of this kind are filtered out by the client before being
  /// shown. So servers can omit computing them.
  std::vector<std::string> Only;
};

/// Add support for JSON serialization.
bool fromJSON(const llvm::json::Value &Value, CodeActionContext &Result,
              llvm::json::Path Path);

//===----------------------------------------------------------------------===//
// CodeActionParams
//===----------------------------------------------------------------------===//

struct CodeActionParams {
  /// The document in which the command was invoked.
  TextDocumentIdentifier TextDocument;

  /// The range for which the command was invoked.
  Range Range;

  /// Context carrying additional information.
  CodeActionContext Context;
};

/// Add support for JSON serialization.
bool fromJSON(const llvm::json::Value &Value, CodeActionParams &Result,
              llvm::json::Path Path);

//===----------------------------------------------------------------------===//
// WorkspaceEdit
//===----------------------------------------------------------------------===//

struct WorkspaceEdit {
  /// Holds changes to existing resources.
  std::map<std::string, std::vector<TextEdit>> Changes;

  /// Note: "documentChanges" is not currently used because currently there is
  /// no support for versioned edits.
};

/// Add support for JSON serialization.
bool fromJSON(const llvm::json::Value &Value, WorkspaceEdit &Result,
              llvm::json::Path Path);
llvm::json::Value toJSON(const WorkspaceEdit &Value);

//===----------------------------------------------------------------------===//
// CodeAction
//===----------------------------------------------------------------------===//

/// A code action represents a change that can be performed in code, e.g. to fix
/// a problem or to refactor code.
///
/// A CodeAction must set either `edit` and/or a `command`. If both are
/// supplied, the `edit` is applied first, then the `command` is executed.
struct CodeAction {
  /// A short, human-readable, title for this code action.
  std::string Title;

  /// The kind of the code action.
  /// Used to filter code actions.
  std::optional<std::string> Kind;
  const static llvm::StringLiteral KQuickFix;
  const static llvm::StringLiteral KRefactor;
  const static llvm::StringLiteral KInfo;

  /// The diagnostics that this code action resolves.
  std::optional<std::vector<Diagnostic>> Diagnostics;

  /// Marks this as a preferred action. Preferred actions are used by the
  /// `auto fix` command and can be targeted by keybindings.
  /// A quick fix should be marked preferred if it properly addresses the
  /// underlying error. A refactoring should be marked preferred if it is the
  /// most reasonable choice of actions to take.
  bool IsPreferred = false;

  /// The workspace edit this code action performs.
  std::optional<WorkspaceEdit> Edit;
};

/// Add support for JSON serialization.
llvm::json::Value toJSON(const CodeAction &);

} // namespace lsp
} // namespace llvm

namespace llvm {
template <>
struct format_provider<lsp::Position> {
  static void format(const lsp::Position &Pos, raw_ostream &Os,
                     StringRef Style) {
    assert(Style.empty() && "style modifiers for this type are not supported");
    Os << Pos;
  }
};
} // namespace llvm

#endif
