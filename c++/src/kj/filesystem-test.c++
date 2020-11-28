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
#include <wchar.h>

namespace kj {
namespace {

KJ_TEST("Path") {
  KJ_EXPECT(Path(nullptr).toString() == ".");
  KJ_EXPECT(Path(nullptr).toString(true) == "/");
  KJ_EXPECT(Path("foo").toString() == "foo");
  KJ_EXPECT(Path("foo").toString(true) == "/foo");

  KJ_EXPECT(Path({"foo", "bar"}).toString() == "foo/bar");
  KJ_EXPECT(Path({"foo", "bar"}).toString(true) == "/foo/bar");

  KJ_EXPECT(Path::parse("foo/bar").toString() == "foo/bar");
  KJ_EXPECT(Path::parse("foo//bar").toString() == "foo/bar");
  KJ_EXPECT(Path::parse("foo/./bar").toString() == "foo/bar");
  KJ_EXPECT(Path::parse("foo/../bar").toString() == "bar");
  KJ_EXPECT(Path::parse("foo/bar/..").toString() == "foo");
  KJ_EXPECT(Path::parse("foo/bar/../..").toString() == ".");

  KJ_EXPECT(Path({"foo", "bar"}).eval("baz").toString() == "foo/bar/baz");
  KJ_EXPECT(Path({"foo", "bar"}).eval("./baz").toString() == "foo/bar/baz");
  KJ_EXPECT(Path({"foo", "bar"}).eval("baz/qux").toString() == "foo/bar/baz/qux");
  KJ_EXPECT(Path({"foo", "bar"}).eval("baz//qux").toString() == "foo/bar/baz/qux");
  KJ_EXPECT(Path({"foo", "bar"}).eval("baz/./qux").toString() == "foo/bar/baz/qux");
  KJ_EXPECT(Path({"foo", "bar"}).eval("baz/../qux").toString() == "foo/bar/qux");
  KJ_EXPECT(Path({"foo", "bar"}).eval("baz/qux/..").toString() == "foo/bar/baz");
  KJ_EXPECT(Path({"foo", "bar"}).eval("../baz").toString() == "foo/baz");
  KJ_EXPECT(Path({"foo", "bar"}).eval("baz/../../qux/").toString() == "foo/qux");
  KJ_EXPECT(Path({"foo", "bar"}).eval("/baz/qux").toString() == "baz/qux");
  KJ_EXPECT(Path({"foo", "bar"}).eval("//baz/qux").toString() == "baz/qux");
  KJ_EXPECT(Path({"foo", "bar"}).eval("/baz/../qux").toString() == "qux");

  KJ_EXPECT(Path({"foo", "bar"}).basename()[0] == "bar");
  KJ_EXPECT(Path({"foo", "bar", "baz"}).parent().toString() == "foo/bar");

  KJ_EXPECT(Path({"foo", "bar"}).append("baz").toString() == "foo/bar/baz");
  KJ_EXPECT(Path({"foo", "bar"}).append(Path({"baz", "qux"})).toString() == "foo/bar/baz/qux");

  {
    // Test methods which are overloaded for && on a non-rvalue path.
    Path path({"foo", "bar"});
    KJ_EXPECT(path.eval("baz").toString() == "foo/bar/baz");
    KJ_EXPECT(path.eval("./baz").toString() == "foo/bar/baz");
    KJ_EXPECT(path.eval("baz/qux").toString() == "foo/bar/baz/qux");
    KJ_EXPECT(path.eval("baz//qux").toString() == "foo/bar/baz/qux");
    KJ_EXPECT(path.eval("baz/./qux").toString() == "foo/bar/baz/qux");
    KJ_EXPECT(path.eval("baz/../qux").toString() == "foo/bar/qux");
    KJ_EXPECT(path.eval("baz/qux/..").toString() == "foo/bar/baz");
    KJ_EXPECT(path.eval("../baz").toString() == "foo/baz");
    KJ_EXPECT(path.eval("baz/../../qux/").toString() == "foo/qux");
    KJ_EXPECT(path.eval("/baz/qux").toString() == "baz/qux");
    KJ_EXPECT(path.eval("/baz/../qux").toString() == "qux");

    KJ_EXPECT(path.basename()[0] == "bar");
    KJ_EXPECT(path.parent().toString() == "foo");

    KJ_EXPECT(path.append("baz").toString() == "foo/bar/baz");
    KJ_EXPECT(path.append(Path({"baz", "qux"})).toString() == "foo/bar/baz/qux");
  }

  KJ_EXPECT(kj::str(Path({"foo", "bar"})) == "foo/bar");
}

KJ_TEST("Path comparisons") {
  KJ_EXPECT(Path({"foo", "bar"}) == Path({"foo", "bar"}));
  KJ_EXPECT(!(Path({"foo", "bar"}) != Path({"foo", "bar"})));
  KJ_EXPECT(Path({"foo", "bar"}) != Path({"foo", "baz"}));
  KJ_EXPECT(!(Path({"foo", "bar"}) == Path({"foo", "baz"})));

  KJ_EXPECT(Path({"foo", "bar"}) != Path({"fob", "bar"}));
  KJ_EXPECT(Path({"foo", "bar"}) != Path({"foo", "bar", "baz"}));
  KJ_EXPECT(Path({"foo", "bar", "baz"}) != Path({"foo", "bar"}));

  KJ_EXPECT(Path({"foo", "bar"}) <= Path({"foo", "bar"}));
  KJ_EXPECT(Path({"foo", "bar"}) >= Path({"foo", "bar"}));
  KJ_EXPECT(!(Path({"foo", "bar"}) < Path({"foo", "bar"})));
  KJ_EXPECT(!(Path({"foo", "bar"}) > Path({"foo", "bar"})));

  KJ_EXPECT(Path({"foo", "bar"}) < Path({"foo", "bar", "baz"}));
  KJ_EXPECT(!(Path({"foo", "bar"}) > Path({"foo", "bar", "baz"})));
  KJ_EXPECT(Path({"foo", "bar", "baz"}) > Path({"foo", "bar"}));
  KJ_EXPECT(!(Path({"foo", "bar", "baz"}) < Path({"foo", "bar"})));

  KJ_EXPECT(Path({"foo", "bar"}) < Path({"foo", "baz"}));
  KJ_EXPECT(Path({"foo", "bar"}) > Path({"foo", "baa"}));
  KJ_EXPECT(Path({"foo", "bar"}) > Path({"foo"}));

  KJ_EXPECT(Path({"foo", "bar"}).startsWith(Path({})));
  KJ_EXPECT(Path({"foo", "bar"}).startsWith(Path({"foo"})));
  KJ_EXPECT(Path({"foo", "bar"}).startsWith(Path({"foo", "bar"})));
  KJ_EXPECT(!Path({"foo", "bar"}).startsWith(Path({"foo", "bar", "baz"})));
  KJ_EXPECT(!Path({"foo", "bar"}).startsWith(Path({"foo", "baz"})));
  KJ_EXPECT(!Path({"foo", "bar"}).startsWith(Path({"baz", "foo", "bar"})));
  KJ_EXPECT(!Path({"foo", "bar"}).startsWith(Path({"baz"})));

  KJ_EXPECT(Path({"foo", "bar"}).endsWith(Path({})));
  KJ_EXPECT(Path({"foo", "bar"}).endsWith(Path({"bar"})));
  KJ_EXPECT(Path({"foo", "bar"}).endsWith(Path({"foo", "bar"})));
  KJ_EXPECT(!Path({"foo", "bar"}).endsWith(Path({"baz", "foo", "bar"})));
  KJ_EXPECT(!Path({"foo", "bar"}).endsWith(Path({"fob", "bar"})));
  KJ_EXPECT(!Path({"foo", "bar"}).endsWith(Path({"foo", "bar", "baz"})));
  KJ_EXPECT(!Path({"foo", "bar"}).endsWith(Path({"baz"})));
}

KJ_TEST("Path exceptions") {
  KJ_EXPECT_THROW_MESSAGE("invalid path component", Path(""));
  KJ_EXPECT_THROW_MESSAGE("invalid path component", Path("."));
  KJ_EXPECT_THROW_MESSAGE("invalid path component", Path(".."));
  KJ_EXPECT_THROW_MESSAGE("NUL character", Path(StringPtr("foo\0bar", 7)));

  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("break out of starting", Path::parse(".."));
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("break out of starting", Path::parse("../foo"));
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("break out of starting", Path::parse("foo/../.."));
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("expected a relative path", Path::parse("/foo"));

  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("NUL character", Path::parse(kj::StringPtr("foo\0bar", 7)));

  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("break out of starting",
      Path({"foo", "bar"}).eval("../../.."));
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("break out of starting",
      Path({"foo", "bar"}).eval("../baz/../../.."));
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("break out of starting",
      Path({"foo", "bar"}).eval("baz/../../../.."));
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("break out of starting",
      Path({"foo", "bar"}).eval("/.."));
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("break out of starting",
      Path({"foo", "bar"}).eval("/baz/../.."));

  KJ_EXPECT_THROW_MESSAGE("root path has no basename", Path(nullptr).basename());
  KJ_EXPECT_THROW_MESSAGE("root path has no parent", Path(nullptr).parent());
}

constexpr kj::ArrayPtr<const wchar_t> operator "" _a(const wchar_t* str, size_t n) {
  return { str, n };
}

KJ_TEST("Win32 Path") {
  KJ_EXPECT(Path({"foo", "bar"}).toWin32String() == "foo\\bar");
  KJ_EXPECT(Path({"foo", "bar"}).toWin32String(true) == "\\\\foo\\bar");
  KJ_EXPECT(Path({"c:", "foo", "bar"}).toWin32String(true) == "c:\\foo\\bar");
  KJ_EXPECT(Path({"A:", "foo", "bar"}).toWin32String(true) == "A:\\foo\\bar");

  KJ_EXPECT(Path({"foo", "bar"}).evalWin32("baz").toWin32String() == "foo\\bar\\baz");
  KJ_EXPECT(Path({"foo", "bar"}).evalWin32("./baz").toWin32String() == "foo\\bar\\baz");
  KJ_EXPECT(Path({"foo", "bar"}).evalWin32("baz/qux").toWin32String() == "foo\\bar\\baz\\qux");
  KJ_EXPECT(Path({"foo", "bar"}).evalWin32("baz//qux").toWin32String() == "foo\\bar\\baz\\qux");
  KJ_EXPECT(Path({"foo", "bar"}).evalWin32("baz/./qux").toWin32String() == "foo\\bar\\baz\\qux");
  KJ_EXPECT(Path({"foo", "bar"}).evalWin32("baz/../qux").toWin32String() == "foo\\bar\\qux");
  KJ_EXPECT(Path({"foo", "bar"}).evalWin32("baz/qux/..").toWin32String() == "foo\\bar\\baz");
  KJ_EXPECT(Path({"foo", "bar"}).evalWin32("../baz").toWin32String() == "foo\\baz");
  KJ_EXPECT(Path({"foo", "bar"}).evalWin32("baz/../../qux/").toWin32String() == "foo\\qux");
  KJ_EXPECT(Path({"foo", "bar"}).evalWin32(".\\baz").toWin32String() == "foo\\bar\\baz");
  KJ_EXPECT(Path({"foo", "bar"}).evalWin32("baz\\qux").toWin32String() == "foo\\bar\\baz\\qux");
  KJ_EXPECT(Path({"foo", "bar"}).evalWin32("baz\\\\qux").toWin32String() == "foo\\bar\\baz\\qux");
  KJ_EXPECT(Path({"foo", "bar"}).evalWin32("baz\\.\\qux").toWin32String() == "foo\\bar\\baz\\qux");
  KJ_EXPECT(Path({"foo", "bar"}).evalWin32("baz\\..\\qux").toWin32String() == "foo\\bar\\qux");
  KJ_EXPECT(Path({"foo", "bar"}).evalWin32("baz\\qux\\..").toWin32String() == "foo\\bar\\baz");
  KJ_EXPECT(Path({"foo", "bar"}).evalWin32("..\\baz").toWin32String() == "foo\\baz");
  KJ_EXPECT(Path({"foo", "bar"}).evalWin32("baz\\..\\..\\qux\\").toWin32String() == "foo\\qux");
  KJ_EXPECT(Path({"foo", "bar"}).evalWin32("baz\\../..\\qux/").toWin32String() == "foo\\qux");

  KJ_EXPECT(Path({"c:", "foo", "bar"}).evalWin32("/baz/qux")
      .toWin32String(true) == "c:\\baz\\qux");
  KJ_EXPECT(Path({"c:", "foo", "bar"}).evalWin32("\\baz\\qux")
      .toWin32String(true) == "c:\\baz\\qux");
  KJ_EXPECT(Path({"c:", "foo", "bar"}).evalWin32("d:\\baz\\qux")
      .toWin32String(true) == "d:\\baz\\qux");
  KJ_EXPECT(Path({"c:", "foo", "bar"}).evalWin32("d:\\baz\\..\\qux")
      .toWin32String(true) == "d:\\qux");
  KJ_EXPECT(Path({"c:", "foo", "bar"}).evalWin32("\\\\baz\\qux")
      .toWin32String(true) == "\\\\baz\\qux");
  KJ_EXPECT(Path({"foo", "bar"}).evalWin32("d:\\baz\\..\\qux")
      .toWin32String(true) == "d:\\qux");
  KJ_EXPECT(Path({"foo", "bar", "baz"}).evalWin32("\\qux")
      .toWin32String(true) == "\\\\foo\\bar\\qux");

  KJ_EXPECT(Path({"foo", "bar"}).forWin32Api(false) == L"foo\\bar");
  KJ_EXPECT(Path({"foo", "bar"}).forWin32Api(true) == L"\\\\?\\UNC\\foo\\bar");
  KJ_EXPECT(Path({"c:", "foo", "bar"}).forWin32Api(true) == L"\\\\?\\c:\\foo\\bar");
  KJ_EXPECT(Path({"A:", "foo", "bar"}).forWin32Api(true) == L"\\\\?\\A:\\foo\\bar");

  KJ_EXPECT(Path::parseWin32Api(L"\\\\?\\c:\\foo\\bar"_a).toString() == "c:/foo/bar");
  KJ_EXPECT(Path::parseWin32Api(L"\\\\?\\UNC\\foo\\bar"_a).toString() == "foo/bar");
  KJ_EXPECT(Path::parseWin32Api(L"c:\\foo\\bar"_a).toString() == "c:/foo/bar");
  KJ_EXPECT(Path::parseWin32Api(L"\\\\foo\\bar"_a).toString() == "foo/bar");
}

KJ_TEST("Win32 Path exceptions") {
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("colons are prohibited",
      Path({"c:", "foo", "bar"}).toWin32String());
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("colons are prohibited",
      Path({"c:", "foo:bar"}).toWin32String(true));
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("DOS reserved name", Path({"con"}).toWin32String());
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("DOS reserved name", Path({"CON", "bar"}).toWin32String());
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("DOS reserved name", Path({"foo", "cOn"}).toWin32String());
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("DOS reserved name", Path({"prn"}).toWin32String());
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("DOS reserved name", Path({"aux"}).toWin32String());
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("DOS reserved name", Path({"NUL"}).toWin32String());
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("DOS reserved name", Path({"nul.txt"}).toWin32String());
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("DOS reserved name", Path({"com3"}).toWin32String());
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("DOS reserved name", Path({"lpt9"}).toWin32String());
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("DOS reserved name", Path({"com1.hello"}).toWin32String());

  KJ_EXPECT_THROW_MESSAGE("drive letter or netbios", Path({"?", "foo"}).toWin32String(true));

  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("break out of starting",
      Path({"foo", "bar"}).evalWin32("../../.."));
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("break out of starting",
      Path({"foo", "bar"}).evalWin32("../baz/../../.."));
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("break out of starting",
      Path({"foo", "bar"}).evalWin32("baz/../../../.."));
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("break out of starting",
      Path({"foo", "bar"}).evalWin32("c:\\..\\.."));
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("break out of starting",
      Path({"c:", "foo", "bar"}).evalWin32("/baz/../../.."));
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("must specify drive letter",
      Path({"foo"}).evalWin32("\\baz\\qux"));
}

KJ_TEST("WriteMode operators") {
  WriteMode createOrModify = WriteMode::CREATE | WriteMode::MODIFY;

  KJ_EXPECT(has(createOrModify, WriteMode::MODIFY));
  KJ_EXPECT(has(createOrModify, WriteMode::CREATE));
  KJ_EXPECT(!has(createOrModify, WriteMode::CREATE_PARENT));
  KJ_EXPECT(has(createOrModify, createOrModify));
  KJ_EXPECT(!has(createOrModify, createOrModify | WriteMode::CREATE_PARENT));
  KJ_EXPECT(!has(createOrModify, WriteMode::CREATE | WriteMode::CREATE_PARENT));
  KJ_EXPECT(!has(WriteMode::CREATE, createOrModify));

  KJ_EXPECT(createOrModify != WriteMode::MODIFY);
  KJ_EXPECT(createOrModify != WriteMode::CREATE);

  KJ_EXPECT(createOrModify - WriteMode::CREATE == WriteMode::MODIFY);
  KJ_EXPECT(WriteMode::CREATE + WriteMode::MODIFY == createOrModify);

  // Adding existing bit / subtracting non-existing bit are no-ops.
  KJ_EXPECT(createOrModify + WriteMode::MODIFY == createOrModify);
  KJ_EXPECT(createOrModify - WriteMode::CREATE_PARENT == createOrModify);
}

// ======================================================================================

class TestClock final: public Clock {
public:
  void tick() {
    time += 1 * SECONDS;
  }

