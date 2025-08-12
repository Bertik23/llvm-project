//===--- Protocol.cpp - Language Server Protocol Implementation -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the serialization code for the LSP structs.
//
//===----------------------------------------------------------------------===//

#include "Protocol.h"
#include "Logging.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace llvm::lsp;

// Helper that doesn't treat `null` and absent fields as failures.
template <typename T>
static bool mapOptOrNull(const llvm::json::Value &Params,
                         llvm::StringLiteral Prop, T &Out,
                         llvm::json::Path Path) {
  const llvm::json::Object *O = Params.getAsObject();
  assert(O);

  // Field is missing or null.
  auto *V = O->get(Prop);
  if (!V || V->getAsNull())
    return true;
  return fromJSON(*V, Out, Path.field(Prop));
}

//===----------------------------------------------------------------------===//
// LSPError
//===----------------------------------------------------------------------===//

char LSPError::ID;

//===----------------------------------------------------------------------===//
// URIForFile
//===----------------------------------------------------------------------===//

static bool isWindowsPath(StringRef Path) {
  return Path.size() > 1 && llvm::isAlpha(Path[0]) && Path[1] == ':';
}

static bool isNetworkPath(StringRef Path) {
  return Path.size() > 2 && Path[0] == Path[1] &&
         llvm::sys::path::is_separator(Path[0]);
}

static bool shouldEscapeInURI(unsigned char C) {
  // Unreserved characters.
  if ((C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') ||
      (C >= '0' && C <= '9'))
    return false;

  switch (C) {
  case '-':
  case '_':
  case '.':
  case '~':
  // '/' is only reserved when parsing.
  case '/':
  // ':' is only reserved for relative URI paths, which we doesn't produce.
  case ':':
    return false;
  }
  return true;
}

/// Encodes a string according to percent-encoding.
/// - Unreserved characters are not escaped.
/// - Reserved characters always escaped with exceptions like '/'.
/// - All other characters are escaped.
static void percentEncode(StringRef Content, std::string &Out) {
  for (unsigned char C : Content) {
    if (shouldEscapeInURI(C)) {
      Out.push_back('%');
      Out.push_back(llvm::hexdigit(C / 16));
      Out.push_back(llvm::hexdigit(C % 16));
    } else {
      Out.push_back(C);
    }
  }
}

/// Decodes a string according to percent-encoding.
static std::string percentDecode(StringRef Content) {
  std::string Result;
  for (auto I = Content.begin(), E = Content.end(); I != E; ++I) {
    if (*I != '%') {
      Result += *I;
      continue;
    }
    if (*I == '%' && I + 2 < Content.end() && llvm::isHexDigit(*(I + 1)) &&
        llvm::isHexDigit(*(I + 2))) {
      Result.push_back(llvm::hexFromNibbles(*(I + 1), *(I + 2)));
      I += 2;
    } else {
      Result.push_back(*I);
    }
  }
  return Result;
}

/// Return the set containing the supported URI schemes.
static StringSet<> &getSupportedSchemes() {
  static StringSet<> Schemes({"file", "test"});
  return Schemes;
}

/// Returns true if the given scheme is structurally valid, i.e. it does not
/// contain any invalid scheme characters. This does not check that the scheme
/// is actually supported.
static bool isStructurallyValidScheme(StringRef Scheme) {
  if (Scheme.empty())
    return false;
  if (!llvm::isAlpha(Scheme[0]))
    return false;
  return llvm::all_of(llvm::drop_begin(Scheme), [](char C) {
    return llvm::isAlnum(C) || C == '+' || C == '.' || C == '-';
  });
}

static llvm::Expected<std::string> uriFromAbsolutePath(StringRef AbsolutePath,
                                                       StringRef Scheme) {
  std::string Body;
  StringRef Authority;
  StringRef Root = llvm::sys::path::root_name(AbsolutePath);
  if (isNetworkPath(Root)) {
    // Windows UNC paths e.g. \\server\share => file://server/share
    Authority = Root.drop_front(2);
    AbsolutePath.consume_front(Root);
  } else if (isWindowsPath(Root)) {
    // Windows paths e.g. X:\path => file:///X:/path
    Body = "/";
  }
  Body += llvm::sys::path::convert_to_slash(AbsolutePath);

  std::string Uri = Scheme.str() + ":";
  if (Authority.empty() && Body.empty())
    return Uri;

  // If authority if empty, we only print body if it starts with "/"; otherwise,
  // the URI is invalid.
  if (!Authority.empty() || StringRef(Body).starts_with("/")) {
    Uri.append("//");
    percentEncode(Authority, Uri);
  }
  percentEncode(Body, Uri);
  return Uri;
}

static llvm::Expected<std::string> getAbsolutePath(StringRef Authority,
                                                   StringRef Body) {
  if (!Body.starts_with("/"))
    return llvm::createStringError(
        llvm::inconvertibleErrorCode(),
        "File scheme: expect body to be an absolute path starting "
        "with '/': " +
            Body);
  SmallString<128> Path;
  if (!Authority.empty()) {
    // Windows UNC paths e.g. file://server/share => \\server\share
    ("//" + Authority).toVector(Path);
  } else if (isWindowsPath(Body.substr(1))) {
    // Windows paths e.g. file:///X:/path => X:\path
    Body.consume_front("/");
  }
  Path.append(Body);
  llvm::sys::path::native(Path);
  return std::string(Path);
}

static llvm::Expected<std::string> parseFilePathFromURI(StringRef OrigUri) {
  StringRef Uri = OrigUri;

  // Decode the scheme of the URI.
  size_t Pos = Uri.find(':');
  if (Pos == StringRef::npos)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "Scheme must be provided in URI: " +
                                       OrigUri);
  StringRef SchemeStr = Uri.substr(0, Pos);
  std::string UriScheme = percentDecode(SchemeStr);
  if (!isStructurallyValidScheme(UriScheme))
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "Invalid scheme: " + SchemeStr +
                                       " (decoded: " + UriScheme + ")");
  Uri = Uri.substr(Pos + 1);

  // Decode the authority of the URI.
  std::string UriAuthority;
  if (Uri.consume_front("//")) {
    Pos = Uri.find('/');
    UriAuthority = percentDecode(Uri.substr(0, Pos));
    Uri = Uri.substr(Pos);
  }

  // Decode the body of the URI.
  std::string UriBody = percentDecode(Uri);

  // Compute the absolute path for this uri.
  if (!getSupportedSchemes().contains(UriScheme)) {
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "unsupported URI scheme `" + UriScheme +
                                       "' for workspace files");
  }
  return getAbsolutePath(UriAuthority, UriBody);
}

