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
#include <kj/miniposix.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#if _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

namespace capnp {
namespace compiler {

namespace {

class MmapDisposer: public kj::ArrayDisposer {
protected:
  void disposeImpl(void* firstElement, size_t elementSize, size_t elementCount,
                   size_t capacity, void (*destroyElement)(void*)) const {
#if _WIN32
    KJ_ASSERT(UnmapViewOfFile(firstElement));
#else
    munmap(firstElement, elementSize * elementCount);
#endif
  }
};

KJ_CONSTEXPR(static const) MmapDisposer mmapDisposer = MmapDisposer();

kj::Array<const byte> mmapForRead(kj::StringPtr filename) {
  int fd;
  // We already established that the file exists, so this should not fail.
  KJ_SYSCALL(fd = open(filename.cStr(), O_RDONLY), filename);
  kj::AutoCloseFd closer(fd);

  struct stat stats;
  KJ_SYSCALL(fstat(fd, &stats));

  if (S_ISREG(stats.st_mode)) {
    if (stats.st_size == 0) {
      // mmap()ing zero bytes will fail.
      return nullptr;
    }

    // Regular file.  Just mmap() it.
#if _WIN32
    HANDLE handle = reinterpret_cast<HANDLE>(_get_osfhandle(fd));
    KJ_ASSERT(handle != INVALID_HANDLE_VALUE);
    HANDLE mappingHandle = CreateFileMapping(
        handle, NULL, PAGE_READONLY, 0, stats.st_size, NULL);
    KJ_ASSERT(mappingHandle != INVALID_HANDLE_VALUE);
    KJ_DEFER(KJ_ASSERT(CloseHandle(mappingHandle)));
    const void* mapping = MapViewOfFile(mappingHandle, FILE_MAP_READ, 0, 0, stats.st_size);
#else  // _WIN32
    const void* mapping = mmap(NULL, stats.st_size, PROT_READ, MAP_SHARED, fd, 0);
    if (mapping == MAP_FAILED) {
      KJ_FAIL_SYSCALL("mmap", errno, filename);
    }
#endif  // _WIN32, else

    return kj::Array<const byte>(
        reinterpret_cast<const byte*>(mapping), stats.st_size, mmapDisposer);
  } else {
    // This could be a stream of some sort, like a pipe.  Fall back to read().
    // TODO(cleanup):  This does a lot of copies.  Not sure I care.
    kj::Vector<byte> data(8192);

    byte buffer[4096];
    for (;;) {
      kj::miniposix::ssize_t n;
      KJ_SYSCALL(n = read(fd, buffer, sizeof(buffer)));
      if (n == 0) break;
      data.addAll(buffer, buffer + n);
    }

    return data.releaseAsArray();
  }
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

  char* start = path.startsWith("/") ? result.begin() + 1 : result.begin();
  char* end = canonicalizePath(start);
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
  Impl(GlobalErrorReporter& errorReporter): errorReporter(errorReporter) {}

  void addImportPath(kj::String path) {
    searchPath.add(kj::heapString(kj::mv(path)));
  }

  kj::Maybe<Module&> loadModule(kj::StringPtr localName, kj::StringPtr sourceName);
  kj::Maybe<Module&> loadModuleFromSearchPath(kj::StringPtr sourceName);
  kj::Maybe<kj::Array<const byte>> readEmbed(kj::StringPtr localName, kj::StringPtr sourceName);
  kj::Maybe<kj::Array<const byte>> readEmbedFromSearchPath(kj::StringPtr sourceName);
  GlobalErrorReporter& getErrorReporter() { return errorReporter; }

private:
  GlobalErrorReporter& errorReporter;
  kj::Vector<kj::String> searchPath;
  std::map<kj::StringPtr, kj::Own<Module>> modules;
};

class ModuleLoader::ModuleImpl final: public Module {
public:
  ModuleImpl(ModuleLoader::Impl& loader, kj::String localName, kj::String sourceName)
      : loader(loader), localName(kj::mv(localName)), sourceName(kj::mv(sourceName)) {}

