// Copyright (c) 2015 Sandstorm Development Group, Inc. and contributors
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

#include "filesystem.h"
#include "vector.h"
#include "debug.h"
#include "one-of.h"
#include <map>

namespace kj {

Path::Path(StringPtr name): Path(heapString(name)) {}
Path::Path(String&& name): parts(heapArray<String>(1)) {
  parts[0] = kj::mv(name);
  validatePart(parts[0]);
}

Path::Path(ArrayPtr<const StringPtr> parts)
    : Path(KJ_MAP(p, parts) { return heapString(p); }) {}
Path::Path(Array<String> partsParam)
    : Path(kj::mv(partsParam), ALREADY_CHECKED) {
  for (auto& p: parts) {
    validatePart(p);
  }
}

Path PathPtr::clone() {
  return Path(KJ_MAP(p, parts) { return heapString(p); }, Path::ALREADY_CHECKED);
}

Path Path::parse(StringPtr path) {
  KJ_REQUIRE(!path.startsWith("/"), "expected a relative path, got absolute", path) {
    // When exceptions are disabled, go on -- the leading '/' will end up ignored.
    break;
  }
  return evalImpl(Vector<String>(countParts(path)), path);
}

Path PathPtr::append(Path suffix) const {
  auto newParts = kj::heapArrayBuilder<String>(parts.size() + suffix.parts.size());
  for (auto& p: parts) newParts.add(heapString(p));
  for (auto& p: suffix.parts) newParts.add(kj::mv(p));
  return Path(newParts.finish(), Path::ALREADY_CHECKED);
}
Path Path::append(Path suffix) && {
  auto newParts = kj::heapArrayBuilder<String>(parts.size() + suffix.parts.size());
  for (auto& p: parts) newParts.add(kj::mv(p));
  for (auto& p: suffix.parts) newParts.add(kj::mv(p));
  return Path(newParts.finish(), ALREADY_CHECKED);
}
Path PathPtr::append(PathPtr suffix) const {
  auto newParts = kj::heapArrayBuilder<String>(parts.size() + suffix.parts.size());
  for (auto& p: parts) newParts.add(heapString(p));
  for (auto& p: suffix.parts) newParts.add(heapString(p));
  return Path(newParts.finish(), Path::ALREADY_CHECKED);
}
Path Path::append(PathPtr suffix) && {
  auto newParts = kj::heapArrayBuilder<String>(parts.size() + suffix.parts.size());
  for (auto& p: parts) newParts.add(kj::mv(p));
  for (auto& p: suffix.parts) newParts.add(heapString(p));
  return Path(newParts.finish(), ALREADY_CHECKED);
}

Path PathPtr::eval(StringPtr pathText) const {
  if (pathText.startsWith("/")) {
    // Optimization: avoid copying parts that will just be dropped.
    return Path::evalImpl(Vector<String>(Path::countParts(pathText)), pathText);
  } else {
    Vector<String> newParts(parts.size() + Path::countParts(pathText));
    for (auto& p: parts) newParts.add(heapString(p));
    return Path::evalImpl(kj::mv(newParts), pathText);
  }
}
Path Path::eval(StringPtr pathText) && {
  if (pathText.startsWith("/")) {
    // Optimization: avoid copying parts that will just be dropped.
    return evalImpl(Vector<String>(countParts(pathText)), pathText);
  } else {
    Vector<String> newParts(parts.size() + countParts(pathText));
    for (auto& p: parts) newParts.add(kj::mv(p));
    return evalImpl(kj::mv(newParts), pathText);
  }
}

PathPtr PathPtr::basename() const {
  KJ_REQUIRE(parts.size() > 0, "root path has no basename");
  return PathPtr(parts.slice(parts.size() - 1, parts.size()));
}
Path Path::basename() && {
  KJ_REQUIRE(parts.size() > 0, "root path has no basename");
  auto newParts = kj::heapArrayBuilder<String>(1);
  newParts.add(kj::mv(parts[parts.size() - 1]));
  return Path(newParts.finish(), ALREADY_CHECKED);
}

PathPtr PathPtr::parent() const {
  KJ_REQUIRE(parts.size() > 0, "root path has no parent");
  return PathPtr(parts.slice(0, parts.size() - 1));
}
Path Path::parent() && {
  KJ_REQUIRE(parts.size() > 0, "root path has no parent");
  return Path(KJ_MAP(p, parts.slice(0, parts.size() - 1)) { return kj::mv(p); }, ALREADY_CHECKED);
}

String PathPtr::toString(bool absolute) const {
  if (parts.size() == 0) {
    // Special-case empty path.
    return absolute ? kj::str("/") : kj::str(".");
  }

  size_t size = absolute + (parts.size() - 1);
  for (auto& p: parts) size += p.size();

  String result = kj::heapString(size);

  char* ptr = result.begin();
  bool leadingSlash = absolute;
  for (auto& p: parts) {
    if (leadingSlash) *ptr++ = '/';
    leadingSlash = true;
    memcpy(ptr, p.begin(), p.size());
    ptr += p.size();
  }
  KJ_ASSERT(ptr == result.end());

  return result;
}

Path Path::slice(size_t start, size_t end) && {
  return Path(KJ_MAP(p, parts.slice(start, end)) { return kj::mv(p); });
}

Path PathPtr::evalWin32(StringPtr pathText) const {
  Vector<String> newParts(parts.size() + Path::countPartsWin32(pathText));
  for (auto& p: parts) newParts.add(heapString(p));
  return Path::evalWin32Impl(kj::mv(newParts), pathText);
}
Path Path::evalWin32(StringPtr pathText) && {
  Vector<String> newParts(parts.size() + countPartsWin32(pathText));
  for (auto& p: parts) newParts.add(kj::mv(p));
  return evalWin32Impl(kj::mv(newParts), pathText);
}

String PathPtr::toWin32String(bool absolute) const {
  if (parts.size() == 0) {
    // Special-case empty path.
    KJ_REQUIRE(!absolute, "absolute path is missing disk designator") {
      break;
    }
    return absolute ? kj::str("\\\\") : kj::str(".");
  }

  bool isUncPath = false;
  if (absolute) {
    if (Path::isWin32Drive(parts[0])) {
      // It's a win32 drive
    } else if (Path::isNetbiosName(parts[0])) {
      isUncPath = true;
    } else {
      KJ_FAIL_REQUIRE("absolute win32 path must start with drive letter or netbios host name",
                      parts[0]);
    }
  }

  size_t size = (isUncPath ? 2 : 0) + (parts.size() - 1);
  for (auto& p: parts) size += p.size();

  String result = heapString(size);

  char* ptr = result.begin();

  if (isUncPath) {
    *ptr++ = '\\';
    *ptr++ = '\\';
  }

  bool leadingSlash = false;
  for (auto& p: parts) {
    if (leadingSlash) *ptr++ = '\\';
    leadingSlash = true;

    KJ_REQUIRE(!Path::isWin32Special(p), "path cannot contain DOS reserved name", p) {
      // Recover by blotting out the name with invalid characters which Win32 syscalls will reject.
      for (size_t i = 0; i < p.size(); i++) {
        *ptr++ = '|';
      }
      continue;
    }

    memcpy(ptr, p.begin(), p.size());
    ptr += p.size();
  }

  KJ_ASSERT(ptr == result.end());

  // Check for colons (other than in drive letter), which on NTFS would be interpreted as an
  // "alternate data stream", which can lead to surprising results. If we want to support ADS, we
  // should do so using an explicit API. Note that this check also prevents a relative path from
  // appearing to start with a drive letter.
  for (size_t i: kj::indices(result)) {
    if (result[i] == ':') {
      if (absolute && i == 1) {
        // False alarm: this is the drive letter.
      } else {
        KJ_FAIL_REQUIRE(
            "colons are prohibited in win32 paths to avoid triggering alterante data streams",
            result) {
          // Recover by using a different character which we know Win32 syscalls will reject.
          result[i] = '|';
        }
      }
    }
  }

  return result;
}

// -----------------------------------------------------------------------------

String Path::stripNul(String input) {
  kj::Vector<char> output(input.size());
  for (char c: input) {
    if (c != '\0') output.add(c);
  }
  output.add('\0');
  return String(output.releaseAsArray());
}

void Path::validatePart(StringPtr part) {
  KJ_REQUIRE(part != "" && part != "." && part != "..", "invalid path component", part);
  KJ_REQUIRE(strlen(part.begin()) == part.size(), "NUL character in path component", part);
  KJ_REQUIRE(part.findFirst('/') == nullptr,
      "'/' character in path component; did you mean to use Path::parse()?", part);
}

void Path::evalPart(Vector<String>& parts, ArrayPtr<const char> part) {
  if (part.size() == 0) {
    // Ignore consecutive or trailing '/'s.
  } else if (part.size() == 1 && part[0] == '.') {
    // Refers to current directory; ignore.
  } else if (part.size() == 2 && part[0] == '.' && part [1] == '.') {
    KJ_REQUIRE(parts.size() > 0, "can't use \"..\" to break out of starting directory") {
      // When exceptions are disabled, ignore.
      return;
    }
    parts.removeLast();
  } else {
    auto str = heapString(part);
    KJ_REQUIRE(strlen(str.begin()) == str.size(), "NUL character in path component", str) {
      // When exceptions are disabled, strip out '\0' chars.
      str = stripNul(kj::mv(str));
      break;
    }
    parts.add(kj::mv(str));
  }
}

Path Path::evalImpl(Vector<String>&& parts, StringPtr path) {
  if (path.startsWith("/")) {
    parts.clear();
  }

  size_t partStart = 0;
  for (auto i: kj::indices(path)) {
    if (path[i] == '/') {
      evalPart(parts, path.slice(partStart, i));
      partStart = i + 1;
    }
  }
  evalPart(parts, path.slice(partStart));

  return Path(parts.releaseAsArray(), Path::ALREADY_CHECKED);
}

Path Path::evalWin32Impl(Vector<String>&& parts, StringPtr path) {
  // Convert all forward slashes to backslashes.
  String ownPath;
  if (path.findFirst('/') != nullptr) {
    ownPath = heapString(path);
    for (char& c: ownPath) {
      if (c == '/') c = '\\';
    }
    path = ownPath;
  }

  // Interpret various forms of absolute paths.
  if (path.startsWith("\\\\")) {
    // UNC path.
    path = path.slice(2);

    // This path is absolute. The first component is a server name.
    parts.clear();
  } else if (path.startsWith("\\")) {
    // Path is relative to the current drive / network share.
    if (parts.size() >= 1 && isWin32Drive(parts[0])) {
      // Leading \ interpreted as root of current drive.
      parts.truncate(1);
    } else if (parts.size() >= 2) {
      // Leading \ interpreted as root of current network share (which is indicated by the first
      // *two* components of the path).
      parts.truncate(2);
    } else {
      KJ_FAIL_REQUIRE("must specify drive letter", path) {
        // Recover by assuming C drive.
        parts.clear();
        parts.add(kj::str("c:"));
        break;
      }
    }
  } else if ((path.size() == 2 || (path.size() > 2 && path[2] == '\\')) &&
             isWin32Drive(path.slice(0, 2))) {
    // Starts with a drive letter.
    parts.clear();
  }

  size_t partStart = 0;
  for (auto i: kj::indices(path)) {
    if (path[i] == '\\') {
      evalPart(parts, path.slice(partStart, i));
      partStart = i + 1;
    }
  }
  evalPart(parts, path.slice(partStart));

  return Path(parts.releaseAsArray(), Path::ALREADY_CHECKED);
}

size_t Path::countParts(StringPtr path) {
  size_t result = 1;
  for (char c: path) {
    result += (c == '/');
  }
  return result;
}

size_t Path::countPartsWin32(StringPtr path) {
  size_t result = 1;
  for (char c: path) {
    result += (c == '/' || c == '\\');
  }
  return result;
}

bool Path::isWin32Drive(ArrayPtr<const char> part) {
  return part.size() == 2 && part[1] == ':' &&
         (('a' <= part[0] && part[0] <= 'z') || ('A' <= part[0] && part[0] <= 'Z'));
}

bool Path::isNetbiosName(ArrayPtr<const char> part) {
  // Characters must be alphanumeric or '.' or '-'.
  for (char c: part) {
    if (c != '.' && c != '-' &&
        (c < 'a' || 'z' < c) &&
        (c < 'A' || 'Z' < c) &&
        (c < '0' || '9' < c)) {
      return false;
    }
  }

  // Can't be empty nor start or end with a '.' or a '-'.
  return part.size() > 0 &&
      part[0] != '.' && part[0] != '-' &&
      part[part.size() - 1] != '.' && part[part.size() - 1] != '-';
}

bool Path::isWin32Special(StringPtr part) {
  bool isNumbered;
  if (part.size() == 3 || (part.size() > 3 && part[3] == '.')) {
    // Filename is three characters or three characters followed by an extension.
    isNumbered = false;
  } else if ((part.size() == 4 || (part.size() > 4 && part[4] == '.')) &&
             '1' <= part[3] && part[3] <= '9') {
    // Filename is four characters or four characters followed by an extension, and the fourth
    // character is a nonzero digit.
    isNumbered = true;
  } else {
    return false;
  }

  // OK, this could be a Win32 special filename. We need to match the first three letters against
  // the list of specials, case-insensitively.
  char tmp[4];
  memcpy(tmp, part.begin(), 3);
  tmp[3] = '\0';
  for (char& c: tmp) {
    if ('A' <= c && c <= 'Z') {
      c += 'a' - 'A';
    }
  }

  StringPtr str(tmp, 3);
  if (isNumbered) {
    // Specials that are followed by a digit.
    return str == "com" || str == "lpt";
  } else {
    // Specials that are not followed by a digit.
    return str == "con" || str == "prn" || str == "aux" || str == "nul";
  }
}

// =======================================================================================

String ReadableFile::readAllText() {
  String result = heapString(stat().size);
  size_t n = read(0, result.asBytes());
  if (n < result.size()) {
    // Apparently file was truncated concurrently. Reduce to new size to match.
    result = heapString(result.slice(0, n));
  }
  return result;
}

Array<byte> ReadableFile::readAllBytes() {
  Array<byte> result = heapArray<byte>(stat().size);
  size_t n = read(0, result.asBytes());
  if (n < result.size()) {
    // Apparently file was truncated concurrently. Reduce to new size to match.
    result = heapArray(result.slice(0, n));
  }
  return result;
}

void File::writeAll(ArrayPtr<const byte> bytes) {
  truncate(0);
  write(0, bytes);
}

void File::writeAll(StringPtr text) {
  writeAll(text.asBytes());
}

size_t File::copy(uint64_t offset, ReadableFile& from, uint64_t fromOffset, uint64_t size) {
  byte buffer[8192];

  size_t result = 0;
  while (size > 0) {
    size_t n = from.read(fromOffset, kj::arrayPtr(buffer, kj::min(sizeof(buffer), size)));
    write(offset, arrayPtr(buffer, n));
    result += n;
    if (n < sizeof(buffer)) {
      // Either we copied the amount requested or we hit EOF.
      break;
    }
    fromOffset += n;
    offset += n;
    size -= n;
  }

  return result;
}

FsNode::Metadata ReadableDirectory::lstat(PathPtr path) {
  KJ_IF_MAYBE(meta, tryLstat(path)) {
    return *meta;
  } else {
    KJ_FAIL_REQUIRE("no such file", path) { break; }
    return FsNode::Metadata();
  }
}

Own<ReadableFile> ReadableDirectory::openFile(PathPtr path) {
  KJ_IF_MAYBE(file, tryOpenFile(path)) {
    return kj::mv(*file);
  } else {
    KJ_FAIL_REQUIRE("no such directory", path) { break; }
    return newInMemoryFile(nullClock());
  }
}

Own<ReadableDirectory> ReadableDirectory::openSubdir(PathPtr path) {
  KJ_IF_MAYBE(dir, tryOpenSubdir(path)) {
    return kj::mv(*dir);
  } else {
    KJ_FAIL_REQUIRE("no such file or directory", path) { break; }
    return newInMemoryDirectory(nullClock());
  }
}

String ReadableDirectory::readlink(PathPtr path) {
  KJ_IF_MAYBE(p, tryReadlink(path)) {
    return kj::mv(*p);
  } else {
    KJ_FAIL_REQUIRE("not a symlink", path) { break; }
    return kj::str(".");
  }
}

Own<File> Directory::openFile(PathPtr path, WriteMode mode) {
  KJ_IF_MAYBE(f, tryOpenFile(path, mode)) {
    return kj::mv(*f);
  } else if (has(mode, WriteMode::CREATE) && !has(mode, WriteMode::MODIFY)) {
    KJ_FAIL_REQUIRE("file already exists", path) { break; }
  } else if (has(mode, WriteMode::MODIFY) && !has(mode, WriteMode::CREATE)) {
    KJ_FAIL_REQUIRE("file does not exist", path) { break; }
  } else if (!has(mode, WriteMode::MODIFY) && !has(mode, WriteMode::CREATE)) {
    KJ_FAIL_ASSERT("neither WriteMode::CREATE nor WriteMode::MODIFY was given", path) { break; }
  } else {
    // Shouldn't happen.
    KJ_FAIL_ASSERT("tryOpenFile() returned null despite no preconditions", path) { break; }
  }
  return newInMemoryFile(nullClock());
}

Own<AppendableFile> Directory::appendFile(PathPtr path, WriteMode mode) {
  KJ_IF_MAYBE(f, tryAppendFile(path, mode)) {
    return kj::mv(*f);
  } else if (has(mode, WriteMode::CREATE) && !has(mode, WriteMode::MODIFY)) {
    KJ_FAIL_REQUIRE("file already exists", path) { break; }
  } else if (has(mode, WriteMode::MODIFY) && !has(mode, WriteMode::CREATE)) {
    KJ_FAIL_REQUIRE("file does not exist", path) { break; }
  } else if (!has(mode, WriteMode::MODIFY) && !has(mode, WriteMode::CREATE)) {
    KJ_FAIL_ASSERT("neither WriteMode::CREATE nor WriteMode::MODIFY was given", path) { break; }
  } else {
    // Shouldn't happen.
    KJ_FAIL_ASSERT("tryAppendFile() returned null despite no preconditions", path) { break; }
  }
  return newFileAppender(newInMemoryFile(nullClock()));
}

Own<Directory> Directory::openSubdir(PathPtr path, WriteMode mode) {
  KJ_IF_MAYBE(f, tryOpenSubdir(path, mode)) {
    return kj::mv(*f);
  } else if (has(mode, WriteMode::CREATE) && !has(mode, WriteMode::MODIFY)) {
    KJ_FAIL_REQUIRE("directory already exists", path) { break; }
  } else if (has(mode, WriteMode::MODIFY) && !has(mode, WriteMode::CREATE)) {
    KJ_FAIL_REQUIRE("directory does not exist", path) { break; }
  } else if (!has(mode, WriteMode::MODIFY) && !has(mode, WriteMode::CREATE)) {
    KJ_FAIL_ASSERT("neither WriteMode::CREATE nor WriteMode::MODIFY was given", path) { break; }
  } else {
    // Shouldn't happen.
    KJ_FAIL_ASSERT("tryOpenSubdir() returned null despite no preconditions", path) { break; }
  }
  return newInMemoryDirectory(nullClock());
}

void Directory::symlink(PathPtr linkpath, StringPtr content, WriteMode mode) {
  if (!trySymlink(linkpath, content, mode)) {
    if (has(mode, WriteMode::CREATE)) {
      KJ_FAIL_REQUIRE("path already exsits", linkpath) { break; }
    } else {
      // Shouldn't happen.
      KJ_FAIL_ASSERT("symlink() returned null despite no preconditions", linkpath) { break; }
    }
  }
}

void Directory::transfer(PathPtr toPath, WriteMode toMode,
                         Directory& fromDirectory, PathPtr fromPath,
                         TransferMode mode) {
  if (!tryTransfer(toPath, toMode, fromDirectory, fromPath, mode)) {
    if (has(toMode, WriteMode::CREATE)) {
      KJ_FAIL_REQUIRE("toPath already exists or fromPath doesn't exist", toPath, fromPath) {
        break;
      }
    } else {
      KJ_FAIL_ASSERT("fromPath doesn't exist", fromPath) { break; }
    }
  }
}

static void copyContents(Directory& to, ReadableDirectory& from);

static bool tryCopyDirectoryEntry(Directory& to, PathPtr toPath, WriteMode toMode,
                                  ReadableDirectory& from, PathPtr fromPath,
                                  FsNode::Type type, bool atomic) {
  // TODO(cleanup): Make this reusable?

  switch (type) {
    case FsNode::Type::FILE: {
      KJ_IF_MAYBE(fromFile, from.tryOpenFile(fromPath)) {
        if (atomic) {
          auto replacer = to.replaceFile(toPath, toMode);
          replacer->get().copy(0, **fromFile, 0, kj::maxValue);
          return replacer->tryCommit();
        } else KJ_IF_MAYBE(toFile, to.tryOpenFile(toPath, toMode)) {
          toFile->get()->copy(0, **fromFile, 0, kj::maxValue);
          return true;
        } else {
          return false;
        }
      } else {
        // Apparently disappeared. Treat as source-doesn't-exist.
        return false;
      }
    }
    case FsNode::Type::DIRECTORY:
      KJ_IF_MAYBE(fromSubdir, from.tryOpenSubdir(fromPath)) {
        if (atomic) {
          auto replacer = to.replaceSubdir(toPath, toMode);
          copyContents(replacer->get(), **fromSubdir);
          return replacer->tryCommit();
        } else KJ_IF_MAYBE(toSubdir, to.tryOpenSubdir(toPath, toMode)) {
          copyContents(**toSubdir, **fromSubdir);
          return true;
        } else {
          return false;
        }
      } else {
        // Apparently disappeared. Treat as source-doesn't-exist.
        return false;
      }
    case FsNode::Type::SYMLINK:
      KJ_IF_MAYBE(content, from.tryReadlink(fromPath)) {
        return to.trySymlink(toPath, *content, toMode);
      } else {
        // Apparently disappeared. Treat as source-doesn't-exist.
        return false;
      }
      break;

    default:
      // Note: Unclear whether it's better to throw an error here or just ignore it / log a
      //   warning. Can reconsider when we see an actual use case.
      KJ_FAIL_REQUIRE("can only copy files, directories, and symlinks", fromPath) {
        return false;
      }
  }
}

static void copyContents(Directory& to, ReadableDirectory& from) {
  for (auto& entry: from.listEntries()) {
    Path subPath(kj::mv(entry.name));
    tryCopyDirectoryEntry(to, subPath, WriteMode::CREATE, from, subPath, entry.type, false);
  }
}

bool Directory::tryTransfer(PathPtr toPath, WriteMode toMode,
                            Directory& fromDirectory, PathPtr fromPath,
                            TransferMode mode) {
  KJ_REQUIRE(toPath.size() > 0, "can't replace self") { return false; }

  // First try reversing.
  KJ_IF_MAYBE(result, fromDirectory.tryTransferTo(*this, toPath, toMode, fromPath, mode)) {
    return *result;
  }

  switch (mode) {
    case TransferMode::COPY:
      KJ_IF_MAYBE(meta, fromDirectory.tryLstat(fromPath)) {
        return tryCopyDirectoryEntry(*this, toPath, toMode, fromDirectory,
                                     fromPath, meta->type, true);
      } else {
        // Source doesn't exist.
        return false;
      }
    case TransferMode::MOVE:
      // Implement move as copy-then-delete.
      if (!tryTransfer(toPath, toMode, fromDirectory, fromPath, TransferMode::COPY)) {
        return false;
      }
      fromDirectory.remove(fromPath);
      return true;
    case TransferMode::LINK:
      KJ_FAIL_REQUIRE("can't link across different Directory implementations") { return false; }
  }
}

Maybe<bool> Directory::tryTransferTo(Directory& toDirectory, PathPtr toPath, WriteMode toMode,
                                     PathPtr fromPath, TransferMode mode) {
  return nullptr;
}

void Directory::remove(PathPtr path) {
  if (!tryRemove(path)) {
    KJ_FAIL_REQUIRE("path to remove doesn't exist", path) { break; }
  }
}

void Directory::commitFailed(WriteMode mode) {
  if (has(mode, WriteMode::CREATE) && !has(mode, WriteMode::MODIFY)) {
    KJ_FAIL_REQUIRE("replace target already exists") { break; }
  } else if (has(mode, WriteMode::MODIFY) && !has(mode, WriteMode::CREATE)) {
    KJ_FAIL_REQUIRE("replace target does not exist") { break; }
  } else if (!has(mode, WriteMode::MODIFY) && !has(mode, WriteMode::CREATE)) {
    KJ_FAIL_ASSERT("neither WriteMode::CREATE nor WriteMode::MODIFY was given") { break; }
  } else {
    KJ_FAIL_ASSERT("tryCommit() returned null despite no preconditions") { break; }
  }
}

// =======================================================================================

namespace {

class InMemoryFile final: public File, public Refcounted {
public:
  InMemoryFile(Clock& clock): clock(clock), lastModified(clock.now()) {}