llvm::Expected<URIForFile> URIForFile::fromURI(StringRef Uri) {
  llvm::Expected<std::string> FilePath = parseFilePathFromURI(Uri);
  if (!FilePath)
    return FilePath.takeError();
  return URIForFile(std::move(*FilePath), Uri.str());
}

llvm::Expected<URIForFile> URIForFile::fromFile(StringRef AbsoluteFilepath,
                                                StringRef Scheme) {
  llvm::Expected<std::string> Uri =
      uriFromAbsolutePath(AbsoluteFilepath, Scheme);
  if (!Uri)
    return Uri.takeError();
  return fromURI(*Uri);
}

StringRef URIForFile::scheme() const { return uri().split(':').first; }

void URIForFile::registerSupportedScheme(StringRef Scheme) {
  getSupportedSchemes().insert(Scheme);
}

bool llvm::lsp::fromJSON(const llvm::json::Value &Value, URIForFile &Result,
                         llvm::json::Path Path) {
  if (std::optional<StringRef> Str = Value.getAsString()) {
    llvm::Expected<URIForFile> ExpectedUri = URIForFile::fromURI(*Str);
    if (!ExpectedUri) {
      Path.report("unresolvable URI");
      consumeError(ExpectedUri.takeError());
      return false;
    }
    Result = std::move(*ExpectedUri);
    return true;
  }
  return false;
}

llvm::json::Value llvm::lsp::toJSON(const URIForFile &Value) {
  return Value.uri();
}

raw_ostream &llvm::lsp::operator<<(raw_ostream &Os, const URIForFile &Value) {
  return Os << Value.uri();
}

//===----------------------------------------------------------------------===//
// ClientCapabilities
//===----------------------------------------------------------------------===//

bool llvm::lsp::fromJSON(const llvm::json::Value &Value,
                         ClientCapabilities &Result, llvm::json::Path Path) {
  const llvm::json::Object *O = Value.getAsObject();
  if (!O) {
    Path.report("expected object");
    return false;
  }
  if (const llvm::json::Object *TextDocument = O->getObject("textDocument")) {
    if (const llvm::json::Object *DocumentSymbol =
            TextDocument->getObject("documentSymbol")) {
      if (std::optional<bool> HierarchicalSupport =
              DocumentSymbol->getBoolean("hierarchicalDocumentSymbolSupport"))
        Result.HierarchicalDocumentSymbol = *HierarchicalSupport;
    }
    if (auto *CodeAction = TextDocument->getObject("codeAction")) {
      if (CodeAction->getObject("codeActionLiteralSupport"))
        Result.CodeActionStructure = true;
    }
  }
  return true;
}

