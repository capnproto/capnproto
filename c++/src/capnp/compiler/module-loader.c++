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

#include "module-loader.h"
#include "lexer.h"
#include "parser.h"
#include <kj/vector.h>
#include <kj/mutex.h>
#include <kj/debug.h>
#include <kj/io.h>
#include <capnp/message.h>
#include <unordered_map>

namespace capnp {
namespace compiler {

namespace {

struct FileKey {
  // Key type for the modules map. We need to implement some complicated heuristics to detect when
  // two files are actually the same underlying file on disk, in order to handle the case where
  // people have mapped the same file into multiple locations in the import tree, whether by
  // passing overlapping import paths, weird symlinks, or whatever.
  //
  // This is probably over-engineered.

  const kj::ReadableDirectory& baseDir;
  kj::PathPtr path;
  kj::Maybe<const kj::ReadableFile&> file;
  uint64_t hashCode;
  uint64_t size;
  kj::Date lastModified;

  FileKey(const kj::ReadableDirectory& baseDir, kj::PathPtr path)
      : baseDir(baseDir), path(path), file(nullptr),
        hashCode(0), size(0), lastModified(kj::UNIX_EPOCH) {}
  FileKey(const kj::ReadableDirectory& baseDir, kj::PathPtr path, const kj::ReadableFile& file)
      : FileKey(baseDir, path, file, file.stat()) {}

  FileKey(const kj::ReadableDirectory& baseDir, kj::PathPtr path, const kj::ReadableFile& file,
          kj::FsNode::Metadata meta)
      : baseDir(baseDir), path(path), file(&file),
        hashCode(meta.hashCode), size(meta.size), lastModified(meta.lastModified) {}

  bool operator==(const FileKey& other) const {
    // Allow matching on baseDir and path without a file.
    if (&baseDir == &other.baseDir && path == other.path) return true;
    if (file == nullptr || other.file == nullptr) return false;

    // Try comparing various file metadata to rule out obvious differences.
    if (hashCode != other.hashCode) return false;
    if (size != other.size || lastModified != other.lastModified) return false;
    if (path.size() > 0 && other.path.size() > 0 &&
        path[path.size() - 1] != other.path[other.path.size() - 1]) {
      // Names differ, so probably not the same file.
      return false;
    }

    // Same file hash, but different paths, but same size and modification date. This could be a
    // case of two different import paths overlapping and containing the same file. We'll need to
    // check the content.
    auto mapping1 = KJ_ASSERT_NONNULL(file).mmap(0, size);
    auto mapping2 = KJ_ASSERT_NONNULL(other.file).mmap(0, size);
    if (memcmp(mapping1.begin(), mapping2.begin(), size) != 0) return false;

    if (path == other.path) {
      // Exactly the same content was mapped at exactly the same path relative to two different
      // import directories. This can only really happen if this was one of the files passed on
      // the command line, but its --src-prefix is not also an import path, but some other
      // directory containing the same file was given as an import path. Whatever, we'll ignore
      // this.
      return true;
    }

    // Exactly the same content!
    static bool warned = false;
    if (!warned) {
      KJ_LOG(WARNING,
          "Found exactly the same source file mapped at two different paths. This suggests "
          "that your -I and --src-prefix flags are overlapping or inconsistent. Remember, these "
          "flags should only specify directories that are logical 'roots' of the source tree. "
          "It should never be the case that one of the import directories contains another one of "
          "them.",
          path, other.path);
      warned = true;
    }

    return true;
  }
};

struct FileKeyHash {
  size_t operator()(const FileKey& key) const {
    if (sizeof(size_t) < sizeof(key.hashCode)) {
      // 32-bit system, do more mixing
      return (key.hashCode >> 32) * 31 + static_cast<size_t>(key.hashCode) +
          key.size * 103 + (key.lastModified - kj::UNIX_EPOCH) / kj::MILLISECONDS * 73;
    } else {
      return key.hashCode + key.size * 103 +
          (key.lastModified - kj::UNIX_EPOCH) / kj::NANOSECONDS * 73ull;
    }
  }
};

};

class ModuleLoader::Impl {
public:
  Impl(GlobalErrorReporter& errorReporter)
      : errorReporter(errorReporter) {}

  void addImportPath(const kj::ReadableDirectory& dir) {
    searchPath.add(&dir);
  }

  kj::Maybe<Module&> loadModule(const kj::ReadableDirectory& dir, kj::PathPtr path);
  kj::Maybe<Module&> loadModuleFromSearchPath(kj::PathPtr path);
  kj::Maybe<kj::Array<const byte>> readEmbed(const kj::ReadableDirectory& dir, kj::PathPtr path);
  kj::Maybe<kj::Array<const byte>> readEmbedFromSearchPath(kj::PathPtr path);
  GlobalErrorReporter& getErrorReporter() { return errorReporter; }

  void setFileIdsRequired(bool value) { fileIdsRequired = value; }
  bool areFileIdsRequired() { return fileIdsRequired; }

private:
  GlobalErrorReporter& errorReporter;
  kj::Vector<const kj::ReadableDirectory*> searchPath;
  std::unordered_map<FileKey, kj::Own<Module>, FileKeyHash> modules;
  bool fileIdsRequired = true;
};

class ModuleLoader::ModuleImpl final: public Module {
public:
  ModuleImpl(ModuleLoader::Impl& loader, kj::Own<const kj::ReadableFile> file,
             const kj::ReadableDirectory& sourceDir, kj::Path pathParam)
      : loader(loader), file(kj::mv(file)), sourceDir(sourceDir), path(kj::mv(pathParam)),
        sourceNameStr(path.toString()) {
    KJ_REQUIRE(path.size() > 0);
  }