  Own<FsNode> cloneFsNode() override {
    return addRef(*this);
  }

  Maybe<int> getFd() override {
    return nullptr;
  }

  Metadata stat() override {
    return Metadata { Type::FILE, size, size, lastModified, 1 };
  }

  void sync() override {}
  void datasync() override {}
  // no-ops

  size_t read(uint64_t offset, ArrayPtr<byte> buffer) override {
    if (offset >= size) {
      // Entirely out-of-range.
      return 0;
    }

    size_t readSize = kj::min(buffer.size(), size - offset);
    memcpy(buffer.begin(), bytes.begin() + offset, readSize);
    return readSize;
  }

  Array<const byte> mmap(uint64_t offset, uint64_t size) override {
    KJ_REQUIRE(offset + size >= offset, "mmap() request overflows uint64");
    ensureCapacity(offset + size);

    ArrayDisposer* disposer = new MmapDisposer(addRef(*this));
    return Array<const byte>(bytes.begin() + offset, size, *disposer);
  }

  Array<byte> mmapPrivate(uint64_t offset, uint64_t size) override {
    // Return a copy.

    // Allocate exactly the size requested.
    auto result = heapArray<byte>(size);

    // Use read() to fill it.
    size_t actual = read(offset, result);

    // Ignore the rest.
    if (actual < size) {
      memset(result.begin() + actual, 0, size - actual);
    }

    return result;
  }