//===----------------------------------------------------------------------===//
// ClientInfo
//===----------------------------------------------------------------------===//

bool llvm::lsp::fromJSON(const llvm::json::Value &Value, ClientInfo &Result,
                         llvm::json::Path Path) {
  llvm::json::ObjectMapper O(Value, Path);
  if (!O || !O.map("name", Result.Name))
    return false;

  // Don't fail if we can't parse version.
  O.map("version", Result.Version);
  return true;
}

//===----------------------------------------------------------------------===//
// InitializeParams
//===----------------------------------------------------------------------===//

bool llvm::lsp::fromJSON(const llvm::json::Value &Value, TraceLevel &Result,
                         llvm::json::Path Path) {
  if (std::optional<StringRef> Str = Value.getAsString()) {
    if (*Str == "off") {
      Result = TraceLevel::Off;
      return true;
    }
    if (*Str == "messages") {
      Result = TraceLevel::Messages;
      return true;
    }
    if (*Str == "verbose") {
      Result = TraceLevel::Verbose;
      return true;
    }
  }
  return false;
}

bool llvm::lsp::fromJSON(const llvm::json::Value &Value,
                         InitializeParams &Result, llvm::json::Path Path) {
  llvm::json::ObjectMapper O(Value, Path);
  if (!O)
    return false;
  // We deliberately don't fail if we can't parse individual fields.
  O.map("capabilities", Result.Capabilities);
  O.map("trace", Result.Trace);
  mapOptOrNull(Value, "clientInfo", Result.ClientInfo, Path);

  return true;
}

//===----------------------------------------------------------------------===//
// TextDocumentItem
//===----------------------------------------------------------------------===//

bool llvm::lsp::fromJSON(const llvm::json::Value &Value,
                         TextDocumentItem &Result, llvm::json::Path Path) {
  llvm::json::ObjectMapper O(Value, Path);
  return O && O.map("uri", Result.Uri) &&
         O.map("languageId", Result.LanguageId) && O.map("text", Result.Text) &&
         O.map("version", Result.Version);
}

//===----------------------------------------------------------------------===//
// TextDocumentIdentifier
//===----------------------------------------------------------------------===//

llvm::json::Value llvm::lsp::toJSON(const TextDocumentIdentifier &Value) {
  return llvm::json::Object{{"uri", Value.Uri}};
}

bool llvm::lsp::fromJSON(const llvm::json::Value &Value,
                         TextDocumentIdentifier &Result,
                         llvm::json::Path Path) {
  llvm::json::ObjectMapper O(Value, Path);
  return O && O.map("uri", Result.Uri);
}

//===----------------------------------------------------------------------===//
// VersionedTextDocumentIdentifier
//===----------------------------------------------------------------------===//

llvm::json::Value
llvm::lsp::toJSON(const VersionedTextDocumentIdentifier &Value) {
  return llvm::json::Object{
      {"uri", Value.Uri},
      {"version", Value.Version},
  };
}

bool llvm::lsp::fromJSON(const llvm::json::Value &Value,
                         VersionedTextDocumentIdentifier &Result,
                         llvm::json::Path Path) {
  llvm::json::ObjectMapper O(Value, Path);
  return O && O.map("uri", Result.Uri) && O.map("version", Result.Version);
}

//===----------------------------------------------------------------------===//
// Position
//===----------------------------------------------------------------------===//

bool llvm::lsp::fromJSON(const llvm::json::Value &Value, Position &Result,
                         llvm::json::Path Path) {
  llvm::json::ObjectMapper O(Value, Path);
  return O && O.map("line", Result.Line) &&
         O.map("character", Result.Character);
}

llvm::json::Value llvm::lsp::toJSON(const Position &Value) {
  return llvm::json::Object{
      {"line", Value.Line},
      {"character", Value.Character},
  };
}

raw_ostream &llvm::lsp::operator<<(raw_ostream &Os, const Position &Value) {
  return Os << Value.Line << ':' << Value.Character;
}

//===----------------------------------------------------------------------===//
// Range
//===----------------------------------------------------------------------===//

bool llvm::lsp::fromJSON(const llvm::json::Value &Value, Range &Result,
                         llvm::json::Path Path) {
  llvm::json::ObjectMapper O(Value, Path);
  return O && O.map("start", Result.Start) && O.map("end", Result.End);
}

llvm::json::Value llvm::lsp::toJSON(const Range &Value) {
  return llvm::json::Object{
      {"start", Value.Start},
      {"end", Value.End},
  };
}

