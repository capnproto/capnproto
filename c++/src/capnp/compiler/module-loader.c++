// Copyright (c) 2013, Kenton Varda <temporal@gmail.com>
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this
//    list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
// ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "module-loader.h"
#include "lexer.h"
#include "parser.h"
#include <kj/vector.h>
#include <kj/mutex.h>
#include <kj/debug.h>
#include <kj/io.h>
#include <capnp/message.h>
#include <map>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

namespace capnp {
namespace compiler {

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

class MmapDisposer: public kj::ArrayDisposer {
protected:
  void disposeImpl(void* firstElement, size_t elementSize, size_t elementCount,
                   size_t capacity, void (*destroyElement)(void*)) const {
    munmap(firstElement, elementSize * elementCount);
  }
};

constexpr MmapDisposer mmapDisposer = MmapDisposer();

kj::Array<const char> mmapForRead(kj::StringPtr filename) {
  int fd;
  // We already established that the file exists, so this should not fail.
  KJ_SYSCALL(fd = open(filename.cStr(), O_RDONLY), filename);
  kj::AutoCloseFd closer(fd);

  struct stat stats;
  KJ_SYSCALL(fstat(fd, &stats));

  const void* mapping = mmap(NULL, stats.st_size, PROT_READ, MAP_SHARED, fd, 0);
  if (mapping == MAP_FAILED) {
    KJ_FAIL_SYSCALL("mmap", errno, filename);
  }

  return kj::Array<const char>(
      reinterpret_cast<const char*>(mapping), stats.st_size, mmapDisposer);
}

static char* canonicalizePath(char* path) {
  // Taken from some old C code of mine.

  // Preconditions:
  // - path has already been determined to be relative, perhaps because the pointer actually points
  //   into the middle of some larger path string, in which case it must point to the character
  //   immediately after a '/'.

  // Invariants:
  // - src points to the beginning of a path component.
  // - dst points to the location where the path component should end up, if it is not special.
  // - src == path or src[-1] == '/'.
  // - dst == path or dst[-1] == '/'.

  char* src = path;
  char* dst = path;
  char* locked = dst;  // dst cannot backtrack past this
  char* partEnd;
  bool hasMore;

  for (;;) {
    while (*src == '/') {
      // Skip duplicate slash.
      ++src;
    }

    partEnd = strchr(src, '/');
    hasMore = partEnd != NULL;
    if (hasMore) {
      *partEnd = '\0';
    } else {
      partEnd = src + strlen(src);
    }

    if (strcmp(src, ".") == 0) {
      // Skip it.
    } else if (strcmp(src, "..") == 0) {
      if (dst > locked) {
        // Backtrack over last path component.
        --dst;
        while (dst > locked && dst[-1] != '/') --dst;
      } else {
        locked += 3;
        goto copy;
      }
    } else {
      // Copy if needed.
    copy:
      if (dst < src) {
        memmove(dst, src, partEnd - src);
        dst += partEnd - src;
      } else {
        dst = partEnd;
      }
      *dst++ = '/';
    }

    if (hasMore) {
      src = partEnd + 1;
    } else {
      // Oops, we have to remove the trailing '/'.
      if (dst == path) {
        // Oops, there is no trailing '/'.  We have to return ".".
        strcpy(path, ".");
        return path + 1;
      } else {
        // Remove the trailing '/'.  Note that this means that opening the file will work even
        // if it is not a directory, where normally it should fail on non-directories when a
        // trailing '/' is present.  If this is a problem, we need to add some sort of special
        // handling for this case where we stat() it separately to check if it is a directory,
        // because Ekam findInput will not accept a trailing '/'.
        --dst;
        *dst = '\0';
        return dst;
      }
    }
  }
}

kj::String canonicalizePath(kj::StringPtr path) {
  KJ_STACK_ARRAY(char, result, path.size() + 1, 128, 512);
  strcpy(result.begin(), path.begin());
  char* end = canonicalizePath(result.begin());
  return kj::heapString(result.slice(0, end - result.begin()));
}

kj::String catPath(kj::StringPtr base, kj::StringPtr add) {
  if (add.size() > 0 && add[0] == '/') {
    return kj::heapString(add);
  }

  const char* pos = base.end();
  while (pos > base.begin() && pos[-1] != '/') {
    --pos;
  }

  return kj::str(base.slice(0, pos - base.begin()), add);
}

}  // namespace


class ModuleLoader::Impl {
public:
  Impl(const GlobalErrorReporter& errorReporter): errorReporter(errorReporter) {}

