// Copyright (c) 2013-2014 Sandstorm Development Group, Inc. and contributors
// Licensed under the MIT License:
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#include "schema-parser.h"
#include "message.h"
#include <capnp/compiler/compiler.h>
#include <capnp/compiler/lexer.capnp.h>
#include <capnp/compiler/lexer.h>
#include <capnp/compiler/grammar.capnp.h>
#include <capnp/compiler/parser.h>
#include <unordered_map>
#include <kj/mutex.h>
#include <kj/vector.h>
#include <kj/debug.h>
#include <kj/io.h>
#include <map>

namespace capnp {

namespace {

template <typename T>
size_t findLargestElementBefore(const kj::Vector<T>& vec, const T& key) {
  KJ_REQUIRE(vec.size() > 0 && vec[0] <= key);

  size_t lower = 0;
  size_t upper = vec.size();

  while (upper - lower > 1) {
    size_t mid = (lower + upper) / 2;
    if (vec[mid] > key) {
      upper = mid;
    } else {
      lower = mid;
    }
  }

  return lower;
}

}  // namespace

// =======================================================================================

class SchemaParser::ModuleImpl final: public compiler::Module {
public:
  ModuleImpl(const SchemaParser& parser, kj::Own<const SchemaFile>&& file)
      : parser(parser), file(kj::mv(file)) {}

  kj::StringPtr getSourceName() override {
    return file->getDisplayName();
  }

  Orphan<compiler::ParsedFile> loadContent(Orphanage orphanage) override {
    kj::Array<const char> content = file->readContent();

    lineBreaks.get([&](kj::SpaceFor<kj::Vector<uint>>& space) {
      auto vec = space.construct(content.size() / 40);
      vec->add(0);
      for (const char* pos = content.begin(); pos < content.end(); ++pos) {
        if (*pos == '\n') {
          vec->add(pos + 1 - content.begin());
        }
      }
      return vec;
    });

    MallocMessageBuilder lexedBuilder;
    auto statements = lexedBuilder.initRoot<compiler::LexedStatements>();
    compiler::lex(content, statements, *this);

    auto parsed = orphanage.newOrphan<compiler::ParsedFile>();
    compiler::parseFile(statements.getStatements(), parsed.get(), *this);
    return parsed;
  }

  kj::Maybe<Module&> importRelative(kj::StringPtr importPath) override {
    KJ_IF_MAYBE(importedFile, file->import(importPath)) {
      return parser.getModuleImpl(kj::mv(*importedFile));
    } else {
      return nullptr;
    }
  }

  kj::Maybe<kj::Array<const byte>> embedRelative(kj::StringPtr embedPath) override {
    KJ_IF_MAYBE(importedFile, file->import(embedPath)) {
      return importedFile->get()->readContent().releaseAsBytes();
    } else {
      return nullptr;
    }
  }

  void addError(uint32_t startByte, uint32_t endByte, kj::StringPtr message) override {
    auto& lines = lineBreaks.get(
        [](kj::SpaceFor<kj::Vector<uint>>& space) {
          KJ_FAIL_REQUIRE("Can't report errors until loadContent() is called.");
          return space.construct();
        });

    // TODO(someday):  This counts tabs as single characters.  Do we care?
    uint startLine = findLargestElementBefore(lines, startByte);
    uint startCol = startByte - lines[startLine];
    uint endLine = findLargestElementBefore(lines, endByte);
    uint endCol = endByte - lines[endLine];

    file->reportError(
        SchemaFile::SourcePos { startByte, startLine, startCol },
        SchemaFile::SourcePos { endByte, endLine, endCol },
        message);

    // We intentionally only set hadErrors true if reportError() didn't throw.
    parser.hadErrors = true;
  }

  bool hadErrors() override {
    return parser.hadErrors;
  }

private:
  const SchemaParser& parser;
  kj::Own<const SchemaFile> file;