  kj::PathPtr getPath() {
    return path;
  }

  kj::StringPtr getSourceName() override {
    return sourceNameStr;
  }

  Orphan<ParsedFile> loadContent(Orphanage orphanage) override {
    kj::Array<const char> content = file->mmap(0, file->stat().size).releaseAsChars();

    lineBreaks = nullptr;  // In case loadContent() is called multiple times.
    lineBreaks = lineBreaksSpace.construct(content);

    MallocMessageBuilder lexedBuilder;
    auto statements = lexedBuilder.initRoot<LexedStatements>();
    lex(content, statements, *this);

    auto parsed = orphanage.newOrphan<ParsedFile>();
    parseFile(statements.getStatements(), parsed.get(), *this, loader.areFileIdsRequired());
    return parsed;
  }

  kj::Maybe<Module&> importRelative(kj::StringPtr importPath) override {
    if (importPath.size() > 0 && importPath[0] == '/') {
      return loader.loadModuleFromSearchPath(kj::Path::parse(importPath.slice(1)));
    } else {
      return loader.loadModule(sourceDir, path.parent().eval(importPath));
    }
  }

  kj::Maybe<kj::Array<const byte>> embedRelative(kj::StringPtr embedPath) override {
    if (embedPath.size() > 0 && embedPath[0] == '/') {
      return loader.readEmbedFromSearchPath(kj::Path::parse(embedPath.slice(1)));
    } else {
      return loader.readEmbed(sourceDir, path.parent().eval(embedPath));
    }
  }

  void addError(uint32_t startByte, uint32_t endByte, kj::StringPtr message) override {
    auto& lines = *KJ_REQUIRE_NONNULL(lineBreaks,
        "Can't report errors until loadContent() is called.");

    loader.getErrorReporter().addError(sourceDir, path,
        lines.toSourcePos(startByte), lines.toSourcePos(endByte), message);
  }

  bool hadErrors() override {
    return loader.getErrorReporter().hadErrors();
  }

  void reportResolution(Resolution resolution) override{
    resolutions.add(resolution);
  }

  kj::ArrayPtr<const Resolution> getResolutions() override {
    return resolutions.asPtr();
  }

private:
  ModuleLoader::Impl& loader;
  kj::Own<const kj::ReadableFile> file;
  const kj::ReadableDirectory& sourceDir;
  kj::Path path;
  kj::String sourceNameStr;

  kj::SpaceFor<LineBreakTable> lineBreaksSpace;
  kj::Maybe<kj::Own<LineBreakTable>> lineBreaks;
  kj::Vector<Resolution> resolutions;
};

// =======================================================================================

kj::Maybe<Module&> ModuleLoader::Impl::loadModule(
    const kj::ReadableDirectory& dir, kj::PathPtr path) {
  auto iter = modules.find(FileKey(dir, path));
  if (iter != modules.end()) {
    // Return existing file.
    return *iter->second;
  }

  KJ_IF_MAYBE(file, dir.tryOpenFile(path)) {
    auto pathCopy = path.clone();
    auto key = FileKey(dir, pathCopy, **file);
    auto module = kj::heap<ModuleImpl>(*this, kj::mv(*file), dir, kj::mv(pathCopy));
    auto& result = *module;
    auto insertResult = modules.insert(std::make_pair(key, kj::mv(module)));
    if (insertResult.second) {
      return result;
    } else {
      // Now that we have the file open, we noticed a collision. Return the old file.
      return *insertResult.first->second;
    }
  } else {
    // No such file.
    return nullptr;
  }
}

kj::Maybe<Module&> ModuleLoader::Impl::loadModuleFromSearchPath(kj::PathPtr path) {
  for (auto candidate: searchPath) {
    KJ_IF_MAYBE(module, loadModule(*candidate, path)) {
      return *module;
    }
  }
  return nullptr;
}

kj::Maybe<kj::Array<const byte>> ModuleLoader::Impl::readEmbed(
    const kj::ReadableDirectory& dir, kj::PathPtr path) {
  KJ_IF_MAYBE(file, dir.tryOpenFile(path)) {
    return file->get()->mmap(0, file->get()->stat().size);
  }
  return nullptr;
}

kj::Maybe<kj::Array<const byte>> ModuleLoader::Impl::readEmbedFromSearchPath(kj::PathPtr path) {
  for (auto candidate: searchPath) {
    KJ_IF_MAYBE(module, readEmbed(*candidate, path)) {
      return kj::mv(*module);
    }
  }
  return nullptr;
}

// =======================================================================================

ModuleLoader::ModuleLoader(GlobalErrorReporter& errorReporter)
    : impl(kj::heap<Impl>(errorReporter)) {}
ModuleLoader::~ModuleLoader() noexcept(false) {}

void ModuleLoader::addImportPath(const kj::ReadableDirectory& dir) {
  impl->addImportPath(dir);
}

kj::Maybe<Module&> ModuleLoader::loadModule(const kj::ReadableDirectory& dir, kj::PathPtr path) {
  return impl->loadModule(dir, path);
}

void ModuleLoader::setFileIdsRequired(bool value) {
  return impl->setFileIdsRequired(value);
}

}  // namespace compiler
}  // namespace capnp
