//===--- Protocol.cpp - Language Server Protocol Implementation -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Protocol.h"

using namespace llvm;
using namespace llvm::lsp;

bool llvm::lsp::fromJSON(const llvm::json::Value &Value, GetCfgParams &Result,
                         llvm::json::Path Path) {
  llvm::json::ObjectMapper O(Value, Path);
  return O && O.map("uri", Result.uri) &&
         O.map("position", Result.position);
}

llvm::json::Value llvm::lsp::toJSON(const CFG &Value) {
  return llvm::json::Object{{"uri", Value.uri},
                            {"node_id", Value.node_id},
                            {"function", Value.function}};
}

bool llvm::lsp::fromJSON(const llvm::json::Value &Value,
                         BbLocationParams &Result, llvm::json::Path Path) {
  llvm::json::ObjectMapper O(Value, Path);
  return O && O.map("uri", Result.uri) && O.map("node_id", Result.node_id);
}

llvm::json::Value llvm::lsp::toJSON(const BbLocation &Value) {
  return llvm::json::Object{{"uri", Value.uri}, {"range", Value.range}};
}