  kj::StringPtr getLocalName() {
    return localName;
  }

  kj::StringPtr getSourceName() override {
    return sourceName;
  }

  Orphan<ParsedFile> loadContent(Orphanage orphanage) override {
    kj::Array<const char> content = mmapForRead(localName).releaseAsChars();

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
      return loader.loadModuleFromSearchPath(importPath.slice(1));
    } else {
      return loader.loadModule(catPath(localName, importPath), catPath(sourceName, importPath));
    }
  }

  kj::Maybe<kj::Array<const byte>> embedRelative(kj::StringPtr embedPath) override {
    if (embedPath.size() > 0 && embedPath[0] == '/') {
      return loader.readEmbedFromSearchPath(embedPath.slice(1));
    } else {
      return loader.readEmbed(catPath(localName, embedPath), catPath(sourceName, embedPath));
    }
  }

  void addError(uint32_t startByte, uint32_t endByte, kj::StringPtr message) override {
    auto& lines = *KJ_REQUIRE_NONNULL(lineBreaks,
        "Can't report errors until loadContent() is called.");

    loader.getErrorReporter().addError(
        localName, lines.toSourcePos(startByte), lines.toSourcePos(endByte), message);
  }

  bool hadErrors() override {
    return loader.getErrorReporter().hadErrors();
  }

private:
  ModuleLoader::Impl& loader;
  kj::String localName;
  kj::String sourceName;

  kj::SpaceFor<LineBreakTable> lineBreaksSpace;
  kj::Maybe<kj::Own<LineBreakTable>> lineBreaks;
};

// =======================================================================================

kj::Maybe<Module&> ModuleLoader::Impl::loadModule(
    kj::StringPtr localName, kj::StringPtr sourceName) {
  kj::String canonicalLocalName = canonicalizePath(localName);
  kj::String canonicalSourceName = canonicalizePath(sourceName);

  auto iter = modules.find(canonicalLocalName);
  if (iter != modules.end()) {
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
  modules.insert(std::make_pair(result.getLocalName(), kj::mv(module)));
  return result;
}

kj::Maybe<Module&> ModuleLoader::Impl::loadModuleFromSearchPath(kj::StringPtr sourceName) {
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

kj::Maybe<kj::Array<const byte>> ModuleLoader::Impl::readEmbed(
    kj::StringPtr localName, kj::StringPtr sourceName) {
  kj::String canonicalLocalName = canonicalizePath(localName);
  kj::String canonicalSourceName = canonicalizePath(sourceName);

  if (access(canonicalLocalName.cStr(), F_OK) < 0) {
    // No such file.
    return nullptr;
  }

  return mmapForRead(localName);
}

kj::Maybe<kj::Array<const byte>> ModuleLoader::Impl::readEmbedFromSearchPath(
    kj::StringPtr sourceName) {
  for (auto& search: searchPath) {
    kj::String candidate = kj::str(search, "/", sourceName);
    char* end = canonicalizePath(candidate.begin() + (candidate[0] == '/'));

    KJ_IF_MAYBE(module, readEmbed(
        kj::heapString(candidate.slice(0, end - candidate.begin())), sourceName)) {
      return kj::mv(*module);
    }
  }
  return nullptr;
}

// =======================================================================================

ModuleLoader::ModuleLoader(GlobalErrorReporter& errorReporter)
    : impl(kj::heap<Impl>(errorReporter)) {}
ModuleLoader::~ModuleLoader() noexcept(false) {}

void ModuleLoader::addImportPath(kj::String path) { impl->addImportPath(kj::mv(path)); }

kj::Maybe<Module&> ModuleLoader::loadModule(kj::StringPtr localName, kj::StringPtr sourceName) {
  return impl->loadModule(localName, sourceName);
}

}  // namespace compiler
}  // namespace capnp