  kj::Lazy<kj::Vector<uint>> lineBreaks;
  // Byte offsets of the first byte in each source line.  The first element is always zero.
  // Initialized the first time the module is loaded.
};

// =======================================================================================

namespace {

struct SchemaFileHash {
  inline bool operator()(const SchemaFile* f) const {
    return f->hashCode();
  }
};

struct SchemaFileEq {
  inline bool operator()(const SchemaFile* a, const SchemaFile* b) const {
    return *a == *b;
  }
};

}  // namespace

struct SchemaParser::DiskFileCompat {
  // Stuff we only create if parseDiskFile() is ever called, in order to translate that call into
  // KJ filesystem API calls.

  kj::Own<kj::Filesystem> ownFs;
  kj::Filesystem& fs;

  struct ImportDir {
    kj::String pathStr;
    kj::Path path;
    kj::Own<const kj::ReadableDirectory> dir;
  };
  std::map<kj::StringPtr, ImportDir> cachedImportDirs;

  std::map<std::pair<const kj::StringPtr*, size_t>, kj::Array<const kj::ReadableDirectory*>>
      cachedImportPaths;

  DiskFileCompat(): ownFs(kj::newDiskFilesystem()), fs(*ownFs) {}
  DiskFileCompat(kj::Filesystem& fs): fs(fs) {}
};

struct SchemaParser::Impl {
  typedef std::unordered_map<
      const SchemaFile*, kj::Own<ModuleImpl>, SchemaFileHash, SchemaFileEq> FileMap;
  kj::MutexGuarded<FileMap> fileMap;
  compiler::Compiler compiler;