  void write(uint64_t offset, ArrayPtr<const byte> data) override {
    if (data.size() == 0) return;
    modified();
    uint64_t end = offset + data.size();
    KJ_REQUIRE(end >= offset, "write() request overflows uint64");
    ensureCapacity(end);
    size = kj::max(size, end);
    memcpy(bytes.begin() + offset, data.begin(), data.size());
  }

  void zero(uint64_t offset, uint64_t zeroSize) override {
    if (zeroSize == 0) return;
    modified();
    uint64_t end = offset + zeroSize;
    KJ_REQUIRE(end >= offset, "zero() request overflows uint64");
    ensureCapacity(end);
    size = kj::max(size, end);
    memset(bytes.begin() + offset, 0, zeroSize);
  }

  void truncate(uint64_t newSize) override {
    if (newSize < size) {
      modified();
      memset(bytes.begin() + newSize, 0, size - newSize);
      size = newSize;
    } else if (newSize > size) {
      modified();
      ensureCapacity(newSize);
      size = newSize;
    }
  }

  Own<WritableFileMapping> mmapWritable(uint64_t offset, uint64_t size) override {
    uint64_t end = offset + size;
    KJ_REQUIRE(end >= offset, "mmapWritable() request overflows uint64");
    ensureCapacity(end);
    return heap<WritableFileMappingImpl>(addRef(*this), bytes.slice(offset, end));
  }

