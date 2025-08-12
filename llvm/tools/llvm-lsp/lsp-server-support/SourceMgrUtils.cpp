//===--- SourceMgrUtils.cpp - SourceMgr LSP Utils -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "SourceMgrUtils.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/Path.h"
#include <optional>

using namespace llvm;
using namespace llvm::lsp;

//===----------------------------------------------------------------------===//
// Utils
//===----------------------------------------------------------------------===//

/// Find the end of a string whose contents start at the given `curPtr`. Returns
/// the position at the end of the string, after a terminal or invalid character
/// (e.g. `"` or `\0`).
static const char *lexLocStringTok(const char *CurPtr) {
  while (char C = *CurPtr++) {
    // Check for various terminal characters.
    if (StringRef("\"\n\v\f").contains(C))
      return CurPtr;

    // Check for escape sequences.
    if (C == '\\') {
      // Check a few known escapes and \xx hex digits.
      if (*CurPtr == '"' || *CurPtr == '\\' || *CurPtr == 'n' || *CurPtr == 't')
        ++CurPtr;
      else if (llvm::isHexDigit(*CurPtr) && llvm::isHexDigit(CurPtr[1]))
        CurPtr += 2;
      else
        return CurPtr;
    }
  }

  // If we hit this point, we've reached the end of the buffer. Update the end
  // pointer to not point past the buffer.
  return CurPtr - 1;
}

SMRange lsp::convertTokenLocToRange(SMLoc Loc, StringRef IdentifierChars) {
  if (!Loc.isValid())
    return SMRange();
  const char *CurPtr = Loc.getPointer();

  // Check if this is a string token.
  if (*CurPtr == '"') {
    CurPtr = lexLocStringTok(CurPtr + 1);

    // Otherwise, default to handling an identifier.
  } else {
    // Return if the given character is a valid identifier character.
    auto IsIdentifierChar = [=](char C) {
      return isalnum(C) || C == '_' || IdentifierChars.contains(C);
    };

    while (*CurPtr && IsIdentifierChar(*(++CurPtr)))
      continue;
  }

  return SMRange(Loc, SMLoc::getFromPointer(CurPtr));
}

std::optional<std::string>
lsp::extractSourceDocComment(llvm::SourceMgr &SourceMgr, SMLoc Loc) {
  // This is a heuristic, and isn't intended to cover every case, but should
  // cover the most common. We essentially look for a comment preceding the
  // line, and if we find one, use that as the documentation.
  if (!Loc.isValid())
    return std::nullopt;
  int BufferId = SourceMgr.FindBufferContainingLoc(Loc);
  if (BufferId == 0)
    return std::nullopt;
  const char *BufferStart =
      SourceMgr.getMemoryBuffer(BufferId)->getBufferStart();
  StringRef Buffer(BufferStart, Loc.getPointer() - BufferStart);

  // Pop the last line from the buffer string.
  auto PopLastLine = [&]() -> std::optional<StringRef> {
    size_t NewlineOffset = Buffer.find_last_of('\n');
    if (NewlineOffset == StringRef::npos)
      return std::nullopt;
    StringRef LastLine = Buffer.drop_front(NewlineOffset).trim();
    Buffer = Buffer.take_front(NewlineOffset);
    return LastLine;
  };

  // Try to pop the current line.
  if (!PopLastLine())
    return std::nullopt;

  // Try to parse a comment string from the source file.
  SmallVector<StringRef> CommentLines;
  while (std::optional<StringRef> Line = PopLastLine()) {
    // Check for a comment at the beginning of the line.
    if (!Line->starts_with("//"))
      break;

    // Extract the document string from the comment.
    CommentLines.push_back(Line->ltrim('/'));
  }

  if (CommentLines.empty())
    return std::nullopt;
  return llvm::join(llvm::reverse(CommentLines), "\n");
}

bool lsp::contains(SMRange Range, SMLoc Loc) {
  return Range.Start.getPointer() <= Loc.getPointer() &&
         Loc.getPointer() < Range.End.getPointer();
}

//===----------------------------------------------------------------------===//
// SourceMgrInclude
//===----------------------------------------------------------------------===//

Hover SourceMgrInclude::buildHover() const {
  Hover Hover(Range);
  {
    llvm::raw_string_ostream HoverOs(Hover.Contents.Value);
    HoverOs << "`" << llvm::sys::path::filename(Uri.file()) << "`\n***\n"
            << Uri.file();
  }
  return Hover;
}

void lsp::gatherIncludeFiles(llvm::SourceMgr &SourceMgr,
                             SmallVectorImpl<SourceMgrInclude> &Includes) {
  for (unsigned I = 1, E = SourceMgr.getNumBuffers(); I < E; ++I) {
    // Check to see if this file was included by the main file.
    SMLoc IncludeLoc = SourceMgr.getBufferInfo(I + 1).IncludeLoc;
    if (!IncludeLoc.isValid() || SourceMgr.FindBufferContainingLoc(
                                     IncludeLoc) != SourceMgr.getMainFileID())
      continue;

    // Try to build a URI for this file path.
    auto *Buffer = SourceMgr.getMemoryBuffer(I + 1);
    llvm::SmallString<256> Path(Buffer->getBufferIdentifier());
    llvm::sys::path::remove_dots(Path, /*remove_dot_dot=*/true);

    llvm::Expected<URIForFile> IncludedFileUri = URIForFile::fromFile(Path);
    if (!IncludedFileUri)
      continue;

    // Find the end of the include token.
    const char *IncludeStart = IncludeLoc.getPointer() - 2;
    while (*(--IncludeStart) != '\"')
      continue;

    // Push this include.
    SMRange IncludeRange(SMLoc::getFromPointer(IncludeStart), IncludeLoc);
    Includes.emplace_back(*IncludedFileUri, Range(SourceMgr, IncludeRange));
  }
}