raw_ostream &llvm::lsp::operator<<(raw_ostream &Os, const Range &Value) {
  return Os << Value.Start << '-' << Value.End;
}

//===----------------------------------------------------------------------===//
// Location
//===----------------------------------------------------------------------===//

bool llvm::lsp::fromJSON(const llvm::json::Value &Value, Location &Result,
                         llvm::json::Path Path) {
  llvm::json::ObjectMapper O(Value, Path);
  return O && O.map("uri", Result.Uri) && O.map("range", Result.Range);
}

llvm::json::Value llvm::lsp::toJSON(const Location &Value) {
  return llvm::json::Object{
      {"uri", Value.Uri},
      {"range", Value.Range},
  };
}

raw_ostream &llvm::lsp::operator<<(raw_ostream &Os, const Location &Value) {
  return Os << Value.Range << '@' << Value.Uri;
}

//===----------------------------------------------------------------------===//
// TextDocumentPositionParams
//===----------------------------------------------------------------------===//

bool llvm::lsp::fromJSON(const llvm::json::Value &Value,
                         TextDocumentPositionParams &Result,
                         llvm::json::Path Path) {
  llvm::json::ObjectMapper O(Value, Path);
  return O && O.map("textDocument", Result.TextDocument) &&
         O.map("position", Result.Position);
}

//===----------------------------------------------------------------------===//
// ReferenceParams
//===----------------------------------------------------------------------===//

bool llvm::lsp::fromJSON(const llvm::json::Value &Value,
                         ReferenceContext &Result, llvm::json::Path Path) {
  llvm::json::ObjectMapper O(Value, Path);
  return O && O.mapOptional("includeDeclaration", Result.IncludeDeclaration);
}

bool llvm::lsp::fromJSON(const llvm::json::Value &Value,
                         ReferenceParams &Result, llvm::json::Path Path) {
  TextDocumentPositionParams &Base = Result;
  llvm::json::ObjectMapper O(Value, Path);
  return fromJSON(Value, Base, Path) && O &&
         O.mapOptional("context", Result.Context);
}

//===----------------------------------------------------------------------===//
// DidOpenTextDocumentParams
//===----------------------------------------------------------------------===//

bool llvm::lsp::fromJSON(const llvm::json::Value &Value,
                         DidOpenTextDocumentParams &Result,
                         llvm::json::Path Path) {
  llvm::json::ObjectMapper O(Value, Path);
  return O && O.map("textDocument", Result.TextDocument);
}

//===----------------------------------------------------------------------===//
// DidCloseTextDocumentParams
//===----------------------------------------------------------------------===//

bool llvm::lsp::fromJSON(const llvm::json::Value &Value,
                         DidCloseTextDocumentParams &Result,
                         llvm::json::Path Path) {
  llvm::json::ObjectMapper O(Value, Path);
  return O && O.map("textDocument", Result.TextDocument);
}

//===----------------------------------------------------------------------===//
// DidChangeTextDocumentParams
//===----------------------------------------------------------------------===//

LogicalResult
TextDocumentContentChangeEvent::applyTo(std::string &Contents) const {
  // If there is no range, the full document changed.
  if (!Range) {
    Contents = Text;
    return success();
  }

  // Try to map the replacement range to the content.
  llvm::SourceMgr TmpScrMgr;
  TmpScrMgr.AddNewSourceBuffer(llvm::MemoryBuffer::getMemBuffer(Contents),
                               SMLoc());
  SMRange RangeLoc = Range->getAsSMRange(TmpScrMgr);
  if (!RangeLoc.isValid())
    return failure();

  Contents.replace(RangeLoc.Start.getPointer() - Contents.data(),
                   RangeLoc.End.getPointer() - RangeLoc.Start.getPointer(),
                   Text);
  return success();
}

LogicalResult TextDocumentContentChangeEvent::applyTo(
    ArrayRef<TextDocumentContentChangeEvent> Changes, std::string &Contents) {
  for (const auto &Change : Changes)
    if (failed(Change.applyTo(Contents)))
      return failure();
  return success();
}

bool llvm::lsp::fromJSON(const llvm::json::Value &Value,
                         TextDocumentContentChangeEvent &Result,
                         llvm::json::Path Path) {
  llvm::json::ObjectMapper O(Value, Path);
  return O && O.map("range", Result.Range) &&
         O.map("rangeLength", Result.RangeLength) && O.map("text", Result.Text);
}

bool llvm::lsp::fromJSON(const llvm::json::Value &Value,
                         DidChangeTextDocumentParams &Result,
                         llvm::json::Path Path) {
  llvm::json::ObjectMapper O(Value, Path);
  return O && O.map("textDocument", Result.TextDocument) &&
         O.map("contentChanges", Result.ContentChanges);
}