  size_t copy(uint64_t offset, ReadableFile& from,
              uint64_t fromOffset, uint64_t copySize) override {
    size_t fromFileSize = from.stat().size;
    if (fromFileSize <= fromOffset) return 0;

    // Clamp size to EOF.
    copySize = kj::min(copySize, fromFileSize - fromOffset);
    if (copySize == 0) return 0;

    // Allocate space for the copy.
    uint64_t end = offset + copySize;
    ensureCapacity(end);

    // Read directly into our backing store.
    size_t n = from.read(fromOffset, bytes.slice(offset, end));
    size = kj::max(size, offset + n);

    modified();
    return n;
  }

private:
  Clock& clock;
  Array<byte> bytes;
  size_t size = 0;     // bytes may be larger than this to accommodate mmaps
  Date lastModified;
  uint mmapCount = 0;  // number of mappings outstanding

  void ensureCapacity(size_t capacity) {
    if (bytes.size() < capacity) {
      KJ_ASSERT(mmapCount == 0,
          "InMemoryFile cannot resize the file backing store while memory mappings exist.");

      auto newBytes = heapArray<byte>(kj::max(capacity, bytes.size() * 2));
      memcpy(newBytes.begin(), bytes.begin(), size);
      memset(newBytes.begin() + size, 0, newBytes.size() - size);
      bytes = kj::mv(newBytes);
    }
  }

