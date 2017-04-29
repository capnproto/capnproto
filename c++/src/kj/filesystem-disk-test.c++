// Copyright (c) 2016 Sandstorm Development Group, Inc. and contributors
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
#include "test.h"
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>

namespace kj {
namespace {

static Own<File> newTempFile() {
  char filename[] = "/var/tmp/kj-filesystem-test.XXXXXX";
  int fd;
  KJ_SYSCALL(fd = mkstemp(filename));
  KJ_DEFER(KJ_SYSCALL(unlink(filename)));
  return newDiskFile(AutoCloseFd(fd));
}

class TempDir {
public:
  TempDir(): filename(heapString("/var/tmp/kj-filesystem-test.XXXXXX")) {
    if (mkdtemp(filename.begin()) == nullptr) {
      KJ_FAIL_SYSCALL("mkdtemp", errno, filename);
    }
  }

  Own<Directory> get() {
    int fd;
    KJ_SYSCALL(fd = open(filename.cStr(), O_RDONLY));
    return newDiskDirectory(AutoCloseFd(fd));
  }

  ~TempDir() noexcept(false) {
    recursiveDelete(filename);
  }

private:
  String filename;

  static void recursiveDelete(StringPtr path) {
    // Recursively delete the temp dir, verifying that no .kj-tmp. files were left over.

    {
      DIR* dir = opendir(path.cStr());
      KJ_ASSERT(dir != nullptr);
      KJ_DEFER(closedir(dir));

      for (;;) {
        auto entry = readdir(dir);
        if (entry == nullptr) break;

        StringPtr name = entry->d_name;
        if (name == "." || name == "..") continue;

        auto subPath = kj::str(path, '/', entry->d_name);

        KJ_EXPECT(!name.startsWith(".kj-tmp."), "temp file not cleaned up", subPath);

        struct stat stats;
        KJ_SYSCALL(lstat(subPath.cStr(), &stats));

        if (S_ISDIR(stats.st_mode)) {
          recursiveDelete(subPath);
        } else {
          KJ_SYSCALL(unlink(subPath.cStr()));
        }
      }
    }

    KJ_SYSCALL(rmdir(path.cStr()));
  }
};

KJ_TEST("DiskFile") {
  auto file = newTempFile();

  KJ_EXPECT(file->readAllText() == "");

  file->writeAll("foo");
  KJ_EXPECT(file->readAllText() == "foo");

  file->write(3, StringPtr("bar").asBytes());
  KJ_EXPECT(file->readAllText() == "foobar");

  file->write(3, StringPtr("baz").asBytes());
  KJ_EXPECT(file->readAllText() == "foobaz");

  file->write(9, StringPtr("qux").asBytes());
  KJ_EXPECT(file->readAllText() == kj::StringPtr("foobaz\0\0\0qux", 12));

  file->truncate(6);
  KJ_EXPECT(file->readAllText() == "foobaz");

  file->truncate(18);
  KJ_EXPECT(file->readAllText() == kj::StringPtr("foobaz\0\0\0\0\0\0\0\0\0\0\0\0", 18));

  {
    auto mapping = file->mmap(0, 18);
    auto privateMapping = file->mmapPrivate(0, 18);
    auto writableMapping = file->mmapWritable(0, 18);

    KJ_EXPECT(mapping.size() == 18);
    KJ_EXPECT(privateMapping.size() == 18);
    KJ_EXPECT(writableMapping->get().size() == 18);

    KJ_EXPECT(writableMapping->get().begin() != mapping.begin());
    KJ_EXPECT(privateMapping.begin() != mapping.begin());
    KJ_EXPECT(writableMapping->get().begin() != privateMapping.begin());

    KJ_EXPECT(kj::str(mapping.slice(0, 6).asChars()) == "foobaz");
    KJ_EXPECT(kj::str(writableMapping->get().slice(0, 6).asChars()) == "foobaz");
    KJ_EXPECT(kj::str(privateMapping.slice(0, 6).asChars()) == "foobaz");

    privateMapping[0] = 'F';
    KJ_EXPECT(kj::str(mapping.slice(0, 6).asChars()) == "foobaz");
    KJ_EXPECT(kj::str(writableMapping->get().slice(0, 6).asChars()) == "foobaz");
    KJ_EXPECT(kj::str(privateMapping.slice(0, 6).asChars()) == "Foobaz");

    writableMapping->get()[0] = 'D';
    writableMapping->changed(writableMapping->get().slice(0, 1));
    KJ_EXPECT(kj::str(mapping.slice(0, 6).asChars()) == "Doobaz");
    KJ_EXPECT(kj::str(writableMapping->get().slice(0, 6).asChars()) == "Doobaz");
    KJ_EXPECT(kj::str(privateMapping.slice(0, 6).asChars()) == "Foobaz");

    file->write(0, StringPtr("qux").asBytes());
    KJ_EXPECT(kj::str(mapping.slice(0, 6).asChars()) == "quxbaz");
    KJ_EXPECT(kj::str(writableMapping->get().slice(0, 6).asChars()) == "quxbaz");
    KJ_EXPECT(kj::str(privateMapping.slice(0, 6).asChars()) == "Foobaz");

    file->write(12, StringPtr("corge").asBytes());
    KJ_EXPECT(kj::str(mapping.slice(12, 17).asChars()) == "corge");

    // Can shrink.
    file->truncate(6);
    KJ_EXPECT(kj::str(mapping.slice(12, 17).asChars()) == kj::StringPtr("\0\0\0\0\0", 5));

    // Can regrow.
    file->truncate(18);
    KJ_EXPECT(kj::str(mapping.slice(12, 17).asChars()) == kj::StringPtr("\0\0\0\0\0", 5));

    // Can even regrow past previous capacity.
    file->truncate(100);
  }

  file->truncate(6);

  KJ_EXPECT(file->readAllText() == "quxbaz");
  file->zero(3, 3);
  KJ_EXPECT(file->readAllText() == StringPtr("qux\0\0\0", 6));
}

KJ_TEST("DiskFile::copy()") {
  auto source = newTempFile();
  source->writeAll("foobarbaz");

  auto dest = newTempFile();
  dest->writeAll("quxcorge");

  KJ_EXPECT(dest->copy(3, *source, 6, kj::maxValue) == 3);
  KJ_EXPECT(dest->readAllText() == "quxbazge");

  KJ_EXPECT(dest->copy(0, *source, 3, 4) == 4);
  KJ_EXPECT(dest->readAllText() == "barbazge");

  KJ_EXPECT(dest->copy(0, *source, 128, kj::maxValue) == 0);

  KJ_EXPECT(dest->copy(4, *source, 3, 0) == 0);

  String bigString = strArray(repeat("foobar", 10000), "");
  source->truncate(bigString.size() + 1000);
  source->write(123, bigString.asBytes());

  dest->copy(321, *source, 123, bigString.size());
  KJ_EXPECT(dest->readAllText().slice(321) == bigString);
}

KJ_TEST("DiskDirectory") {
  TempDir tempDir;
  auto dir = tempDir.get();

  KJ_EXPECT(dir->listNames() == nullptr);
  KJ_EXPECT(dir->listEntries() == nullptr);
  KJ_EXPECT(!dir->exists(Path("foo")));
  KJ_EXPECT(dir->tryOpenFile(Path("foo")) == nullptr);
  KJ_EXPECT(dir->tryOpenFile(Path("foo"), WriteMode::MODIFY) == nullptr);

  {
    auto file = dir->openFile(Path("foo"), WriteMode::CREATE);
    file->writeAll("foobar");
  }

  KJ_EXPECT(dir->exists(Path("foo")));

  {
    auto stats = dir->lstat(Path("foo"));
    KJ_EXPECT(stats.type == FsNode::Type::FILE);
    KJ_EXPECT(stats.size == 6);
  }

  {
    auto list = dir->listNames();
    KJ_ASSERT(list.size() == 1);
    KJ_EXPECT(list[0] == "foo");
  }

  {
    auto list = dir->listEntries();
    KJ_ASSERT(list.size() == 1);
    KJ_EXPECT(list[0].name == "foo");
    KJ_EXPECT(list[0].type == FsNode::Type::FILE);
  }

  KJ_EXPECT(dir->openFile(Path("foo"))->readAllText() == "foobar");

  KJ_EXPECT(dir->tryOpenFile(Path({"foo", "bar"}), WriteMode::MODIFY) == nullptr);
  KJ_EXPECT(dir->tryOpenFile(Path({"bar", "baz"}), WriteMode::MODIFY) == nullptr);
  KJ_EXPECT_THROW_MESSAGE("parent is not a directory",
      dir->tryOpenFile(Path({"bar", "baz"}), WriteMode::CREATE));

  {
    auto file = dir->openFile(Path({"bar", "baz"}), WriteMode::CREATE | WriteMode::CREATE_PARENT);
    file->writeAll("bazqux");
  }

  KJ_EXPECT(dir->openFile(Path({"bar", "baz"}))->readAllText() == "bazqux");

  {
    auto stats = dir->lstat(Path("bar"));
    KJ_EXPECT(stats.type == FsNode::Type::DIRECTORY);
  }

  {
    auto list = dir->listNames();
    KJ_ASSERT(list.size() == 2);
    KJ_EXPECT(list[0] == "bar");
    KJ_EXPECT(list[1] == "foo");
  }

  {
    auto list = dir->listEntries();
    KJ_ASSERT(list.size() == 2);
    KJ_EXPECT(list[0].name == "bar");
    KJ_EXPECT(list[0].type == FsNode::Type::DIRECTORY);
    KJ_EXPECT(list[1].name == "foo");
    KJ_EXPECT(list[1].type == FsNode::Type::FILE);
  }

  {
    auto subdir = dir->openSubdir(Path("bar"));

    KJ_EXPECT(subdir->openFile(Path("baz"))->readAllText() == "bazqux");
  }

  auto subdir = dir->openSubdir(Path("corge"), WriteMode::CREATE);

  subdir->openFile(Path("grault"), WriteMode::CREATE)->writeAll("garply");

  KJ_EXPECT(dir->openFile(Path({"corge", "grault"}))->readAllText() == "garply");

  dir->openFile(Path({"corge", "grault"}), WriteMode::CREATE | WriteMode::MODIFY)
     ->write(0, StringPtr("rag").asBytes());
  KJ_EXPECT(dir->openFile(Path({"corge", "grault"}))->readAllText() == "ragply");

  KJ_EXPECT(dir->openSubdir(Path("corge"))->listNames().size() == 1);

  {
    auto replacer =
        dir->replaceFile(Path({"corge", "grault"}), WriteMode::CREATE | WriteMode::MODIFY);
    replacer->get().writeAll("rag");

    // temp file not in list
    KJ_EXPECT(dir->openSubdir(Path("corge"))->listNames().size() == 1);

    // Don't commit.
  }
  KJ_EXPECT(dir->openFile(Path({"corge", "grault"}))->readAllText() == "ragply");

  {
    auto replacer =
        dir->replaceFile(Path({"corge", "grault"}), WriteMode::CREATE | WriteMode::MODIFY);
    replacer->get().writeAll("rag");

    // temp file not in list
    KJ_EXPECT(dir->openSubdir(Path("corge"))->listNames().size() == 1);

    replacer->commit();
    KJ_EXPECT(dir->openFile(Path({"corge", "grault"}))->readAllText() == "rag");
  }

  KJ_EXPECT(dir->openFile(Path({"corge", "grault"}))->readAllText() == "rag");

  {
    auto appender = dir->appendFile(Path({"corge", "grault"}), WriteMode::MODIFY);
    appender->write("waldo", 5);
    appender->write("fred", 4);
  }

  KJ_EXPECT(dir->openFile(Path({"corge", "grault"}))->readAllText() == "ragwaldofred");

  KJ_EXPECT(dir->exists(Path("foo")));
  dir->remove(Path("foo"));
  KJ_EXPECT(!dir->exists(Path("foo")));
  KJ_EXPECT(!dir->tryRemove(Path("foo")));

  KJ_EXPECT(dir->exists(Path({"bar", "baz"})));
  dir->remove(Path({"bar", "baz"}));
  KJ_EXPECT(!dir->exists(Path({"bar", "baz"})));
  KJ_EXPECT(dir->exists(Path("bar")));
  KJ_EXPECT(!dir->tryRemove(Path({"bar", "baz"})));

  KJ_EXPECT(dir->exists(Path("corge")));
  KJ_EXPECT(dir->exists(Path({"corge", "grault"})));
  dir->remove(Path("corge"));
  KJ_EXPECT(!dir->exists(Path("corge")));
  KJ_EXPECT(!dir->exists(Path({"corge", "grault"})));
  KJ_EXPECT(!dir->tryRemove(Path("corge")));
}

KJ_TEST("DiskDirectory symlinks") {
  TempDir tempDir;
  auto dir = tempDir.get();

  dir->symlink(Path("foo"), "bar/qux/../baz", WriteMode::CREATE);

  KJ_EXPECT(!dir->trySymlink(Path("foo"), "bar/qux/../baz", WriteMode::CREATE));

  {
    auto stats = dir->lstat(Path("foo"));
    KJ_EXPECT(stats.type == FsNode::Type::SYMLINK);
  }

  KJ_EXPECT(dir->readlink(Path("foo")) == "bar/qux/../baz");

  // Broken link into non-existing directory cannot be opened in any mode.
  KJ_EXPECT(dir->tryOpenFile(Path("foo")) == nullptr);
  KJ_EXPECT(dir->tryOpenFile(Path("foo"), WriteMode::CREATE) == nullptr);
  KJ_EXPECT(dir->tryOpenFile(Path("foo"), WriteMode::MODIFY) == nullptr);
  KJ_EXPECT_THROW_MESSAGE("parent is not a directory",
      dir->tryOpenFile(Path("foo"), WriteMode::CREATE | WriteMode::MODIFY));
  KJ_EXPECT_THROW_MESSAGE("parent is not a directory",
      dir->tryOpenFile(Path("foo"),
          WriteMode::CREATE | WriteMode::MODIFY | WriteMode::CREATE_PARENT));

  // Create the directory.
  auto subdir = dir->openSubdir(Path("bar"), WriteMode::CREATE);
  subdir->openSubdir(Path("qux"), WriteMode::CREATE);

  // Link still points to non-existing file so cannot be open in most modes.
  KJ_EXPECT(dir->tryOpenFile(Path("foo")) == nullptr);
  KJ_EXPECT(dir->tryOpenFile(Path("foo"), WriteMode::CREATE) == nullptr);
  KJ_EXPECT(dir->tryOpenFile(Path("foo"), WriteMode::MODIFY) == nullptr);

  // But... CREATE | MODIFY works.
  dir->openFile(Path("foo"), WriteMode::CREATE | WriteMode::MODIFY)
     ->writeAll("foobar");

  KJ_EXPECT(dir->openFile(Path({"bar", "baz"}))->readAllText() == "foobar");
  KJ_EXPECT(dir->openFile(Path("foo"))->readAllText() == "foobar");
  KJ_EXPECT(dir->openFile(Path("foo"), WriteMode::MODIFY)->readAllText() == "foobar");

  // operations that modify the symlink
  dir->symlink(Path("foo"), "corge", WriteMode::MODIFY);
  KJ_EXPECT(dir->openFile(Path({"bar", "baz"}))->readAllText() == "foobar");
  KJ_EXPECT(dir->readlink(Path("foo")) == "corge");
  KJ_EXPECT(!dir->exists(Path("foo")));
  KJ_EXPECT(dir->lstat(Path("foo")).type == FsNode::Type::SYMLINK);
  KJ_EXPECT(dir->tryOpenFile(Path("foo")) == nullptr);

  dir->remove(Path("foo"));
  KJ_EXPECT(!dir->exists(Path("foo")));
  KJ_EXPECT(dir->tryOpenFile(Path("foo")) == nullptr);
}

KJ_TEST("DiskDirectory link") {
  TempDir tempDirSrc;
  TempDir tempDirDst;

  auto src = tempDirSrc.get();
  auto dst = tempDirDst.get();

  src->openFile(Path("foo"), WriteMode::CREATE | WriteMode::CREATE_PARENT)
     ->writeAll("foobar");

  dst->transfer(Path("link"), WriteMode::CREATE, *src, Path("foo"), TransferMode::LINK);

  KJ_EXPECT(dst->openFile(Path("link"))->readAllText() == "foobar");

  // Writing the old location modifies the new.
  src->openFile(Path("foo"), WriteMode::MODIFY)->writeAll("bazqux");
  KJ_EXPECT(dst->openFile(Path("link"))->readAllText() == "bazqux");

  // Replacing the old location doesn't modify the new.
  {
    auto replacer = src->replaceFile(Path("foo"), WriteMode::MODIFY);
    replacer->get().writeAll("corge");
    replacer->commit();
  }
  KJ_EXPECT(src->openFile(Path("foo"))->readAllText() == "corge");
  KJ_EXPECT(dst->openFile(Path("link"))->readAllText() == "bazqux");
}

KJ_TEST("DiskDirectory copy") {
  TempDir tempDirSrc;
  TempDir tempDirDst;

  auto src = tempDirSrc.get();
  auto dst = tempDirDst.get();

  src->openFile(Path({"foo", "bar"}), WriteMode::CREATE | WriteMode::CREATE_PARENT)
     ->writeAll("foobar");
  src->openFile(Path({"foo", "baz", "qux"}), WriteMode::CREATE | WriteMode::CREATE_PARENT)
     ->writeAll("bazqux");

  dst->transfer(Path("link"), WriteMode::CREATE, *src, Path("foo"), TransferMode::COPY);

  KJ_EXPECT(src->openFile(Path({"foo", "bar"}))->readAllText() == "foobar");
  KJ_EXPECT(src->openFile(Path({"foo", "baz", "qux"}))->readAllText() == "bazqux");
  KJ_EXPECT(dst->openFile(Path({"link", "bar"}))->readAllText() == "foobar");
  KJ_EXPECT(dst->openFile(Path({"link", "baz", "qux"}))->readAllText() == "bazqux");

  KJ_EXPECT(dst->exists(Path({"link", "bar"})));
  src->remove(Path({"foo", "bar"}));
  KJ_EXPECT(dst->openFile(Path({"link", "bar"}))->readAllText() == "foobar");
}

KJ_TEST("DiskDirectory move") {
  TempDir tempDirSrc;
  TempDir tempDirDst;

  auto src = tempDirSrc.get();
  auto dst = tempDirDst.get();

  src->openFile(Path({"foo", "bar"}), WriteMode::CREATE | WriteMode::CREATE_PARENT)
     ->writeAll("foobar");
  src->openFile(Path({"foo", "baz", "qux"}), WriteMode::CREATE | WriteMode::CREATE_PARENT)
     ->writeAll("bazqux");

  dst->transfer(Path("link"), WriteMode::CREATE, *src, Path("foo"), TransferMode::MOVE);

  KJ_EXPECT(!src->exists(Path({"foo"})));
  KJ_EXPECT(dst->openFile(Path({"link", "bar"}))->readAllText() == "foobar");
  KJ_EXPECT(dst->openFile(Path({"link", "baz", "qux"}))->readAllText() == "bazqux");
}

KJ_TEST("DiskDirectory createTemporary") {
  TempDir tempDir;
  auto dir = tempDir.get();
  auto file = dir->createTemporary();
  file->writeAll("foobar");
  KJ_EXPECT(file->readAllText() == "foobar");
  KJ_EXPECT(dir->listNames() == nullptr);
}

KJ_TEST("DiskDirectory replace directory with file") {
  TempDir tempDir;
  auto dir = tempDir.get();

  dir->openFile(Path({"foo", "bar"}), WriteMode::CREATE | WriteMode::CREATE_PARENT)
     ->writeAll("foobar");

  {
    // CREATE fails -- already exists.
    auto replacer = dir->replaceFile(Path("foo"), WriteMode::CREATE);
    replacer->get().writeAll("bazqux");
    KJ_EXPECT(!replacer->tryCommit());
  }

  // Still a directory.
  KJ_EXPECT(dir->lstat(Path("foo")).type == FsNode::Type::DIRECTORY);

  {
    // MODIFY succeeds.
    auto replacer = dir->replaceFile(Path("foo"), WriteMode::MODIFY);
    replacer->get().writeAll("bazqux");
    replacer->commit();
  }

  // Replaced with file.
  KJ_EXPECT(dir->openFile(Path("foo"))->readAllText() == "bazqux");
}

KJ_TEST("DiskDirectory replace file with directory") {
  TempDir tempDir;
  auto dir = tempDir.get();

  dir->openFile(Path("foo"), WriteMode::CREATE)
     ->writeAll("foobar");

  {
    // CREATE fails -- already exists.
    auto replacer = dir->replaceSubdir(Path("foo"), WriteMode::CREATE);
    replacer->get().openFile(Path("bar"), WriteMode::CREATE)->writeAll("bazqux");
    KJ_EXPECT(dir->listNames().size() == 1 && dir->listNames()[0] == "foo");
    KJ_EXPECT(!replacer->tryCommit());
  }

  // Still a file.
  KJ_EXPECT(dir->openFile(Path("foo"))->readAllText() == "foobar");

  {
    // MODIFY succeeds.
    auto replacer = dir->replaceSubdir(Path("foo"), WriteMode::MODIFY);
    replacer->get().openFile(Path("bar"), WriteMode::CREATE)->writeAll("bazqux");
    KJ_EXPECT(dir->listNames().size() == 1 && dir->listNames()[0] == "foo");
    replacer->commit();
  }

  // Replaced with directory.
  KJ_EXPECT(dir->openFile(Path({"foo", "bar"}))->readAllText() == "bazqux");
}

#ifndef HOLES_NOT_SUPPORTED
KJ_TEST("DiskFile holes") {
  TempDir tempDir;
  auto dir = tempDir.get();

  auto file = dir->openFile(Path("holes"), WriteMode::CREATE);
  file->writeAll("foobar");
  file->write(1 << 20, StringPtr("foobar").asBytes());

  // Allow for block sizes as low as 512 bytes and as high as 64k.
  auto meta = file->stat();
  KJ_EXPECT(meta.spaceUsed >= 2 * 512);
  KJ_EXPECT(meta.spaceUsed <= 2 * 65536);

  byte buf[7];
  {
    // Copy doesn't fill in holes.
    dir->transfer(Path("copy"), WriteMode::CREATE, Path("holes"), TransferMode::COPY);
    auto copy = dir->openFile(Path("copy"));
    KJ_EXPECT(copy->stat().spaceUsed == meta.spaceUsed);
    KJ_EXPECT(copy->read(0, buf) == 7);
    KJ_EXPECT(StringPtr(reinterpret_cast<char*>(buf), 6) == "foobar");

    KJ_EXPECT(copy->read(1 << 20, buf) == 6);
    KJ_EXPECT(StringPtr(reinterpret_cast<char*>(buf), 6) == "foobar");

    KJ_EXPECT(copy->read(1 << 19, buf) == 7);
    KJ_EXPECT(StringPtr(reinterpret_cast<char*>(buf), 6) == StringPtr("\0\0\0\0\0\0", 6));
  }

  file->truncate(1 << 21);
  KJ_EXPECT(file->stat().spaceUsed == meta.spaceUsed);
  KJ_EXPECT(file->read(1 << 20, buf) == 7);
  KJ_EXPECT(StringPtr(reinterpret_cast<char*>(buf), 6) == "foobar");

  {
    dir->transfer(Path("copy"), WriteMode::MODIFY, Path("holes"), TransferMode::COPY);
    auto copy = dir->openFile(Path("copy"));
    KJ_EXPECT(copy->stat().spaceUsed == meta.spaceUsed);
    KJ_EXPECT(copy->read(0, buf) == 7);
    KJ_EXPECT(StringPtr(reinterpret_cast<char*>(buf), 6) == "foobar");

    KJ_EXPECT(copy->read(1 << 20, buf) == 7);
    KJ_EXPECT(StringPtr(reinterpret_cast<char*>(buf), 6) == "foobar");

    KJ_EXPECT(copy->read(1 << 19, buf) == 7);
    KJ_EXPECT(StringPtr(reinterpret_cast<char*>(buf), 6) == StringPtr("\0\0\0\0\0\0", 6));
  }
}
#endif

}  // namespace
}  // namespace kj