//===----------------------------------------------------------------------===//
// MarkupContent
//===----------------------------------------------------------------------===//

static llvm::StringRef toTextKind(MarkupKind Kind) {
  switch (Kind) {
  case MarkupKind::PlainText:
    return "plaintext";
  case MarkupKind::Markdown:
    return "markdown";
  }
  llvm_unreachable("Invalid MarkupKind");
}

raw_ostream &llvm::lsp::operator<<(raw_ostream &Os, MarkupKind Kind) {
  return Os << toTextKind(Kind);
}

llvm::json::Value llvm::lsp::toJSON(const MarkupContent &Mc) {
  if (Mc.Value.empty())
    return nullptr;

  return llvm::json::Object{
      {"kind", toTextKind(Mc.Kind)},
      {"value", Mc.Value},
  };
}

//===----------------------------------------------------------------------===//
// Hover
//===----------------------------------------------------------------------===//

llvm::json::Value llvm::lsp::toJSON(const Hover &Hover) {
  llvm::json::Object Result{{"contents", toJSON(Hover.Contents)}};
  if (Hover.Range)
    Result["range"] = toJSON(*Hover.Range);
  return std::move(Result);
}

//===----------------------------------------------------------------------===//
// DocumentSymbol
//===----------------------------------------------------------------------===//

llvm::json::Value llvm::lsp::toJSON(const DocumentSymbol &Symbol) {
  llvm::json::Object Result{{"name", Symbol.Name},
                            {"kind", static_cast<int>(Symbol.Kind)},
                            {"range", Symbol.Range},
                            {"selectionRange", Symbol.SelectionRange}};

  if (!Symbol.Detail.empty())
    Result["detail"] = Symbol.Detail;
  if (!Symbol.Children.empty())
    Result["children"] = Symbol.Children;
  return std::move(Result);
}

//===----------------------------------------------------------------------===//
// DocumentSymbolParams
//===----------------------------------------------------------------------===//

bool llvm::lsp::fromJSON(const llvm::json::Value &Value,
                         DocumentSymbolParams &Result, llvm::json::Path Path) {
  llvm::json::ObjectMapper O(Value, Path);
  return O && O.map("textDocument", Result.TextDocument);
}

//===----------------------------------------------------------------------===//
// DiagnosticRelatedInformation
//===----------------------------------------------------------------------===//

bool llvm::lsp::fromJSON(const llvm::json::Value &Value,
                         DiagnosticRelatedInformation &Result,
                         llvm::json::Path Path) {
  llvm::json::ObjectMapper O(Value, Path);
  return O && O.map("location", Result.Location) &&
         O.map("message", Result.Message);
}

llvm::json::Value llvm::lsp::toJSON(const DiagnosticRelatedInformation &Info) {
  return llvm::json::Object{
      {"location", Info.Location},
      {"message", Info.Message},
  };
}

//===----------------------------------------------------------------------===//
// Diagnostic
//===----------------------------------------------------------------------===//

llvm::json::Value llvm::lsp::toJSON(DiagnosticTag Tag) {
  return static_cast<int>(Tag);
}

bool llvm::lsp::fromJSON(const llvm::json::Value &Value, DiagnosticTag &Result,
                         llvm::json::Path Path) {
  if (std::optional<int64_t> I = Value.getAsInteger()) {
    Result = (DiagnosticTag)*I;
    return true;
  }

  return false;
}

llvm::json::Value llvm::lsp::toJSON(const Diagnostic &Diag) {
  llvm::json::Object Result{
      {"range", Diag.Range},
      {"severity", (int)Diag.Severity},
      {"message", Diag.Message},
  };
  if (Diag.Category)
    Result["category"] = *Diag.Category;
  if (!Diag.Source.empty())
    Result["source"] = Diag.Source;
  if (Diag.RelatedInformation)
    Result["relatedInformation"] = *Diag.RelatedInformation;
  if (!Diag.Tags.empty())
    Result["tags"] = Diag.Tags;
  return std::move(Result);
}

bool llvm::lsp::fromJSON(const llvm::json::Value &Value, Diagnostic &Result,
                         llvm::json::Path Path) {
  llvm::json::ObjectMapper O(Value, Path);
  if (!O)
    return false;
  int Severity = 0;
  if (!mapOptOrNull(Value, "severity", Severity, Path))
    return false;
  Result.Severity = (DiagnosticSeverity)Severity;

  return O.map("range", Result.Range) && O.map("message", Result.Message) &&
         mapOptOrNull(Value, "category", Result.Category, Path) &&
         mapOptOrNull(Value, "source", Result.Source, Path) &&
         mapOptOrNull(Value, "relatedInformation", Result.RelatedInformation,
                      Path) &&
         mapOptOrNull(Value, "tags", Result.Tags, Path);
}

