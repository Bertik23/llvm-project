//===--- SourceMgrUtils.h - SourceMgr LSP Utils -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains an array of generally useful SourceMgr utilities for
// interacting with LSP components.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_LSP_LSP_SERVER_SUPPORT_SOURCEMGRUTILS_H
#define LLVM_TOOLS_LLVM_LSP_LSP_SERVER_SUPPORT_SOURCEMGRUTILS_H

#include "Protocol.h"
#include "llvm/Support/SourceMgr.h"
#include <optional>

namespace llvm {
namespace lsp {
//===----------------------------------------------------------------------===//
// Utils
//===----------------------------------------------------------------------===//

/// Returns the range of a lexical token given a SMLoc corresponding to the
/// start of an token location. The range is computed heuristically, and
/// supports identifier-like tokens, strings, etc.
SMRange convertTokenLocToRange(SMLoc Loc, StringRef IdentifierChars = "");

/// Extract a documentation comment for the given location within the source
/// manager. Returns std::nullopt if no comment could be computed.
std::optional<std::string> extractSourceDocComment(llvm::SourceMgr &SourceMgr,
                                                   SMLoc Loc);

/// Returns true if the given range contains the given source location. Note
/// that this has different behavior than SMRange because it is inclusive of the
/// end location.
bool contains(SMRange Range, SMLoc Loc);

//===----------------------------------------------------------------------===//
// SourceMgrInclude
//===----------------------------------------------------------------------===//

/// This class represents a single include within a root file.
struct SourceMgrInclude {
  SourceMgrInclude(const lsp::URIForFile &Uri, const lsp::Range &Range)
      : Uri(Uri), Range(Range) {}

  /// Build a hover for the current include file.
  Hover buildHover() const;

  /// The URI of the file that is included.
  lsp::URIForFile Uri;

  /// The range of the include directive.
  lsp::Range Range;
};

/// Given a source manager, gather all of the processed include files. These are
/// assumed to be all of the files other than the main root file.
void gatherIncludeFiles(llvm::SourceMgr &SourceMgr,
                        SmallVectorImpl<SourceMgrInclude> &Includes);

} // namespace lsp
} // namespace llvm

#endif