  void addImportPath(kj::String path) {
    searchPath.add(kj::heapString(kj::mv(path)));
  }

  kj::Maybe<const Module&> loadModule(kj::StringPtr localName, kj::StringPtr sourceName) const;
  kj::Maybe<const Module&> loadModuleFromSearchPath(kj::StringPtr sourceName) const;
  const GlobalErrorReporter& getErrorReporter() const { return errorReporter; }

private:
  const GlobalErrorReporter& errorReporter;
  kj::Vector<kj::String> searchPath;
  kj::MutexGuarded<std::map<kj::StringPtr, kj::Own<Module>>> modules;
};

class ModuleLoader::ModuleImpl: public Module {
public:
  ModuleImpl(const ModuleLoader::Impl& loader, kj::String localName, kj::String sourceName)
      : loader(loader), localName(kj::mv(localName)), sourceName(kj::mv(sourceName)) {}

  kj::StringPtr getLocalName() const override {
    return localName;
  }

  kj::StringPtr getSourceName() const override {
    return sourceName;
  }

  Orphan<ParsedFile> loadContent(Orphanage orphanage) const override {
    kj::Array<const char> content = mmapForRead(localName);

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
    auto statements = lexedBuilder.initRoot<LexedStatements>();
    lex(content, statements, *this);

    auto parsed = orphanage.newOrphan<ParsedFile>();
    parseFile(statements.getStatements(), parsed.get(), *this);
    return parsed;
  }

  kj::Maybe<const Module&> importRelative(kj::StringPtr importPath) const override {
    if (importPath.size() > 0 && importPath[0] == '/') {
      return loader.loadModuleFromSearchPath(importPath.slice(1));
    } else {
      return loader.loadModule(catPath(localName, importPath), catPath(sourceName, importPath));
    }
  }

  void addError(uint32_t startByte, uint32_t endByte, kj::StringPtr message) const override {
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

    loader.getErrorReporter().addError(
        localName,
        GlobalErrorReporter::SourcePos { startByte, startLine, startCol },
        GlobalErrorReporter::SourcePos { endByte, endLine, endCol },
        message);
  }

  bool hadErrors() const {
    return loader.getErrorReporter().hadErrors();
  }

private:
  const ModuleLoader::Impl& loader;
  kj::String localName;
  kj::String sourceName;

  kj::Lazy<kj::Vector<uint>> lineBreaks;
  // Byte offsets of the first byte in each source line.  The first element is always zero.
  // Initialized the first time the module is loaded.
};

// =======================================================================================

kj::Maybe<const Module&> ModuleLoader::Impl::loadModule(
    kj::StringPtr localName, kj::StringPtr sourceName) const {
  kj::String canonicalLocalName = canonicalizePath(localName);
  kj::String canonicalSourceName = canonicalizePath(sourceName);

  auto locked = modules.lockExclusive();

  auto iter = locked->find(canonicalLocalName);
  if (iter != locked->end()) {
    // Return existing file.
    return *iter->second;
  }

  if (access(canonicalLocalName.cStr(), F_OK) < 0) {
    // No such file.
    return nullptr;
  }

  auto module = kj::heap<ModuleImpl>(
      *this, kj::mv(canonicalLocalName), kj::mv(canonicalSourceName));
  auto& result = *module;
  locked->insert(std::make_pair(result.getLocalName(), kj::mv(module)));
  return result;
}

kj::Maybe<const Module&> ModuleLoader::Impl::loadModuleFromSearchPath(
    kj::StringPtr sourceName) const {
  for (auto& search: searchPath) {
    kj::String candidate = kj::str(search, "/", sourceName);
    char* end = canonicalizePath(candidate.begin() + (candidate[0] == '/'));

    KJ_IF_MAYBE(module, loadModule(
        kj::heapString(candidate.slice(0, end - candidate.begin())), sourceName)) {
      return *module;
    }
  }
  return nullptr;
}

// =======================================================================================

ModuleLoader::ModuleLoader(const GlobalErrorReporter& errorReporter)
    : impl(kj::heap<Impl>(errorReporter)) {}
ModuleLoader::~ModuleLoader() {}

void ModuleLoader::addImportPath(kj::String path) { impl->addImportPath(kj::mv(path)); }

kj::Maybe<const Module&> ModuleLoader::loadModule(
    kj::StringPtr localName, kj::StringPtr sourceName) const {
  return impl->loadModule(localName, sourceName);
}

}  // namespace compiler
}  // namespace capnp