//===----------------------------------------------------------------------===//
// PublishDiagnosticsParams
//===----------------------------------------------------------------------===//

llvm::json::Value llvm::lsp::toJSON(const PublishDiagnosticsParams &Params) {
  return llvm::json::Object{
      {"uri", Params.Uri},
      {"diagnostics", Params.Diagnostics},
      {"version", Params.Version},
  };
}

//===----------------------------------------------------------------------===//
// ShowMessageParams
//===----------------------------------------------------------------------===//

llvm::json::Value llvm::lsp::toJSON(const ShowMessageParams &Params) {
  return llvm::json::Object{
      {"type", static_cast<int>(Params.Type)},
      {"message", Params.Message},
  };
}

//===----------------------------------------------------------------------===//
// TextEdit
//===----------------------------------------------------------------------===//

bool llvm::lsp::fromJSON(const llvm::json::Value &Value, TextEdit &Result,
                         llvm::json::Path Path) {
  llvm::json::ObjectMapper O(Value, Path);
  return O && O.map("range", Result.Range) && O.map("newText", Result.NewText);
}

llvm::json::Value llvm::lsp::toJSON(const TextEdit &Value) {
  return llvm::json::Object{
      {"range", Value.Range},
      {"newText", Value.NewText},
  };
}

raw_ostream &llvm::lsp::operator<<(raw_ostream &Os, const TextEdit &Value) {
  Os << Value.Range << " => \"";
  llvm::printEscapedString(Value.NewText, Os);
  return Os << '"';
}

//===----------------------------------------------------------------------===//
// CompletionItemKind
//===----------------------------------------------------------------------===//

bool llvm::lsp::fromJSON(const llvm::json::Value &Value,
                         CompletionItemKind &Result, llvm::json::Path Path) {
  if (std::optional<int64_t> IntValue = Value.getAsInteger()) {
    if (*IntValue < static_cast<int>(CompletionItemKind::Text) ||
        *IntValue > static_cast<int>(CompletionItemKind::TypeParameter))
      return false;
    Result = static_cast<CompletionItemKind>(*IntValue);
    return true;
  }
  return false;
}

CompletionItemKind llvm::lsp::adjustKindToCapability(
    CompletionItemKind Kind,
    CompletionItemKindBitset &SupportedCompletionItemKinds) {
  size_t KindVal = static_cast<size_t>(Kind);
  if (KindVal >= KCompletionItemKindMin &&
      KindVal <= SupportedCompletionItemKinds.size() &&
      SupportedCompletionItemKinds[KindVal])
    return Kind;

  // Provide some fall backs for common kinds that are close enough.
  switch (Kind) {
  case CompletionItemKind::Folder:
    return CompletionItemKind::File;
  case CompletionItemKind::EnumMember:
    return CompletionItemKind::Enum;
  case CompletionItemKind::Struct:
    return CompletionItemKind::Class;
  default:
    return CompletionItemKind::Text;
  }
}

bool llvm::lsp::fromJSON(const llvm::json::Value &Value,
                         CompletionItemKindBitset &Result,
                         llvm::json::Path Path) {
  if (const llvm::json::Array *ArrayValue = Value.getAsArray()) {
    for (size_t I = 0, E = ArrayValue->size(); I < E; ++I) {
      CompletionItemKind KindOut;
      if (fromJSON((*ArrayValue)[I], KindOut, Path.index(I)))
        Result.set(size_t(KindOut));
    }
    return true;
  }
  return false;
}

//===----------------------------------------------------------------------===//
// CompletionItem
//===----------------------------------------------------------------------===//

llvm::json::Value llvm::lsp::toJSON(const CompletionItem &Value) {
  assert(!Value.Label.empty() && "completion item label is required");
  llvm::json::Object Result{{"label", Value.Label}};
  if (Value.Kind != CompletionItemKind::Missing)
    Result["kind"] = static_cast<int>(Value.Kind);
  if (!Value.Detail.empty())
    Result["detail"] = Value.Detail;
  if (Value.Documentation)
    Result["documentation"] = Value.Documentation;
  if (!Value.SortText.empty())
    Result["sortText"] = Value.SortText;
  if (!Value.FilterText.empty())
    Result["filterText"] = Value.FilterText;
  if (!Value.InsertText.empty())
    Result["insertText"] = Value.InsertText;
  if (Value.InsertTextFormat != InsertTextFormat::Missing)
    Result["insertTextFormat"] = static_cast<int>(Value.InsertTextFormat);
  if (Value.TextEdit)
    Result["textEdit"] = *Value.TextEdit;
  if (!Value.AdditionalTextEdits.empty()) {
    Result["additionalTextEdits"] =
        llvm::json::Array(Value.AdditionalTextEdits);
  }
  if (Value.Deprecated)
    Result["deprecated"] = Value.Deprecated;
  return std::move(Result);
}