  kj::MutexGuarded<kj::Maybe<DiskFileCompat>> compat;
};

SchemaParser::SchemaParser(): impl(kj::heap<Impl>()) {}
SchemaParser::~SchemaParser() noexcept(false) {}

ParsedSchema SchemaParser::parseFromDirectory(
    const kj::ReadableDirectory& baseDir, kj::Path path,
    kj::ArrayPtr<const kj::ReadableDirectory* const> importPath) const {
  return parseFile(SchemaFile::newFromDirectory(baseDir, kj::mv(path), importPath));
}

ParsedSchema SchemaParser::parseDiskFile(
    kj::StringPtr displayName, kj::StringPtr diskPath,
    kj::ArrayPtr<const kj::StringPtr> importPath) const {
  auto lock = impl->compat.lockExclusive();
  DiskFileCompat* compat;
  KJ_IF_MAYBE(c, *lock) {
    compat = c;
  } else {
    compat = &lock->emplace();
  }

  auto& root = compat->fs.getRoot();
  auto cwd = compat->fs.getCurrentPath();

  const kj::ReadableDirectory* baseDir = &root;
  kj::Path path = cwd.evalNative(diskPath);

  kj::ArrayPtr<const kj::ReadableDirectory* const> translatedImportPath = nullptr;

  if (importPath.size() > 0) {
    auto importPathKey = std::make_pair(importPath.begin(), importPath.size());
    auto& slot = compat->cachedImportPaths[importPathKey];

    if (slot == nullptr) {
      slot = KJ_MAP(path, importPath) -> const kj::ReadableDirectory* {
        auto iter = compat->cachedImportDirs.find(path);
        if (iter != compat->cachedImportDirs.end()) {
          return iter->second.dir;
        }

        auto parsed = cwd.evalNative(path);
        kj::Own<const kj::ReadableDirectory> dir;
        KJ_IF_MAYBE(d, root.tryOpenSubdir(parsed)) {
          dir = kj::mv(*d);
        } else {
          // Ignore paths that don't exist.
          dir = kj::newInMemoryDirectory(kj::nullClock());
        }

        const kj::ReadableDirectory* result = dir;

        kj::StringPtr pathRef = path;
        KJ_ASSERT(compat->cachedImportDirs.insert(std::make_pair(pathRef,
            DiskFileCompat::ImportDir { kj::str(path), kj::mv(parsed), kj::mv(dir) })).second);

        return result;
      };
    }

    translatedImportPath = slot;

    // Check if `path` appears to be inside any of the import path directories. If so, adjust
    // to be relative to that directory rather than absolute.
    kj::Maybe<DiskFileCompat::ImportDir&> matchedImportDir;
    size_t bestMatchLength = 0;
    for (auto importDir: importPath) {
      auto iter = compat->cachedImportDirs.find(importDir);
      KJ_ASSERT(iter != compat->cachedImportDirs.end());

      if (path.startsWith(iter->second.path)) {
        // Looks like we're trying to load a file from inside this import path. Treat the import
        // path as the base directory.
        if (iter->second.path.size() > bestMatchLength) {
          bestMatchLength = iter->second.path.size();
          matchedImportDir = iter->second;
        }
      }
    }

    KJ_IF_MAYBE(match, matchedImportDir) {
      baseDir = match->dir;
      path = path.slice(match->path.size(), path.size()).clone();
    }
  }

  return parseFile(SchemaFile::newFromDirectory(
      *baseDir, kj::mv(path), translatedImportPath, kj::str(displayName)));
}

void SchemaParser::setDiskFilesystem(kj::Filesystem& fs) {
  auto lock = impl->compat.lockExclusive();
  KJ_REQUIRE(*lock == nullptr, "already called parseDiskFile() or setDiskFilesystem()");
  lock->emplace(fs);
}

ParsedSchema SchemaParser::parseFile(kj::Own<SchemaFile>&& file) const {
  KJ_DEFER(impl->compiler.clearWorkspace());
  uint64_t id = impl->compiler.add(getModuleImpl(kj::mv(file)));
  impl->compiler.eagerlyCompile(id,
      compiler::Compiler::NODE | compiler::Compiler::CHILDREN |
      compiler::Compiler::DEPENDENCIES | compiler::Compiler::DEPENDENCY_DEPENDENCIES);
  return ParsedSchema(impl->compiler.getLoader().get(id), *this);
}

kj::Maybe<schema::Node::SourceInfo::Reader> SchemaParser::getSourceInfo(Schema schema) const {
  return impl->compiler.getSourceInfo(schema.getProto().getId());
}

SchemaParser::ModuleImpl& SchemaParser::getModuleImpl(kj::Own<SchemaFile>&& file) const {
  auto lock = impl->fileMap.lockExclusive();

  auto insertResult = lock->insert(std::make_pair(file.get(), kj::Own<ModuleImpl>()));
  if (insertResult.second) {
    // This is a newly-inserted entry.  Construct the ModuleImpl.
    insertResult.first->second = kj::heap<ModuleImpl>(*this, kj::mv(file));
  }
  return *insertResult.first->second;
}

SchemaLoader& SchemaParser::getLoader() {
  return impl->compiler.getLoader();
}

kj::Maybe<ParsedSchema> ParsedSchema::findNested(kj::StringPtr name) const {
  return parser->impl->compiler.lookup(getProto().getId(), name).map(
      [this](uint64_t childId) {
        return ParsedSchema(parser->impl->compiler.getLoader().get(childId), *parser);
      });
}

ParsedSchema ParsedSchema::getNested(kj::StringPtr nestedName) const {
  KJ_IF_MAYBE(nested, findNested(nestedName)) {
    return *nested;
  } else {
    KJ_FAIL_REQUIRE("no such nested declaration", getProto().getDisplayName(), nestedName);
  }
}

schema::Node::SourceInfo::Reader ParsedSchema::getSourceInfo() const {
  return KJ_ASSERT_NONNULL(parser->getSourceInfo(*this));
}

// -------------------------------------------------------------------

class SchemaFile::DiskSchemaFile final: public SchemaFile {
public:
  DiskSchemaFile(const kj::ReadableDirectory& baseDir, kj::Path pathParam,
                 kj::ArrayPtr<const kj::ReadableDirectory* const> importPath,
                 kj::Own<const kj::ReadableFile> file,
                 kj::Maybe<kj::String> displayNameOverride)
      : baseDir(baseDir), path(kj::mv(pathParam)), importPath(importPath), file(kj::mv(file)) {
    KJ_IF_MAYBE(dn, displayNameOverride) {
      displayName = kj::mv(*dn);
      displayNameOverridden = true;
    } else {
      displayName = path.toString();
      displayNameOverridden = false;
    }
  }