  void modified() {
    lastModified = clock.now();
  }

  class MmapDisposer final: public ArrayDisposer {
  public:
    MmapDisposer(Own<InMemoryFile>&& refParam): ref(kj::mv(refParam)) {
      ++ref->mmapCount;
    }
    ~MmapDisposer() noexcept(false) {
      --ref->mmapCount;
    }

    void disposeImpl(void* firstElement, size_t elementSize, size_t elementCount,
                     size_t capacity, void (*destroyElement)(void*)) const override {
      delete this;
    }

  private:
    Own<InMemoryFile> ref;
  };

  class WritableFileMappingImpl final: public WritableFileMapping {
  public:
    WritableFileMappingImpl(Own<InMemoryFile>&& refParam, ArrayPtr<byte> range)
        : ref(kj::mv(refParam)), range(range) {
      ++ref->mmapCount;
    }
    ~WritableFileMappingImpl() noexcept(false) {
      --ref->mmapCount;
    }

    ArrayPtr<byte> get() override {
      return range;
    }

    void changed(ArrayPtr<byte> slice) override {
      ref->modified();
    }

    void sync(ArrayPtr<byte> slice) override {
      ref->modified();
    }

  private:
    Own<InMemoryFile> ref;
    ArrayPtr<byte> range;
  };
};

// -----------------------------------------------------------------------------

class InMemoryDirectory final: public Directory, public Refcounted {
public:
  InMemoryDirectory(Clock& clock)
      : clock(clock), lastModified(clock.now()) {}

  Own<FsNode> cloneFsNode() override {
    return addRef(*this);
  }

  Maybe<int> getFd() override {
    return nullptr;
  }

  Metadata stat() override {
    return Metadata { Type::DIRECTORY, 0, 0, lastModified, 1 };
  }

  void sync() override {}
  void datasync() override {}
  // no-ops

  Array<String> listNames()  override {
    return KJ_MAP(e, entries) { return heapString(e.first); };
  }

  Array<Entry> listEntries() override {
    return KJ_MAP(e, entries) {
      FsNode::Type type;
      if (e.second.node.is<SymlinkNode>()) {
        type = FsNode::Type::SYMLINK;
      } else if (e.second.node.is<FileNode>()) {
        type = FsNode::Type::FILE;
      } else {
        KJ_ASSERT(e.second.node.is<DirectoryNode>());
        type = FsNode::Type::DIRECTORY;
      }

      return Entry { type, heapString(e.first) };
    };
  }

  bool exists(PathPtr path) override {
    if (path.size() == 0) {
      return true;
    } else if (path.size() == 1) {
      KJ_IF_MAYBE(entry, tryGetEntry(path[0])) {
        return exists(*entry);
      } else {
        return false;
      }
    } else {
      KJ_IF_MAYBE(subdir, tryGetParent(path[0])) {
        return subdir->get()->exists(path.slice(1, path.size()));
      } else {
        return false;
      }
    }
  }

  Maybe<FsNode::Metadata> tryLstat(PathPtr path) override {
    if (path.size() == 0) {
      return stat();
    } else if (path.size() == 1) {
      KJ_IF_MAYBE(entry, tryGetEntry(path[0])) {
        if (entry->node.is<FileNode>()) {
          return entry->node.get<FileNode>().file->stat();
        } else if (entry->node.is<DirectoryNode>()) {
          return entry->node.get<DirectoryNode>().directory->stat();
        } else if (entry->node.is<SymlinkNode>()) {
          auto& link = entry->node.get<SymlinkNode>();
          return FsNode::Metadata { FsNode::Type::SYMLINK, 0, 0, link.lastModified, 1 };
        } else {
          KJ_FAIL_ASSERT("unknown node type") { return nullptr; }
        }
      } else {
        return nullptr;
      }
    } else {
      KJ_IF_MAYBE(subdir, tryGetParent(path[0])) {
        return subdir->get()->tryLstat(path.slice(1, path.size()));
      } else {
        return nullptr;
      }
    }
  }

  Maybe<Own<ReadableFile>> tryOpenFile(PathPtr path) override {
    if (path.size() == 0) {
      KJ_FAIL_REQUIRE("not a file") { return nullptr; }
    } else if (path.size() == 1) {
      KJ_IF_MAYBE(entry, tryGetEntry(path[0])) {
        return asFile(*entry);
      } else {
        return nullptr;
      }
    } else {
      KJ_IF_MAYBE(subdir, tryGetParent(path[0])) {
        return subdir->get()->tryOpenFile(path.slice(1, path.size()));
      } else {
        return nullptr;
      }
    }
  }

  Maybe<Own<ReadableDirectory>> tryOpenSubdir(PathPtr path) override {
    if (path.size() == 0) {
      return clone();
    } else if (path.size() == 1) {
      KJ_IF_MAYBE(entry, tryGetEntry(path[0])) {
        return asDirectory(*entry);
      } else {
        return nullptr;
      }
    } else {
      KJ_IF_MAYBE(subdir, tryGetParent(path[0])) {
        return subdir->get()->tryOpenSubdir(path.slice(1, path.size()));
      } else {
        return nullptr;
      }
    }
  }