raw_ostream &llvm::lsp::operator<<(raw_ostream &Os,
                                   const CompletionItem &Value) {
  return Os << Value.Label << " - " << toJSON(Value);
}

bool llvm::lsp::operator<(const CompletionItem &Lhs,
                          const CompletionItem &Rhs) {
  return (Lhs.SortText.empty() ? Lhs.Label : Lhs.SortText) <
         (Rhs.SortText.empty() ? Rhs.Label : Rhs.SortText);
}

//===----------------------------------------------------------------------===//
// CompletionList
//===----------------------------------------------------------------------===//

llvm::json::Value llvm::lsp::toJSON(const CompletionList &Value) {
  return llvm::json::Object{
      {"isIncomplete", Value.IsIncomplete},
      {"items", llvm::json::Array(Value.Items)},
  };
}

//===----------------------------------------------------------------------===//
// CompletionContext
//===----------------------------------------------------------------------===//

bool llvm::lsp::fromJSON(const llvm::json::Value &Value,
                         CompletionContext &Result, llvm::json::Path Path) {
  llvm::json::ObjectMapper O(Value, Path);
  int TriggerKind;
  if (!O || !O.map("triggerKind", TriggerKind) ||
      !mapOptOrNull(Value, "triggerCharacter", Result.TriggerCharacter, Path))
    return false;
  Result.TriggerKind = static_cast<CompletionTriggerKind>(TriggerKind);
  return true;
}

//===----------------------------------------------------------------------===//
// CompletionParams
//===----------------------------------------------------------------------===//

bool llvm::lsp::fromJSON(const llvm::json::Value &Value,
                         CompletionParams &Result, llvm::json::Path Path) {
  if (!fromJSON(Value, static_cast<TextDocumentPositionParams &>(Result), Path))
    return false;
  if (const llvm::json::Value *Context = Value.getAsObject()->get("context"))
    return fromJSON(*Context, Result.Context, Path.field("context"));
  return true;
}

//===----------------------------------------------------------------------===//
// ParameterInformation
//===----------------------------------------------------------------------===//

llvm::json::Value llvm::lsp::toJSON(const ParameterInformation &Value) {
  assert((Value.LabelOffsets || !Value.LabelString.empty()) &&
         "parameter information label is required");
  llvm::json::Object Result;
  if (Value.LabelOffsets)
    Result["label"] = llvm::json::Array(
        {Value.LabelOffsets->first, Value.LabelOffsets->second});
  else
    Result["label"] = Value.LabelString;
  if (!Value.Documentation.empty())
    Result["documentation"] = Value.Documentation;
  return std::move(Result);
}

//===----------------------------------------------------------------------===//
// SignatureInformation
//===----------------------------------------------------------------------===//

llvm::json::Value llvm::lsp::toJSON(const SignatureInformation &Value) {
  assert(!Value.Label.empty() && "signature information label is required");
  llvm::json::Object Result{
      {"label", Value.Label},
      {"parameters", llvm::json::Array(Value.Parameters)},
  };
  if (!Value.Documentation.empty())
    Result["documentation"] = Value.Documentation;
  return std::move(Result);
}

raw_ostream &llvm::lsp::operator<<(raw_ostream &Os,
                                   const SignatureInformation &Value) {
  return Os << Value.Label << " - " << toJSON(Value);
}

//===----------------------------------------------------------------------===//
// SignatureHelp
//===----------------------------------------------------------------------===//

llvm::json::Value llvm::lsp::toJSON(const SignatureHelp &Value) {
  assert(Value.ActiveSignature >= 0 &&
         "Unexpected negative value for number of active signatures.");
  assert(Value.ActiveParameter >= 0 &&
         "Unexpected negative value for active parameter index");
  return llvm::json::Object{
      {"activeSignature", Value.ActiveSignature},
      {"activeParameter", Value.ActiveParameter},
      {"signatures", llvm::json::Array(Value.Signatures)},
  };
}

//===----------------------------------------------------------------------===//
// DocumentLinkParams
//===----------------------------------------------------------------------===//

bool llvm::lsp::fromJSON(const llvm::json::Value &Value,
                         DocumentLinkParams &Result, llvm::json::Path Path) {
  llvm::json::ObjectMapper O(Value, Path);
  return O && O.map("textDocument", Result.TextDocument);
}

