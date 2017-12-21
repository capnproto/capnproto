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
#include <map>

namespace capnp {
namespace compiler {

class ModuleLoader::Impl {
public:
  Impl(GlobalErrorReporter& errorReporter)
      : errorReporter(errorReporter) {}

  void addImportPath(kj::ReadableDirectory& dir) {
    searchPath.add(&dir);
  }

  kj::Maybe<Module&> loadModule(kj::ReadableDirectory& dir, kj::PathPtr path);
  kj::Maybe<Module&> loadModuleFromSearchPath(kj::PathPtr path);
  kj::Maybe<kj::Array<const byte>> readEmbed(kj::ReadableDirectory& dir, kj::PathPtr path);
  kj::Maybe<kj::Array<const byte>> readEmbedFromSearchPath(kj::PathPtr path);
  GlobalErrorReporter& getErrorReporter() { return errorReporter; }

private:
  GlobalErrorReporter& errorReporter;
  kj::Vector<kj::ReadableDirectory*> searchPath;
  std::map<std::pair<kj::ReadableDirectory*, kj::PathPtr>, kj::Own<Module>> modules;
};

class ModuleLoader::ModuleImpl final: public Module {
public:
  ModuleImpl(ModuleLoader::Impl& loader, kj::Own<kj::ReadableFile> file,
             kj::ReadableDirectory& sourceDir, kj::PathPtr path)
      : loader(loader), file(kj::mv(file)), sourceDir(sourceDir), path(path.clone()),
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
    parseFile(statements.getStatements(), parsed.get(), *this);
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

private:
  ModuleLoader::Impl& loader;
  kj::Own<kj::ReadableFile> file;
  kj::ReadableDirectory& sourceDir;
  kj::Path path;
  kj::String sourceNameStr;

  kj::SpaceFor<LineBreakTable> lineBreaksSpace;
  kj::Maybe<kj::Own<LineBreakTable>> lineBreaks;
};

// =======================================================================================

kj::Maybe<Module&> ModuleLoader::Impl::loadModule(
    kj::ReadableDirectory& dir, kj::PathPtr path) {
  auto iter = modules.find(std::make_pair(&dir, path));
  if (iter != modules.end()) {
    // Return existing file.
    return *iter->second;
  }

  KJ_IF_MAYBE(file, dir.tryOpenFile(path)) {
    auto module = kj::heap<ModuleImpl>(*this, kj::mv(*file), dir, path);
    auto& result = *module;
    modules.insert(std::make_pair(std::make_pair(&dir, result.getPath()), kj::mv(module)));
    return result;
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
    kj::ReadableDirectory& dir, kj::PathPtr path) {
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

void ModuleLoader::addImportPath(kj::ReadableDirectory& dir) {
  impl->addImportPath(dir);
}

kj::Maybe<Module&> ModuleLoader::loadModule(kj::ReadableDirectory& dir, kj::PathPtr path) {
  return impl->loadModule(dir, path);
}

}  // namespace compiler
}  // namespace capnp