  Maybe<String> tryReadlink(PathPtr path) override {
    if (path.size() == 0) {
      KJ_FAIL_REQUIRE("not a symlink") { return nullptr; }
    } else if (path.size() == 1) {
      KJ_IF_MAYBE(entry, tryGetEntry(path[0])) {
        return asSymlink(*entry);
      } else {
        return nullptr;
      }
    } else {
      KJ_IF_MAYBE(subdir, tryGetParent(path[0])) {
        return subdir->get()->tryReadlink(path.slice(1, path.size()));
      } else {
        return nullptr;
      }
    }
  }

  Maybe<Own<File>> tryOpenFile(PathPtr path, WriteMode mode) override {
    if (path.size() == 0) {
      if (has(mode, WriteMode::MODIFY)) {
        KJ_FAIL_REQUIRE("not a file") { return nullptr; }
      } else if (has(mode, WriteMode::CREATE)) {
        return nullptr;  // already exists (as a directory)
      } else {
        KJ_FAIL_REQUIRE("can't replace self") { return nullptr; }
      }
    } else if (path.size() == 1) {
      KJ_IF_MAYBE(entry, openEntry(path[0], mode)) {
        return asFile(*entry, mode);
      } else {
        return nullptr;
      }
    } else {
      KJ_IF_MAYBE(child, tryGetParent(path[0], mode)) {
        return child->get()->tryOpenFile(path.slice(1, path.size()), mode);
      } else {
        return nullptr;
      }
    }
  }

  Own<Replacer<File>> replaceFile(PathPtr path, WriteMode mode) override {
    if (path.size() == 0) {
      KJ_FAIL_REQUIRE("can't replace self") { break; }
    } else if (path.size() == 1) {
      return heap<ReplacerImpl<File>>(*this, path[0], newInMemoryFile(clock), mode);
    } else {
      KJ_IF_MAYBE(child, tryGetParent(path[0], mode)) {
        return child->get()->replaceFile(path.slice(1, path.size()), mode);
      }
    }
    return heap<BrokenReplacer<File>>(newInMemoryFile(clock));
  }

  Maybe<Own<Directory>> tryOpenSubdir(PathPtr path, WriteMode mode) override {
    if (path.size() == 0) {
      if (has(mode, WriteMode::MODIFY)) {
        return addRef(*this);
      } else if (has(mode, WriteMode::CREATE)) {
        return nullptr;  // already exists
      } else {
        KJ_FAIL_REQUIRE("can't replace self") { return nullptr; }
      }
    } else if (path.size() == 1) {
      KJ_IF_MAYBE(entry, openEntry(path[0], mode)) {
        return asDirectory(*entry, mode);
      } else {
        return nullptr;
      }
    } else {
      KJ_IF_MAYBE(child, tryGetParent(path[0], mode)) {
        return child->get()->tryOpenSubdir(path.slice(1, path.size()), mode);
      } else {
        return nullptr;
      }
    }
  }

  Own<Replacer<Directory>> replaceSubdir(PathPtr path, WriteMode mode) override {
    if (path.size() == 0) {
      KJ_FAIL_REQUIRE("can't replace self") { break; }
    } else if (path.size() == 1) {
      return heap<ReplacerImpl<Directory>>(*this, path[0], newInMemoryDirectory(clock), mode);
    } else {
      KJ_IF_MAYBE(child, tryGetParent(path[0], mode)) {
        return child->get()->replaceSubdir(path.slice(1, path.size()), mode);
      }
    }
    return heap<BrokenReplacer<Directory>>(newInMemoryDirectory(clock));
  }

  Maybe<Own<AppendableFile>> tryAppendFile(PathPtr path, WriteMode mode) override {
    if (path.size() == 0) {
      if (has(mode, WriteMode::MODIFY)) {
        KJ_FAIL_REQUIRE("not a file") { return nullptr; }
      } else if (has(mode, WriteMode::CREATE)) {
        return nullptr;  // already exists (as a directory)
      } else {
        KJ_FAIL_REQUIRE("can't replace self") { return nullptr; }
      }
    } else if (path.size() == 1) {
      KJ_IF_MAYBE(entry, openEntry(path[0], mode)) {
        return asFile(*entry, mode).map(newFileAppender);
      } else {
        return nullptr;
      }
    } else {
      KJ_IF_MAYBE(child, tryGetParent(path[0], mode)) {
        return child->get()->tryAppendFile(path.slice(1, path.size()), mode);
      } else {
        return nullptr;
      }
    }
  }

  bool trySymlink(PathPtr path, StringPtr content, WriteMode mode) override {
    if (path.size() == 0) {
      if (has(mode, WriteMode::CREATE)) {
        return false;
      } else {
        KJ_FAIL_REQUIRE("can't replace self") { return false; }
      }
    } else if (path.size() == 1) {
      KJ_IF_MAYBE(entry, openEntry(path[0], mode)) {
        entry->init(SymlinkNode { clock.now(), heapString(content) });
        modified();
        return true;
      } else {
        return false;
      }
    } else {
      KJ_IF_MAYBE(child, tryGetParent(path[0], mode)) {
        return child->get()->trySymlink(path.slice(1, path.size()), content, mode);
      } else {
        KJ_FAIL_REQUIRE("couldn't create parent directory") { return false; }
      }
    }
  }

  Own<File> createTemporary() override {
    return newInMemoryFile(clock);
  }

  bool tryTransfer(PathPtr toPath, WriteMode toMode,
                   Directory& fromDirectory, PathPtr fromPath, TransferMode mode) override {
    if (toPath.size() == 0) {
      if (has(toMode, WriteMode::CREATE)) {
        return false;
      } else {
        KJ_FAIL_REQUIRE("can't replace self") { return false; }
      }
    } else if (toPath.size() == 1) {
      // tryTransferChild() needs to at least know the node type, so do an lstat.
      KJ_IF_MAYBE(meta, fromDirectory.tryLstat(fromPath)) {
        KJ_IF_MAYBE(entry, openEntry(toPath[0], toMode)) {
          // Make sure if we just cerated a new entry, and we don't successfully transfer to it, we
          // remove the entry before returning.
          bool needRollback = entry->node == nullptr;
          KJ_DEFER(if (needRollback) { entries.erase(toPath[0]); });

          if (tryTransferChild(*entry, meta->type, meta->lastModified, meta->size,
                               fromDirectory, fromPath, mode)) {
            modified();
            needRollback = false;
            return true;
          } else {
            KJ_FAIL_REQUIRE("InMemoryDirectory can't link an inode of this type", fromPath) {
              return false;
            }
          }
        } else {
          return false;
        }
      } else {
        return false;
      }
    } else {
      // TODO(someday): Ideally we wouldn't create parent directories if fromPath doesn't exist.
      //   This requires a different approach to the code here, though.
      KJ_IF_MAYBE(child, tryGetParent(toPath[0], toMode)) {
        return child->get()->tryTransfer(
            toPath.slice(1, toPath.size()), toMode, fromDirectory, fromPath, mode);
      } else {
        return false;
      }
    }
  }

