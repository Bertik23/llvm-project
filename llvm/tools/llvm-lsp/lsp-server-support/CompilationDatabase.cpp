//===- CompilationDatabase.cpp - LSP Compilation Database -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CompilationDatabase.h"
#include "Logging.h"
#include "Protocol.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/FileUtilities.h"
#include "llvm/Support/YAMLTraits.h"

using namespace llvm;
using namespace llvm::lsp;

//===----------------------------------------------------------------------===//
// YamlFileInfo
//===----------------------------------------------------------------------===//

namespace {
struct YamlFileInfo {
  /// The absolute path to the file.
  std::string Filename;
  /// The include directories available for the file.
  std::vector<std::string> IncludeDirs;
};
} // namespace

//===----------------------------------------------------------------------===//
// CompilationDatabase
//===----------------------------------------------------------------------===//

LLVM_YAML_IS_DOCUMENT_LIST_VECTOR(YamlFileInfo)

namespace llvm {
namespace yaml {
template <>
struct MappingTraits<YamlFileInfo> {
  static void mapping(IO &Io, YamlFileInfo &Info) {
    // Parse the filename and normalize it to the form we will expect from
    // incoming URIs.
    Io.mapRequired("filepath", Info.Filename);

    // Normalize the filename to avoid incompatability with incoming URIs.
    if (Expected<lsp::URIForFile> Uri =
            lsp::URIForFile::fromFile(Info.Filename))
      Info.Filename = Uri->file().str();

    // Parse the includes from the yaml stream. These are in the form of a
    // semi-colon delimited list.
    std::string CombinedIncludes;
    Io.mapRequired("includes", CombinedIncludes);
    for (StringRef Include : llvm::split(CombinedIncludes, ";")) {
      if (!Include.empty())
        Info.IncludeDirs.push_back(Include.str());
    }
  }
};
} // end namespace yaml
} // end namespace llvm

CompilationDatabase::CompilationDatabase(ArrayRef<std::string> Databases) {
  for (StringRef Filename : Databases)
    loadDatabase(Filename);
}

const CompilationDatabase::FileInfo &
CompilationDatabase::getFileInfo(StringRef Filename) const {
  auto It = Files.find(Filename);
  return It == Files.end() ? DefaultFileInfo : It->second;
}

void CompilationDatabase::loadDatabase(StringRef Filename) {
  if (Filename.empty())
    return;

  // Set up the input file.
  std::string ErrorMessage;
  std::unique_ptr<llvm::MemoryBuffer> InputFile;
  //     inputFile(filename, &errorMessage);
  if (!InputFile) {
    Logger::error("Failed to open compilation database: {0}", ErrorMessage);
    return;
  }
  llvm::yaml::Input Yaml(InputFile->getBuffer());

  // Parse the yaml description and add any new files to the database.
  std::vector<YamlFileInfo> ParsedFiles;
  Yaml >> ParsedFiles;

  SetVector<StringRef> KnownIncludes;
  for (auto &File : ParsedFiles) {
    auto It = Files.try_emplace(File.Filename, std::move(File.IncludeDirs));

    // If we encounter a duplicate file, log a warning and ignore it.
    if (!It.second) {
      Logger::info("Duplicate file in compilation database: {0}",
                   File.Filename);
      continue;
    }

    // Track the includes for the file.
    KnownIncludes.insert_range(It.first->second.IncludeDirs);
  }

  // Add all of the known includes to the default file info. We don't know any
  // information about how to treat these files, but these may be project files
  // that we just don't yet have information for. In these cases, providing some
  // heuristic information provides a better user experience, and generally
  // shouldn't lead to any negative side effects.
  for (StringRef Include : KnownIncludes)
    DefaultFileInfo.IncludeDirs.push_back(Include.str());
}