  kj::StringPtr getDisplayName() const override {
    return displayName;
  }

  kj::Array<const char> readContent() const override {
    return file->mmap(0, file->stat().size).releaseAsChars();
  }

  kj::Maybe<kj::Own<SchemaFile>> import(kj::StringPtr target) const override {
    if (target.startsWith("/")) {
      auto parsed = kj::Path::parse(target.slice(1));
      for (auto candidate: importPath) {
        KJ_IF_MAYBE(newFile, candidate->tryOpenFile(parsed)) {
          return kj::implicitCast<kj::Own<SchemaFile>>(kj::heap<DiskSchemaFile>(
              *candidate, kj::mv(parsed), importPath, kj::mv(*newFile), nullptr));
        }
      }
      return nullptr;
    } else {
      auto parsed = path.parent().eval(target);

      kj::Maybe<kj::String> displayNameOverride;
      if (displayNameOverridden) {
        // Try to create a consistent display name override for the imported file. This is for
        // backwards-compatibility only -- display names are only overridden when using the
        // deprecated parseDiskFile() interface.
        kj::runCatchingExceptions([&]() {
          displayNameOverride = kj::Path::parse(displayName).parent().eval(target).toString();
        });
      }

      KJ_IF_MAYBE(newFile, baseDir.tryOpenFile(parsed)) {
        return kj::implicitCast<kj::Own<SchemaFile>>(kj::heap<DiskSchemaFile>(
            baseDir, kj::mv(parsed), importPath, kj::mv(*newFile), kj::mv(displayNameOverride)));
      } else {
        return nullptr;
      }
    }
  }

  bool operator==(const SchemaFile& other) const override {
    auto& other2 = kj::downcast<const DiskSchemaFile>(other);
    return &baseDir == &other2.baseDir && path == other2.path;
  }
  bool operator!=(const SchemaFile& other) const override {
    return !operator==(other);
  }
  size_t hashCode() const override {
    // djb hash with xor
    // TODO(someday):  Add hashing library to KJ.
    size_t result = reinterpret_cast<uintptr_t>(&baseDir);
    for (auto& part: path) {
      for (char c: part) {
        result = (result * 33) ^ c;
      }
      result = (result * 33) ^ '/';
    }
    return result;
  }

  void reportError(SourcePos start, SourcePos end, kj::StringPtr message) const override {
    kj::getExceptionCallback().onRecoverableException(kj::Exception(
        kj::Exception::Type::FAILED, path.toString(), start.line,
        kj::heapString(message)));
  }

private:
  const kj::ReadableDirectory& baseDir;
  kj::Path path;
  kj::ArrayPtr<const kj::ReadableDirectory* const> importPath;
  kj::Own<const kj::ReadableFile> file;
  kj::String displayName;
  bool displayNameOverridden;
};

kj::Own<SchemaFile> SchemaFile::newFromDirectory(
    const kj::ReadableDirectory& baseDir, kj::Path path,
    kj::ArrayPtr<const kj::ReadableDirectory* const> importPath,
    kj::Maybe<kj::String> displayNameOverride) {
  return kj::heap<DiskSchemaFile>(baseDir, kj::mv(path), importPath, baseDir.openFile(path),
                                  kj::mv(displayNameOverride));
}

}  // namespace capnp