  Maybe<bool> tryTransferTo(Directory& toDirectory, PathPtr toPath, WriteMode toMode,
                            PathPtr fromPath, TransferMode mode) override {
    if (fromPath.size() <= 1) {
      // If `fromPath` is in this directory (or *is* this directory) then we don't have any
      // optimizations.
      return nullptr;
    }

    // `fromPath` is in a subdirectory. It could turn out that that subdirectory is not an
    // InMemoryDirectory and is instead something `toDirectory` is friendly with. So let's follow
    // the path.

    KJ_IF_MAYBE(child, tryGetParent(fromPath[0], WriteMode::MODIFY)) {
      // OK, switch back to tryTransfer() but use the subdirectory.
      return toDirectory.tryTransfer(toPath, toMode,
          **child, fromPath.slice(1, fromPath.size()), mode);
    } else {
      // Hmm, doesn't exist. Fall back to standard path.
      return nullptr;
    }
  }

  bool tryRemove(PathPtr path) override {
    if (path.size() == 0) {
      KJ_FAIL_REQUIRE("can't remove self from self") { return false; }
    } else if (path.size() == 1) {
      auto iter = entries.find(path[0]);
      if (iter == entries.end()) {
        return false;
      } else {
        entries.erase(iter);
        modified();
        return true;
      }
    } else {
      KJ_IF_MAYBE(child, tryGetParent(path[0], WriteMode::MODIFY)) {
        return child->get()->tryRemove(path.slice(1, path.size()));
      } else {
        return false;
      }
    }
  }

private:
  struct FileNode {
    Own<File> file;
  };
  struct DirectoryNode {
    Own<Directory> directory;
  };
  struct SymlinkNode {
    Date lastModified;
    String content;

    Path parse() {
      KJ_CONTEXT("parsing symlink", content);
      return Path::parse(content);
    }
  };

  struct EntryImpl {
    String name;
    OneOf<FileNode, DirectoryNode, SymlinkNode> node;

    EntryImpl(String&& name): name(kj::mv(name)) {}

    Own<File> init(FileNode&& value) {
      return node.init<FileNode>(kj::mv(value)).file->clone();
    }
    Own<Directory> init(DirectoryNode&& value) {
      return node.init<DirectoryNode>(kj::mv(value)).directory->clone();
    }
    void init(SymlinkNode&& value) {
      node.init<SymlinkNode>(kj::mv(value));
    }
    bool init(OneOf<FileNode, DirectoryNode, SymlinkNode>&& value) {
      node = kj::mv(value);
      return node != nullptr;
    }

    void set(Own<File>&& value) {
      node.init<FileNode>(FileNode { kj::mv(value) });
    }
    void set(Own<Directory>&& value) {
      node.init<DirectoryNode>(DirectoryNode { kj::mv(value) });
    }
  };

  Clock& clock;

  std::map<StringPtr, EntryImpl> entries;
  // Note: If this changes to a non-sorted map, listNames() and listEntries() must be updated to
  //   sort their results.

  Date lastModified;

  template <typename T>
  class ReplacerImpl: public Replacer<T> {
  public:
    ReplacerImpl(InMemoryDirectory& directory, kj::StringPtr name, Own<T> inner, WriteMode mode)
        : Replacer<T>(mode), directory(addRef(directory)), name(heapString(name)),
          inner(kj::mv(inner)) {}

    T& get() override { return *inner; }

    bool tryCommit() override {
      KJ_REQUIRE(!committed, "commit() already called") { return true; }

      KJ_IF_MAYBE(entry, directory->openEntry(name, Replacer<T>::mode)) {
        entry->set(inner->clone());
        directory->modified();
        return true;
      } else {
        return false;
      }
    }

  private:
    bool committed = false;
    Own<InMemoryDirectory> directory;
    kj::String name;
    Own<T> inner;
  };

  template <typename T>
  class BrokenReplacer: public Replacer<T> {
    // For recovery path when exceptions are disabled.

  public:
    BrokenReplacer(Own<T> inner)
        : Replacer<T>(WriteMode::CREATE | WriteMode::MODIFY),
          inner(kj::mv(inner)) {}

    T& get() override { return *inner; }
    bool tryCommit() override { return false; }

  private:
    Own<T> inner;
  };

  Maybe<EntryImpl&> openEntry(kj::StringPtr name, WriteMode mode) {
    // TODO(perf): We could avoid a copy if the entry exists, at the expense of a double-lookup if
    //   it doesn't. Maybe a better map implementation will solve everything?
    return openEntry(heapString(name), mode);
  }

  Maybe<EntryImpl&> openEntry(String&& name, WriteMode mode) {
    if (has(mode, WriteMode::CREATE)) {
      EntryImpl entry(kj::mv(name));
      StringPtr nameRef = entry.name;
      auto insertResult = entries.insert(std::make_pair(nameRef, kj::mv(entry)));

      if (!insertResult.second && !has(mode, WriteMode::MODIFY)) {
        // Entry already existed and MODIFY not specified.
        return nullptr;
      }

      return insertResult.first->second;
    } else if (has(mode, WriteMode::MODIFY)) {
      return tryGetEntry(name);
    } else {
      // Neither CREATE nor MODIFY specified: precondition always fails.
      return nullptr;
    }
  }

  kj::Maybe<EntryImpl&> tryGetEntry(kj::StringPtr name) {
    auto iter = entries.find(name);
    if (iter == entries.end()) {
      return nullptr;
    } else {
      return iter->second;
    }
  }

  kj::Maybe<Own<ReadableDirectory>> tryGetParent(kj::StringPtr name) {
    KJ_IF_MAYBE(entry, tryGetEntry(name)) {
      return asDirectory(*entry);
    } else {
      return nullptr;
    }
  }

  kj::Maybe<Own<Directory>> tryGetParent(kj::StringPtr name, WriteMode mode) {
    // Get a directory which is a parent of the eventual target. If `mode` includes
    // WriteMode::CREATE_PARENTS, possibly create the parent directory.

    WriteMode parentMode = has(mode, WriteMode::CREATE) && has(mode, WriteMode::CREATE_PARENT)
        ? WriteMode::CREATE | WriteMode::MODIFY   // create parent
        : WriteMode::MODIFY;                      // don't create parent

    // Possibly create parent.
    KJ_IF_MAYBE(entry, openEntry(name, parentMode)) {
      if (entry->node.is<DirectoryNode>()) {
        return entry->node.get<DirectoryNode>().directory->clone();
      } else if (entry->node == nullptr) {
        modified();
        return entry->init(DirectoryNode { newInMemoryDirectory(clock) });
      }
      // Continue on.
    }

    if (has(mode, WriteMode::CREATE)) {
      // CREATE is documented as returning null when the file already exists. In this case, the
      // file does NOT exist because the parent directory does not exist or is not a directory.
      KJ_FAIL_REQUIRE("parent is not a directory") { return nullptr; }
    } else {
      return nullptr;
    }
  }