//===----------------------------------------------------------------------===//
// DocumentLink
//===----------------------------------------------------------------------===//

llvm::json::Value llvm::lsp::toJSON(const DocumentLink &Value) {
  return llvm::json::Object{
      {"range", Value.Range},
      {"target", Value.Target},
  };
}

//===----------------------------------------------------------------------===//
// InlayHintsParams
//===----------------------------------------------------------------------===//

bool llvm::lsp::fromJSON(const llvm::json::Value &Value,
                         InlayHintsParams &Result, llvm::json::Path Path) {
  llvm::json::ObjectMapper O(Value, Path);
  return O && O.map("textDocument", Result.TextDocument) &&
         O.map("range", Result.Range);
}

//===----------------------------------------------------------------------===//
// InlayHint
//===----------------------------------------------------------------------===//

llvm::json::Value llvm::lsp::toJSON(const InlayHint &Value) {
  return llvm::json::Object{{"position", Value.Position},
                            {"kind", (int)Value.Kind},
                            {"label", Value.Label},
                            {"paddingLeft", Value.PaddingLeft},
                            {"paddingRight", Value.PaddingRight}};
}
bool llvm::lsp::operator==(const InlayHint &Lhs, const InlayHint &Rhs) {
  return std::tie(Lhs.Position, Lhs.Kind, Lhs.Label) ==
         std::tie(Rhs.Position, Rhs.Kind, Rhs.Label);
}
bool llvm::lsp::operator<(const InlayHint &Lhs, const InlayHint &Rhs) {
  return std::tie(Lhs.Position, Lhs.Kind, Lhs.Label) <
         std::tie(Rhs.Position, Rhs.Kind, Rhs.Label);
}

llvm::raw_ostream &llvm::lsp::operator<<(llvm::raw_ostream &Os,
                                         InlayHintKind Value) {
  switch (Value) {
  case InlayHintKind::Parameter:
    return Os << "parameter";
  case InlayHintKind::Type:
    return Os << "type";
  }
  llvm_unreachable("Unknown InlayHintKind");
}

//===----------------------------------------------------------------------===//
// CodeActionContext
//===----------------------------------------------------------------------===//

bool llvm::lsp::fromJSON(const llvm::json::Value &Value,
                         CodeActionContext &Result, llvm::json::Path Path) {
  llvm::json::ObjectMapper O(Value, Path);
  if (!O || !O.map("diagnostics", Result.Diagnostics))
    return false;
  O.map("only", Result.Only);
  return true;
}

//===----------------------------------------------------------------------===//
// CodeActionParams
//===----------------------------------------------------------------------===//

bool llvm::lsp::fromJSON(const llvm::json::Value &Value,
                         CodeActionParams &Result, llvm::json::Path Path) {
  llvm::json::ObjectMapper O(Value, Path);
  return O && O.map("textDocument", Result.TextDocument) &&
         O.map("range", Result.Range) && O.map("context", Result.Context);
}

//===----------------------------------------------------------------------===//
// WorkspaceEdit
//===----------------------------------------------------------------------===//

bool llvm::lsp::fromJSON(const llvm::json::Value &Value, WorkspaceEdit &Result,
                         llvm::json::Path Path) {
  llvm::json::ObjectMapper O(Value, Path);
  return O && O.map("changes", Result.Changes);
}

llvm::json::Value llvm::lsp::toJSON(const WorkspaceEdit &Value) {
  llvm::json::Object FileChanges;
  for (auto &Change : Value.Changes)
    FileChanges[Change.first] = llvm::json::Array(Change.second);
  return llvm::json::Object{{"changes", std::move(FileChanges)}};
}

//===----------------------------------------------------------------------===//
// CodeAction
//===----------------------------------------------------------------------===//

const llvm::StringLiteral CodeAction::KQuickFix = "quickfix";
const llvm::StringLiteral CodeAction::KRefactor = "refactor";
const llvm::StringLiteral CodeAction::KInfo = "info";

llvm::json::Value llvm::lsp::toJSON(const CodeAction &Value) {
  llvm::json::Object CodeAction{{"title", Value.Title}};
  if (Value.Kind)
    CodeAction["kind"] = *Value.Kind;
  if (Value.Diagnostics)
    CodeAction["diagnostics"] = llvm::json::Array(*Value.Diagnostics);
  if (Value.IsPreferred)
    CodeAction["isPreferred"] = true;
  if (Value.Edit)
    CodeAction["edit"] = *Value.Edit;
  return std::move(CodeAction);
}
