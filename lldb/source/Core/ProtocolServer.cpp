//===-- ProtocolServer.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/ProtocolServer.h"
#include "lldb/Core/PluginManager.h"

using namespace lldb_private;
using namespace lldb;

ProtocolServerSP ProtocolServer::Create(llvm::StringRef name,
                                        Debugger &debugger) {
  if (ProtocolServerCreateInstance create_callback =
          PluginManager::GetProtocolCreateCallbackForPluginName(name))
    return create_callback(debugger);
  return nullptr;
}