  Date now() const override { return time; }

  void expectChanged(const FsNode& file) {
    KJ_EXPECT(file.stat().lastModified == time);
    time += 1 * SECONDS;
  }
  void expectUnchanged(const FsNode& file) {
    KJ_EXPECT(file.stat().lastModified != time);
  }

private:
  Date time = UNIX_EPOCH + 1 * SECONDS;
};

KJ_TEST("InMemoryFile") {
  TestClock clock;

  auto file = newInMemoryFile(clock);
  clock.expectChanged(*file);

  KJ_EXPECT(file->readAllText() == "");
  clock.expectUnchanged(*file);

  file->writeAll("foo");
  clock.expectChanged(*file);
  KJ_EXPECT(file->readAllText() == "foo");

  file->write(3, StringPtr("bar").asBytes());
  clock.expectChanged(*file);
  KJ_EXPECT(file->readAllText() == "foobar");

  file->write(3, StringPtr("baz").asBytes());
  clock.expectChanged(*file);
  KJ_EXPECT(file->readAllText() == "foobaz");

  file->write(9, StringPtr("qux").asBytes());
  clock.expectChanged(*file);
  KJ_EXPECT(file->readAllText() == kj::StringPtr("foobaz\0\0\0qux", 12));

  file->truncate(6);
  clock.expectChanged(*file);
  KJ_EXPECT(file->readAllText() == "foobaz");

  file->truncate(18);
  clock.expectChanged(*file);
  KJ_EXPECT(file->readAllText() == kj::StringPtr("foobaz\0\0\0\0\0\0\0\0\0\0\0\0", 18));

  {
    auto mapping = file->mmap(0, 18);
    auto privateMapping = file->mmapPrivate(0, 18);
    auto writableMapping = file->mmapWritable(0, 18);
    clock.expectUnchanged(*file);

    KJ_EXPECT(mapping.size() == 18);
    KJ_EXPECT(privateMapping.size() == 18);
    KJ_EXPECT(writableMapping->get().size() == 18);
    clock.expectUnchanged(*file);

    KJ_EXPECT(writableMapping->get().begin() == mapping.begin());
    KJ_EXPECT(privateMapping.begin() != mapping.begin());

    KJ_EXPECT(kj::str(mapping.slice(0, 6).asChars()) == "foobaz");
    KJ_EXPECT(kj::str(privateMapping.slice(0, 6).asChars()) == "foobaz");
    clock.expectUnchanged(*file);

    file->write(0, StringPtr("qux").asBytes());
    clock.expectChanged(*file);
    KJ_EXPECT(kj::str(mapping.slice(0, 6).asChars()) == "quxbaz");
    KJ_EXPECT(kj::str(privateMapping.slice(0, 6).asChars()) == "foobaz");

    file->write(12, StringPtr("corge").asBytes());
    KJ_EXPECT(kj::str(mapping.slice(12, 17).asChars()) == "corge");

    // Can shrink.
    file->truncate(6);
    KJ_EXPECT(kj::str(mapping.slice(12, 17).asChars()) == kj::StringPtr("\0\0\0\0\0", 5));

    // Can regrow.
    file->truncate(18);
    KJ_EXPECT(kj::str(mapping.slice(12, 17).asChars()) == kj::StringPtr("\0\0\0\0\0", 5));

    // Can't grow past previoous capacity.
    KJ_EXPECT_THROW_MESSAGE("cannot resize the file backing store", file->truncate(100));

    clock.expectChanged(*file);
    writableMapping->changed(writableMapping->get().slice(0, 3));
    clock.expectChanged(*file);
    writableMapping->sync(writableMapping->get().slice(0, 3));
    clock.expectChanged(*file);
  }

  // But now we can since the mapping is gone.
  file->truncate(100);

  file->truncate(6);
  clock.expectChanged(*file);

  KJ_EXPECT(file->readAllText() == "quxbaz");
  file->zero(3, 3);
  clock.expectChanged(*file);
  KJ_EXPECT(file->readAllText() == StringPtr("qux\0\0\0", 6));
}

KJ_TEST("InMemoryFile::copy()") {
  TestClock clock;

  auto source = newInMemoryFile(clock);
  source->writeAll("foobarbaz");

  auto dest = newInMemoryFile(clock);
  dest->writeAll("quxcorge");
  clock.expectChanged(*dest);

  KJ_EXPECT(dest->copy(3, *source, 6, kj::maxValue) == 3);
  clock.expectChanged(*dest);
  KJ_EXPECT(dest->readAllText() == "quxbazge");

  KJ_EXPECT(dest->copy(0, *source, 3, 4) == 4);
  clock.expectChanged(*dest);
  KJ_EXPECT(dest->readAllText() == "barbazge");

  KJ_EXPECT(dest->copy(0, *source, 128, kj::maxValue) == 0);
  clock.expectUnchanged(*dest);

  KJ_EXPECT(dest->copy(4, *source, 3, 0) == 0);
  clock.expectUnchanged(*dest);

  String bigString = strArray(repeat("foobar", 10000), "");
  source->truncate(bigString.size() + 1000);
  source->write(123, bigString.asBytes());

  dest->copy(321, *source, 123, bigString.size());
  KJ_EXPECT(dest->readAllText().slice(321) == bigString);
}

KJ_TEST("File::copy()") {
  TestClock clock;

  auto source = newInMemoryFile(clock);
  source->writeAll("foobarbaz");

  auto dest = newInMemoryFile(clock);
  dest->writeAll("quxcorge");
  clock.expectChanged(*dest);

  KJ_EXPECT(dest->File::copy(3, *source, 6, kj::maxValue) == 3);
  clock.expectChanged(*dest);
  KJ_EXPECT(dest->readAllText() == "quxbazge");

  KJ_EXPECT(dest->File::copy(0, *source, 3, 4) == 4);
  clock.expectChanged(*dest);
  KJ_EXPECT(dest->readAllText() == "barbazge");

  KJ_EXPECT(dest->File::copy(0, *source, 128, kj::maxValue) == 0);
  clock.expectUnchanged(*dest);

  KJ_EXPECT(dest->File::copy(4, *source, 3, 0) == 0);
  clock.expectUnchanged(*dest);

  String bigString = strArray(repeat("foobar", 10000), "");
  source->truncate(bigString.size() + 1000);
  source->write(123, bigString.asBytes());

  dest->File::copy(321, *source, 123, bigString.size());
  KJ_EXPECT(dest->readAllText().slice(321) == bigString);
}

KJ_TEST("InMemoryDirectory") {
  TestClock clock;

  auto dir = newInMemoryDirectory(clock);
  clock.expectChanged(*dir);

  KJ_EXPECT(dir->listNames() == nullptr);
  KJ_EXPECT(dir->listEntries() == nullptr);
  KJ_EXPECT(!dir->exists(Path("foo")));
  KJ_EXPECT(dir->tryOpenFile(Path("foo")) == nullptr);
  KJ_EXPECT(dir->tryOpenFile(Path("foo"), WriteMode::MODIFY) == nullptr);
  clock.expectUnchanged(*dir);

  {
    auto file = dir->openFile(Path("foo"), WriteMode::CREATE);
    clock.expectChanged(*dir);
    file->writeAll("foobar");
    clock.expectUnchanged(*dir);
  }
  clock.expectUnchanged(*dir);

  KJ_EXPECT(dir->exists(Path("foo")));
  clock.expectUnchanged(*dir);

  {
    auto stats = dir->lstat(Path("foo"));
    clock.expectUnchanged(*dir);
    KJ_EXPECT(stats.type == FsNode::Type::FILE);
    KJ_EXPECT(stats.size == 6);
  }

  {
    auto list = dir->listNames();
    clock.expectUnchanged(*dir);
    KJ_ASSERT(list.size() == 1);
    KJ_EXPECT(list[0] == "foo");
  }

  {
    auto list = dir->listEntries();
    clock.expectUnchanged(*dir);
    KJ_ASSERT(list.size() == 1);
    KJ_EXPECT(list[0].name == "foo");
    KJ_EXPECT(list[0].type == FsNode::Type::FILE);
  }

  KJ_EXPECT(dir->openFile(Path("foo"))->readAllText() == "foobar");
  clock.expectUnchanged(*dir);

  KJ_EXPECT(dir->tryOpenFile(Path({"foo", "bar"}), WriteMode::MODIFY) == nullptr);
  KJ_EXPECT(dir->tryOpenFile(Path({"bar", "baz"}), WriteMode::MODIFY) == nullptr);
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("parent is not a directory",
      dir->tryOpenFile(Path({"bar", "baz"}), WriteMode::CREATE));
  clock.expectUnchanged(*dir);

  {
    auto file = dir->openFile(Path({"bar", "baz"}), WriteMode::CREATE | WriteMode::CREATE_PARENT);
    clock.expectChanged(*dir);
    file->writeAll("bazqux");
    clock.expectUnchanged(*dir);
  }
  clock.expectUnchanged(*dir);

  KJ_EXPECT(dir->openFile(Path({"bar", "baz"}))->readAllText() == "bazqux");
  clock.expectUnchanged(*dir);

  {
    auto stats = dir->lstat(Path("bar"));
    clock.expectUnchanged(*dir);
    KJ_EXPECT(stats.type == FsNode::Type::DIRECTORY);
  }

  {
    auto list = dir->listNames();
    clock.expectUnchanged(*dir);
    KJ_ASSERT(list.size() == 2);
    KJ_EXPECT(list[0] == "bar");
    KJ_EXPECT(list[1] == "foo");
  }

  {
    auto list = dir->listEntries();
    clock.expectUnchanged(*dir);
    KJ_ASSERT(list.size() == 2);
    KJ_EXPECT(list[0].name == "bar");
    KJ_EXPECT(list[0].type == FsNode::Type::DIRECTORY);
    KJ_EXPECT(list[1].name == "foo");
    KJ_EXPECT(list[1].type == FsNode::Type::FILE);
  }

  {
    auto subdir = dir->openSubdir(Path("bar"));
    clock.expectUnchanged(*dir);
    clock.expectUnchanged(*subdir);

    KJ_EXPECT(subdir->openFile(Path("baz"))->readAllText() == "bazqux");
    clock.expectUnchanged(*subdir);
  }

  auto subdir = dir->openSubdir(Path("corge"), WriteMode::CREATE);
  clock.expectChanged(*dir);

  subdir->openFile(Path("grault"), WriteMode::CREATE)->writeAll("garply");
  clock.expectUnchanged(*dir);
  clock.expectChanged(*subdir);

  KJ_EXPECT(dir->openFile(Path({"corge", "grault"}))->readAllText() == "garply");

  dir->openFile(Path({"corge", "grault"}), WriteMode::CREATE | WriteMode::MODIFY)
     ->write(0, StringPtr("rag").asBytes());
  KJ_EXPECT(dir->openFile(Path({"corge", "grault"}))->readAllText() == "ragply");
  clock.expectUnchanged(*dir);

  {
    auto replacer =
        dir->replaceFile(Path({"corge", "grault"}), WriteMode::CREATE | WriteMode::MODIFY);
    clock.expectUnchanged(*subdir);
    replacer->get().writeAll("rag");
    clock.expectUnchanged(*subdir);
    // Don't commit.
  }
  clock.expectUnchanged(*subdir);
  KJ_EXPECT(dir->openFile(Path({"corge", "grault"}))->readAllText() == "ragply");

  {
    auto replacer =
        dir->replaceFile(Path({"corge", "grault"}), WriteMode::CREATE | WriteMode::MODIFY);
    clock.expectUnchanged(*subdir);
    replacer->get().writeAll("rag");
    clock.expectUnchanged(*subdir);
    replacer->commit();
    clock.expectChanged(*subdir);
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
  clock.expectUnchanged(*dir);
  dir->remove(Path("foo"));
  clock.expectChanged(*dir);
  KJ_EXPECT(!dir->exists(Path("foo")));
  KJ_EXPECT(!dir->tryRemove(Path("foo")));
  clock.expectUnchanged(*dir);

  KJ_EXPECT(dir->exists(Path({"bar", "baz"})));
  clock.expectUnchanged(*dir);
  dir->remove(Path({"bar", "baz"}));
  clock.expectUnchanged(*dir);
  KJ_EXPECT(!dir->exists(Path({"bar", "baz"})));
  KJ_EXPECT(dir->exists(Path("bar")));
  KJ_EXPECT(!dir->tryRemove(Path({"bar", "baz"})));
  clock.expectUnchanged(*dir);

  KJ_EXPECT(dir->exists(Path("corge")));
  KJ_EXPECT(dir->exists(Path({"corge", "grault"})));
  clock.expectUnchanged(*dir);
  dir->remove(Path("corge"));
  clock.expectChanged(*dir);
  KJ_EXPECT(!dir->exists(Path("corge")));
  KJ_EXPECT(!dir->exists(Path({"corge", "grault"})));
  KJ_EXPECT(!dir->tryRemove(Path("corge")));
  clock.expectUnchanged(*dir);
}

KJ_TEST("InMemoryDirectory symlinks") {
  TestClock clock;

  auto dir = newInMemoryDirectory(clock);
  clock.expectChanged(*dir);

  dir->symlink(Path("foo"), "bar/qux/../baz", WriteMode::CREATE);
  clock.expectChanged(*dir);

  KJ_EXPECT(!dir->trySymlink(Path("foo"), "bar/qux/../baz", WriteMode::CREATE));
  clock.expectUnchanged(*dir);

  {
    auto stats = dir->lstat(Path("foo"));
    clock.expectUnchanged(*dir);
    KJ_EXPECT(stats.type == FsNode::Type::SYMLINK);
  }

  KJ_EXPECT(dir->readlink(Path("foo")) == "bar/qux/../baz");

  // Broken link into non-existing directory cannot be opened in any mode.
  KJ_EXPECT(dir->tryOpenFile(Path("foo")) == nullptr);
  KJ_EXPECT(dir->tryOpenFile(Path("foo"), WriteMode::CREATE) == nullptr);
  KJ_EXPECT(dir->tryOpenFile(Path("foo"), WriteMode::MODIFY) == nullptr);
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("parent is not a directory",
      dir->tryOpenFile(Path("foo"), WriteMode::CREATE | WriteMode::MODIFY));
  KJ_EXPECT_THROW_RECOVERABLE_MESSAGE("parent is not a directory",
      dir->tryOpenFile(Path("foo"),
          WriteMode::CREATE | WriteMode::MODIFY | WriteMode::CREATE_PARENT));

  // Create the directory.
  auto subdir = dir->openSubdir(Path("bar"), WriteMode::CREATE);
  clock.expectChanged(*dir);

  // Link still points to non-existing file so cannot be open in most modes.
  KJ_EXPECT(dir->tryOpenFile(Path("foo")) == nullptr);
  KJ_EXPECT(dir->tryOpenFile(Path("foo"), WriteMode::CREATE) == nullptr);
  KJ_EXPECT(dir->tryOpenFile(Path("foo"), WriteMode::MODIFY) == nullptr);
  clock.expectUnchanged(*dir);

  // But... CREATE | MODIFY works.
  dir->openFile(Path("foo"), WriteMode::CREATE | WriteMode::MODIFY)
     ->writeAll("foobar");
  clock.expectUnchanged(*dir);  // Change is only to subdir!

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

KJ_TEST("InMemoryDirectory link") {
  TestClock clock;

  auto src = newInMemoryDirectory(clock);
  auto dst = newInMemoryDirectory(clock);

  src->openFile(Path({"foo", "bar"}), WriteMode::CREATE | WriteMode::CREATE_PARENT)
     ->writeAll("foobar");
  src->openFile(Path({"foo", "baz", "qux"}), WriteMode::CREATE | WriteMode::CREATE_PARENT)
     ->writeAll("bazqux");
  clock.expectChanged(*src);
  clock.expectUnchanged(*dst);

  dst->transfer(Path("link"), WriteMode::CREATE, *src, Path("foo"), TransferMode::LINK);
  clock.expectUnchanged(*src);
  clock.expectChanged(*dst);

  KJ_EXPECT(dst->openFile(Path({"link", "bar"}))->readAllText() == "foobar");
  KJ_EXPECT(dst->openFile(Path({"link", "baz", "qux"}))->readAllText() == "bazqux");

  KJ_EXPECT(dst->exists(Path({"link", "bar"})));
  src->remove(Path({"foo", "bar"}));
  KJ_EXPECT(!dst->exists(Path({"link", "bar"})));
}

KJ_TEST("InMemoryDirectory copy") {
  TestClock clock;

  auto src = newInMemoryDirectory(clock);
  auto dst = newInMemoryDirectory(clock);

  src->openFile(Path({"foo", "bar"}), WriteMode::CREATE | WriteMode::CREATE_PARENT)
     ->writeAll("foobar");
  src->openFile(Path({"foo", "baz", "qux"}), WriteMode::CREATE | WriteMode::CREATE_PARENT)
     ->writeAll("bazqux");
  clock.expectChanged(*src);
  clock.expectUnchanged(*dst);

  dst->transfer(Path("link"), WriteMode::CREATE, *src, Path("foo"), TransferMode::COPY);
  clock.expectUnchanged(*src);
  clock.expectChanged(*dst);

  KJ_EXPECT(src->openFile(Path({"foo", "bar"}))->readAllText() == "foobar");
  KJ_EXPECT(src->openFile(Path({"foo", "baz", "qux"}))->readAllText() == "bazqux");
  KJ_EXPECT(dst->openFile(Path({"link", "bar"}))->readAllText() == "foobar");
  KJ_EXPECT(dst->openFile(Path({"link", "baz", "qux"}))->readAllText() == "bazqux");

  KJ_EXPECT(dst->exists(Path({"link", "bar"})));
  src->remove(Path({"foo", "bar"}));
  KJ_EXPECT(dst->openFile(Path({"link", "bar"}))->readAllText() == "foobar");
}

KJ_TEST("InMemoryDirectory move") {
  TestClock clock;

  auto src = newInMemoryDirectory(clock);
  auto dst = newInMemoryDirectory(clock);

  src->openFile(Path({"foo", "bar"}), WriteMode::CREATE | WriteMode::CREATE_PARENT)
     ->writeAll("foobar");
  src->openFile(Path({"foo", "baz", "qux"}), WriteMode::CREATE | WriteMode::CREATE_PARENT)
     ->writeAll("bazqux");
  clock.expectChanged(*src);
  clock.expectUnchanged(*dst);

  dst->transfer(Path("link"), WriteMode::CREATE, *src, Path("foo"), TransferMode::MOVE);
  clock.expectChanged(*src);

  KJ_EXPECT(!src->exists(Path({"foo"})));
  KJ_EXPECT(dst->openFile(Path({"link", "bar"}))->readAllText() == "foobar");
  KJ_EXPECT(dst->openFile(Path({"link", "baz", "qux"}))->readAllText() == "bazqux");
}

KJ_TEST("InMemoryDirectory createTemporary") {
  TestClock clock;

  auto dir = newInMemoryDirectory(clock);
  auto file = dir->createTemporary();
  file->writeAll("foobar");
  KJ_EXPECT(file->readAllText() == "foobar");
  KJ_EXPECT(dir->listNames() == nullptr);
}

}  // namespace
}  // namespace kj