  bool exists(EntryImpl& entry) {
    if (entry.node.is<SymlinkNode>()) {
      return exists(entry.node.get<SymlinkNode>().parse());
    } else {
      return true;
    }
  }
  Maybe<Own<ReadableFile>> asFile(EntryImpl& entry) {
    if (entry.node.is<FileNode>()) {
      return entry.node.get<FileNode>().file->clone();
    } else if (entry.node.is<SymlinkNode>()) {
      return tryOpenFile(entry.node.get<SymlinkNode>().parse());
    } else {
      KJ_FAIL_REQUIRE("not a file") { return nullptr; }
    }
  }
  Maybe<Own<ReadableDirectory>> asDirectory(EntryImpl& entry) {
    if (entry.node.is<DirectoryNode>()) {
      return entry.node.get<DirectoryNode>().directory->clone();
    } else if (entry.node.is<SymlinkNode>()) {
      return tryOpenSubdir(entry.node.get<SymlinkNode>().parse());
    } else {
      KJ_FAIL_REQUIRE("not a directory") { return nullptr; }
    }
  }
  Maybe<String> asSymlink(EntryImpl& entry) {
    if (entry.node.is<SymlinkNode>()) {
      return heapString(entry.node.get<SymlinkNode>().content);
    } else {
      KJ_FAIL_REQUIRE("not a symlink") { return nullptr; }
    }
  }

  Maybe<Own<File>> asFile(EntryImpl& entry, WriteMode mode) {
    if (entry.node.is<FileNode>()) {
      return entry.node.get<FileNode>().file->clone();
    } else if (entry.node.is<SymlinkNode>()) {
      // CREATE_PARENT doesn't apply to creating the parents of a symlink target. However, the
      // target itself can still be created.
      return tryOpenFile(entry.node.get<SymlinkNode>().parse(), mode - WriteMode::CREATE_PARENT);
    } else if (entry.node == nullptr) {
      KJ_ASSERT(has(mode, WriteMode::CREATE));
      modified();
      return entry.init(FileNode { newInMemoryFile(clock) });
    } else {
      KJ_FAIL_REQUIRE("not a file") { return nullptr; }
    }
  }
  Maybe<Own<Directory>> asDirectory(EntryImpl& entry, WriteMode mode) {
    if (entry.node.is<DirectoryNode>()) {
      return entry.node.get<DirectoryNode>().directory->clone();
    } else if (entry.node.is<SymlinkNode>()) {
      // CREATE_PARENT doesn't apply to creating the parents of a symlink target. However, the
      // target itself can still be created.
      return tryOpenSubdir(entry.node.get<SymlinkNode>().parse(), mode - WriteMode::CREATE_PARENT);
    } else if (entry.node == nullptr) {
      KJ_ASSERT(has(mode, WriteMode::CREATE));
      modified();
      return entry.init(DirectoryNode { newInMemoryDirectory(clock) });
    } else {
      KJ_FAIL_REQUIRE("not a directory") { return nullptr; }
    }
  }

  void modified() {
    lastModified = clock.now();
  }

  bool tryTransferChild(EntryImpl& entry, const FsNode::Type type, kj::Maybe<Date> lastModified,
                        kj::Maybe<uint64_t> size, Directory& fromDirectory, PathPtr fromPath,
                        TransferMode mode) {
    switch (type) {
      case FsNode::Type::FILE:
        KJ_IF_MAYBE(file, fromDirectory.tryOpenFile(fromPath, WriteMode::MODIFY)) {
          if (mode == TransferMode::COPY) {
            auto copy = newInMemoryFile(clock);
            copy->copy(0, **file, 0, size.orDefault(kj::maxValue));
            entry.set(kj::mv(copy));
          } else {
            if (mode == TransferMode::MOVE) {
              KJ_ASSERT(fromDirectory.tryRemove(fromPath), "could't move node", fromPath) {
                return false;
              }
            }
            entry.set(kj::mv(*file));
          }
          return true;
        } else {
          KJ_FAIL_ASSERT("source node deleted concurrently during transfer", fromPath) {
            return false;
          }
        }
      case FsNode::Type::DIRECTORY:
        KJ_IF_MAYBE(subdir, fromDirectory.tryOpenSubdir(fromPath, WriteMode::MODIFY)) {
          if (mode == TransferMode::COPY) {
            auto copy = refcounted<InMemoryDirectory>(clock);
            for (auto& subEntry: subdir->get()->listEntries()) {
              EntryImpl newEntry(kj::mv(subEntry.name));
              Path filename(newEntry.name);
              if (!copy->tryTransferChild(newEntry, subEntry.type, nullptr, nullptr, **subdir,
                                          filename, TransferMode::COPY)) {
                KJ_LOG(ERROR, "couldn't copy node of type not supported by InMemoryDirectory",
                       filename);
              } else {
                StringPtr nameRef = newEntry.name;
                copy->entries.insert(std::make_pair(nameRef, kj::mv(newEntry)));
              }
            }
            entry.set(kj::mv(copy));
          } else {
            if (mode == TransferMode::MOVE) {
              KJ_ASSERT(fromDirectory.tryRemove(fromPath), "could't move node", fromPath) {
                return false;
              }
            }
            entry.set(kj::mv(*subdir));
          }
          return true;
        } else {
          KJ_FAIL_ASSERT("source node deleted concurrently during transfer", fromPath) {
            return false;
          }
        }
      case FsNode::Type::SYMLINK:
        KJ_IF_MAYBE(content, fromDirectory.tryReadlink(fromPath)) {
          // Since symlinks are immutable, we can implement LINK the same as COPY.
          entry.init(SymlinkNode { lastModified.orDefault(clock.now()), kj::mv(*content) });
          if (mode == TransferMode::MOVE) {
            KJ_ASSERT(fromDirectory.tryRemove(fromPath), "could't move node", fromPath) {
              return false;
            }
          }
          return true;
        } else {
          KJ_FAIL_ASSERT("source node deleted concurrently during transfer", fromPath) {
            return false;
          }
        }
      default:
        return false;
    }
  }
};

// -----------------------------------------------------------------------------

class AppendableFileImpl final: public AppendableFile {
public:
  AppendableFileImpl(Own<File>&& fileParam): file(kj::mv(fileParam)) {}

  Own<FsNode> cloneFsNode() override {
    return heap<AppendableFileImpl>(file->clone());
  }

  Maybe<int> getFd() override {
    return nullptr;
  }

  Metadata stat() override {
    return file->stat();
  }

  void sync() override { file->sync(); }
  void datasync() override { file->datasync(); }

  void write(const void* buffer, size_t size) override {
    file->write(file->stat().size, arrayPtr(reinterpret_cast<const byte*>(buffer), size));
  }

private:
  Own<File> file;
};

}  // namespace

// -----------------------------------------------------------------------------

Own<File> newInMemoryFile(Clock& clock) {
  return refcounted<InMemoryFile>(clock);
}
Own<Directory> newInMemoryDirectory(Clock& clock) {
  return refcounted<InMemoryDirectory>(clock);
}
Own<AppendableFile> newFileAppender(Own<File> inner) {
  return heap<AppendableFileImpl>(kj::mv(inner));
}

} // namespace kj
