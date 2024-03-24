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

#if !_WIN32

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
// Request 64-bit off_t. (The code will still work if we get 32-bit off_t as long as actual files
// are under 4GB.)
#endif

#include "filesystem.h"
#include "debug.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/mman.h>
#include <errno.h>
#include <dirent.h>
#include <stdlib.h>
#include "vector.h"
#include "miniposix.h"
#include <algorithm>

#if __linux__
#include <syscall.h>
#include <linux/fs.h>
#include <sys/sendfile.h>
#endif

namespace kj {
namespace {

#define HIDDEN_PREFIX ".kj-tmp."
// Prefix for temp files which should be hidden when listing a directory.
//
// If you change this, make sure to update the unit test.

#ifdef O_CLOEXEC
#define MAYBE_O_CLOEXEC O_CLOEXEC
#else
#define MAYBE_O_CLOEXEC 0
#endif

#ifdef O_DIRECTORY
#define MAYBE_O_DIRECTORY O_DIRECTORY
#else
#define MAYBE_O_DIRECTORY 0
#endif

#if __APPLE__
// Mac OSX defines SEEK_HOLE, but it doesn't work. ("Inappropriate ioctl for device", it says.)
#undef SEEK_HOLE
#endif

#if __BIONIC__
// No no DTTOIF function
#undef DT_UNKNOWN
#endif

static void setCloexec(int fd) KJ_UNUSED;
static void setCloexec(int fd) {
  // Set the O_CLOEXEC flag on the given fd.
  //
  // We try to avoid the need to call this by taking advantage of syscall flags that set it
  // atomically on new file descriptors. Unfortunately some platforms do not support such syscalls.

#ifdef FIOCLEX
  // Yay, we can set the flag in one call.
  KJ_SYSCALL_HANDLE_ERRORS(ioctl(fd, FIOCLEX)) {
    case EINVAL:
    case EOPNOTSUPP:
      break;
    default:
      KJ_FAIL_SYSCALL("ioctl(fd, FIOCLEX)", error) { break; }
      break;
  } else {
    // success
    return;
  }
#endif

  // Sadness, we must resort to read/modify/write.
  //
  // (On many platforms, FD_CLOEXEC is the only flag modifiable via F_SETFD and therefore we could
  // skip the read... but it seems dangerous to assume that's true of all platforms, and anyway
  // most platforms support FIOCLEX.)
  int flags;
  KJ_SYSCALL(flags = fcntl(fd, F_GETFD));
  if (!(flags & FD_CLOEXEC)) {
    KJ_SYSCALL(fcntl(fd, F_SETFD, flags | FD_CLOEXEC));
  }
}

static Date toKjDate(struct timespec tv) {
  return tv.tv_sec * SECONDS + tv.tv_nsec * NANOSECONDS + UNIX_EPOCH;
}

static FsNode::Type modeToType(mode_t mode) {
  switch (mode & S_IFMT) {
    case S_IFREG : return FsNode::Type::FILE;
    case S_IFDIR : return FsNode::Type::DIRECTORY;
    case S_IFLNK : return FsNode::Type::SYMLINK;
    case S_IFBLK : return FsNode::Type::BLOCK_DEVICE;
    case S_IFCHR : return FsNode::Type::CHARACTER_DEVICE;
    case S_IFIFO : return FsNode::Type::NAMED_PIPE;
    case S_IFSOCK: return FsNode::Type::SOCKET;
    default: return FsNode::Type::OTHER;
  }
}

static FsNode::Metadata statToMetadata(struct stat& stats) {
  // Probably st_ino and st_dev are usually under 32 bits, so mix by rotating st_dev left 32 bits
  // and XOR.
  uint64_t d = stats.st_dev;
  uint64_t hash = ((d << 32) | (d >> 32)) ^ stats.st_ino;

  return FsNode::Metadata {
    modeToType(stats.st_mode),
    implicitCast<uint64_t>(stats.st_size),
    implicitCast<uint64_t>(stats.st_blocks * 512u),
#if __APPLE__
    toKjDate(stats.st_mtimespec),
#else
    toKjDate(stats.st_mtim),
#endif
    implicitCast<uint>(stats.st_nlink),
    hash
  };
}

static bool rmrf(int fd, StringPtr path);

static void rmrfChildrenAndClose(int fd) {
  // Assumes fd is seeked to beginning.

  DIR* dir = fdopendir(fd);
  if (dir == nullptr) {
    close(fd);
    KJ_FAIL_SYSCALL("fdopendir", errno);
  };
  KJ_DEFER(closedir(dir));

  for (;;) {
    errno = 0;
    struct dirent* entry = readdir(dir);
    if (entry == nullptr) {
      int error = errno;
      if (error == 0) {
        break;
      } else {
        KJ_FAIL_SYSCALL("readdir", error);
      }
    }

    if (entry->d_name[0] == '.' &&
        (entry->d_name[1] == '\0' ||
         (entry->d_name[1] == '.' &&
          entry->d_name[2] == '\0'))) {
      // ignore . and ..
    } else {
#ifdef DT_UNKNOWN    // d_type is not available on all platforms.
      if (entry->d_type == DT_DIR) {
        int subdirFd;
        KJ_SYSCALL(subdirFd = openat(
            fd, entry->d_name, O_RDONLY | MAYBE_O_DIRECTORY | MAYBE_O_CLOEXEC | O_NOFOLLOW));
        rmrfChildrenAndClose(subdirFd);
        KJ_SYSCALL(unlinkat(fd, entry->d_name, AT_REMOVEDIR));
      } else if (entry->d_type != DT_UNKNOWN) {
        KJ_SYSCALL(unlinkat(fd, entry->d_name, 0));
      } else {
#endif
        KJ_ASSERT(rmrf(fd, entry->d_name));
#ifdef DT_UNKNOWN
      }
#endif
    }
  }
}

static bool rmrf(int fd, StringPtr path) {
  struct stat stats;
  KJ_SYSCALL_HANDLE_ERRORS(fstatat(fd, path.cStr(), &stats, AT_SYMLINK_NOFOLLOW)) {
    case ENOENT:
    case ENOTDIR:
      // Doesn't exist.
      return false;
    default:
      KJ_FAIL_SYSCALL("lstat(path)", error, path) { return false; }
  }

  if (S_ISDIR(stats.st_mode)) {
    int subdirFd;
    KJ_SYSCALL(subdirFd = openat(
        fd, path.cStr(), O_RDONLY | MAYBE_O_DIRECTORY | MAYBE_O_CLOEXEC | O_NOFOLLOW)) {
      return false;
    }
    rmrfChildrenAndClose(subdirFd);
    KJ_SYSCALL(unlinkat(fd, path.cStr(), AT_REMOVEDIR)) { return false; }
  } else {
    KJ_SYSCALL(unlinkat(fd, path.cStr(), 0)) { return false; }
  }

  return true;
}

struct MmapRange {
  uint64_t offset;
  uint64_t size;
};

static MmapRange getMmapRange(uint64_t offset, uint64_t size) {
  // Comes up with an offset and size to pass to mmap(), given an offset and size requested by
  // the caller, and considering the fact that mappings must start at a page boundary.
  //
  // The offset is rounded down to the nearest page boundary, and the size is increased to
  // compensate. Note that the endpoint of the mapping is *not* rounded up to a page boundary, as
  // mmap() does not actually require this, and it causes trouble on some systems (notably Cygwin).

#ifndef _SC_PAGESIZE
#define _SC_PAGESIZE _SC_PAGE_SIZE
#endif
  static const uint64_t pageSize = sysconf(_SC_PAGESIZE);
  uint64_t pageMask = pageSize - 1;

  uint64_t realOffset = offset & ~pageMask;

  return { realOffset, offset + size - realOffset };
}

class MmapDisposer: public ArrayDisposer {
protected:
  void disposeImpl(void* firstElement, size_t elementSize, size_t elementCount,
                   size_t capacity, void (*destroyElement)(void*)) const {
    auto range = getMmapRange(reinterpret_cast<uintptr_t>(firstElement),
                              elementSize * elementCount);
    KJ_SYSCALL(munmap(reinterpret_cast<byte*>(range.offset), range.size)) { break; }
  }
};

constexpr MmapDisposer mmapDisposer = MmapDisposer();

class DiskHandle {
  // We need to implement each of ReadableFile, AppendableFile, File, ReadableDirectory, and
  // Directory for disk handles. There is a lot of implementation overlap between these, especially
  // stat(), sync(), etc. We can't have everything inherit from a common DiskFsNode that implements
  // these because then we get diamond inheritance which means we need to make all our inheritance
  // virtual which means downcasting requires RTTI which violates our goal of supporting compiling
  // with no RTTI. So instead we have the DiskHandle class which implements all the methods without
  // inheriting anything, and then we have DiskFile, DiskDirectory, etc. hold this and delegate to
  // it. Ugly, but works.

public:
  DiskHandle(AutoCloseFd&& fd): fd(kj::mv(fd)) {}

  // OsHandle ------------------------------------------------------------------

  AutoCloseFd clone() const {
    int fd2;
#ifdef F_DUPFD_CLOEXEC
    KJ_SYSCALL_HANDLE_ERRORS(fd2 = fcntl(fd, F_DUPFD_CLOEXEC, 3)) {
      case EINVAL:
      case EOPNOTSUPP:
        // fall back
        break;
      default:
        KJ_FAIL_SYSCALL("fnctl(fd, F_DUPFD_CLOEXEC, 3)", error) { break; }
        break;
    } else {
      return AutoCloseFd(fd2);
    }
#endif

    KJ_SYSCALL(fd2 = ::dup(fd));
    AutoCloseFd result(fd2);
    setCloexec(result);
    return result;
  }

  int getFd() const {
    return fd.get();
  }

  void setFd(AutoCloseFd newFd) {
    // Used for one hack in DiskFilesystem's constructor...
    fd = kj::mv(newFd);
  }

  // FsNode --------------------------------------------------------------------

  FsNode::Metadata stat() const {
    struct stat stats;
    KJ_SYSCALL(::fstat(fd, &stats));
    return statToMetadata(stats);
  }

  void sync() const {
#if __APPLE__
    // For whatever reason, fsync() on OSX only flushes kernel buffers. It does not flush hardware
    // disk buffers. This makes it not very useful. But OSX documents fcntl F_FULLFSYNC which does
    // the right thing. Why they don't just make fsync() do the right thing, I do not know.
    KJ_SYSCALL(fcntl(fd, F_FULLFSYNC));
#else
    KJ_SYSCALL(fsync(fd));
#endif
  }

  void datasync() const {
    // The presence of the _POSIX_SYNCHRONIZED_IO define is supposed to tell us that fdatasync()
    // exists. But Apple defines this yet doesn't offer fdatasync(). Thanks, Apple.
#if _POSIX_SYNCHRONIZED_IO && !__APPLE__
    KJ_SYSCALL(fdatasync(fd));
#else
    this->sync();
#endif
  }

  // ReadableFile --------------------------------------------------------------

  size_t read(uint64_t offset, ArrayPtr<byte> buffer) const {
    // pread() probably never returns short reads unless it hits EOF. Unfortunately, though, per
    // spec we are not allowed to assume this.

    size_t total = 0;
    while (buffer.size() > 0) {
      ssize_t n;
      KJ_SYSCALL(n = pread(fd, buffer.begin(), buffer.size(), offset));
      if (n == 0) break;
      total += n;
      offset += n;
      buffer = buffer.slice(n, buffer.size());
    }
    return total;
  }

  Array<const byte> mmap(uint64_t offset, uint64_t size) const {
    if (size == 0) return nullptr;  // zero-length mmap() returns EINVAL, so avoid it
    auto range = getMmapRange(offset, size);
    const void* mapping = ::mmap(NULL, range.size, PROT_READ, MAP_SHARED, fd, range.offset);
    if (mapping == MAP_FAILED) {
      KJ_FAIL_SYSCALL("mmap", errno);
    }
    return Array<const byte>(reinterpret_cast<const byte*>(mapping) + (offset - range.offset),
                             size, mmapDisposer);
  }

  Array<byte> mmapPrivate(uint64_t offset, uint64_t size) const {
    if (size == 0) return nullptr;  // zero-length mmap() returns EINVAL, so avoid it
    auto range = getMmapRange(offset, size);
    void* mapping = ::mmap(NULL, range.size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, range.offset);
    if (mapping == MAP_FAILED) {
      KJ_FAIL_SYSCALL("mmap", errno);
    }
    return Array<byte>(reinterpret_cast<byte*>(mapping) + (offset - range.offset),
                       size, mmapDisposer);
  }

  // File ----------------------------------------------------------------------

  void write(uint64_t offset, ArrayPtr<const byte> data) const {
    // pwrite() probably never returns short writes unless there's no space left on disk.
    // Unfortunately, though, per spec we are not allowed to assume this.

    while (data.size() > 0) {
      ssize_t n;
      KJ_SYSCALL(n = pwrite(fd, data.begin(), data.size(), offset));
      KJ_ASSERT(n > 0, "pwrite() returned zero?");
      offset += n;
      data = data.slice(n, data.size());
    }
  }

  void zero(uint64_t offset, uint64_t size) const {
    // If FALLOC_FL_PUNCH_HOLE is defined, use it to efficiently zero the area.
    //
    // A fallocate() wrapper was only added to Android's Bionic C library as of API level 21,
    // but FALLOC_FL_PUNCH_HOLE is apparently defined in the headers before that, so we'll
    // have to explicitly test for that case.
#if defined(FALLOC_FL_PUNCH_HOLE) && !(__ANDROID__ && __BIONIC__ && __ANDROID_API__ < 21)
    KJ_SYSCALL_HANDLE_ERRORS(
        fallocate(fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE, offset, size)) {
      case EOPNOTSUPP:
        // fall back to below
        break;
      default:
        KJ_FAIL_SYSCALL("fallocate(FALLOC_FL_PUNCH_HOLE)", error) { return; }
    } else {
      return;
    }
#endif

    static const byte ZEROS[4096] = { 0 };

#if __APPLE__ || __CYGWIN__ || (defined(__ANDROID__) && __ANDROID_API__ < 24)
    // Mac & Cygwin & Android API levels 23 and lower doesn't have pwritev().
    while (size > sizeof(ZEROS)) {
      write(offset, ZEROS);
      size -= sizeof(ZEROS);
      offset += sizeof(ZEROS);
    }
    write(offset, kj::arrayPtr(ZEROS, size));
#else
    // Use a 4k buffer of zeros amplified by iov to write zeros with as few syscalls as possible.
    size_t count = (size + sizeof(ZEROS) - 1) / sizeof(ZEROS);
    const size_t iovmax = miniposix::iovMax();
    KJ_STACK_ARRAY(struct iovec, iov, kj::min(iovmax, count), 16, 256);

    for (auto& item: iov) {
      item.iov_base = const_cast<byte*>(ZEROS);
      item.iov_len = sizeof(ZEROS);
    }

    while (size > 0) {
      size_t iovCount;
      if (size >= iov.size() * sizeof(ZEROS)) {
        iovCount = iov.size();
      } else {
        iovCount = size / sizeof(ZEROS);
        size_t rem = size % sizeof(ZEROS);
        if (rem > 0) {
          iov[iovCount++].iov_len = rem;
        }
      }

      ssize_t n;
      KJ_SYSCALL(n = pwritev(fd, iov.begin(), count, offset));
      KJ_ASSERT(n > 0, "pwrite() returned zero?");

      offset += n;
      size -= n;
    }
#endif
  }

  void truncate(uint64_t size) const {
    KJ_SYSCALL(ftruncate(fd, size));
  }

  class WritableFileMappingImpl final: public WritableFileMapping {
  public:
    WritableFileMappingImpl(Array<byte> bytes): bytes(kj::mv(bytes)) {}

    ArrayPtr<byte> get() const override {
      // const_cast OK because WritableFileMapping does indeed provide a writable view despite
      // being const itself.
      return arrayPtr(const_cast<byte*>(bytes.begin()), bytes.size());
    }

    void changed(ArrayPtr<byte> slice) const override {
      KJ_REQUIRE(slice.begin() >= bytes.begin() && slice.end() <= bytes.end(),
                 "byte range is not part of this mapping");
      if (slice.size() == 0) return;

      // msync() requires page-alignment, apparently, so use getMmapRange() to accomplish that.
      auto range = getMmapRange(reinterpret_cast<uintptr_t>(slice.begin()), slice.size());
      KJ_SYSCALL(msync(reinterpret_cast<void*>(range.offset), range.size, MS_ASYNC));
    }

    void sync(ArrayPtr<byte> slice) const override {
      KJ_REQUIRE(slice.begin() >= bytes.begin() && slice.end() <= bytes.end(),
                 "byte range is not part of this mapping");
      if (slice.size() == 0) return;

      // msync() requires page-alignment, apparently, so use getMmapRange() to accomplish that.
      auto range = getMmapRange(reinterpret_cast<uintptr_t>(slice.begin()), slice.size());
      KJ_SYSCALL(msync(reinterpret_cast<void*>(range.offset), range.size, MS_SYNC));
    }

  private:
    Array<byte> bytes;
  };

  Own<const WritableFileMapping> mmapWritable(uint64_t offset, uint64_t size) const {
    if (size == 0) {
      // zero-length mmap() returns EINVAL, so avoid it
      return heap<WritableFileMappingImpl>(nullptr);
    }
    auto range = getMmapRange(offset, size);
    void* mapping = ::mmap(NULL, range.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, range.offset);
    if (mapping == MAP_FAILED) {
      KJ_FAIL_SYSCALL("mmap", errno);
    }
    auto array = Array<byte>(reinterpret_cast<byte*>(mapping) + (offset - range.offset),
                             size, mmapDisposer);
    return heap<WritableFileMappingImpl>(kj::mv(array));
  }

  size_t copyChunk(uint64_t offset, int fromFd, uint64_t fromOffset, uint64_t size) const {
    // Copies a range of bytes from `fromFd` to this file in the most efficient way possible for
    // the OS. Only returns less than `size` if EOF. Does not account for holes.

#if __linux__
    {
      KJ_SYSCALL(lseek(fd, offset, SEEK_SET));
      off_t fromPos = fromOffset;
      off_t end = fromOffset + size;
      while (fromPos < end) {
        ssize_t n;
        KJ_SYSCALL_HANDLE_ERRORS(n = sendfile(fd, fromFd, &fromPos, end - fromPos)) {
          case EINVAL:
          case ENOSYS:
            goto sendfileNotAvailable;
          default:
            KJ_FAIL_SYSCALL("sendfile", error) { return fromPos - fromOffset; }
        }
        if (n == 0) break;
      }
      return fromPos - fromOffset;
    }

  sendfileNotAvailable:
#endif
    uint64_t total = 0;
    while (size > 0) {
      byte buffer[4096];
      ssize_t n;
      KJ_SYSCALL(n = pread(fromFd, buffer, kj::min(sizeof(buffer), size), fromOffset));
      if (n == 0) break;
      write(offset, arrayPtr(buffer, n));
      fromOffset += n;
      offset += n;
      total += n;
      size -= n;
    }
    return total;
  }

  kj::Maybe<size_t> copy(uint64_t offset, const ReadableFile& from,
                         uint64_t fromOffset, uint64_t size) const {
    KJ_IF_MAYBE(otherFd, from.getFd()) {
#ifdef FICLONE
      if (offset == 0 && fromOffset == 0 && size == kj::maxValue && stat().size == 0) {
        if (ioctl(fd, FICLONE, *otherFd) >= 0) {
          return stat().size;
        }
      } else if (size > 0) {    // src_length = 0 has special meaning for the syscall, so avoid.
        struct file_clone_range range;
        memset(&range, 0, sizeof(range));
        range.src_fd = *otherFd;
        range.dest_offset = offset;
        range.src_offset = fromOffset;
        range.src_length = size == kj::maxValue ? 0 : size;
        if (ioctl(fd, FICLONERANGE, &range) >= 0) {
          // TODO(someday): What does FICLONERANGE actually do if the range goes past EOF? The docs
          //   don't say. Maybe it only copies the parts that exist. Maybe it punches holes for the
          //   rest. Where does the destination file's EOF marker end up? Who knows?
          return kj::min(from.stat().size - fromOffset, size);
        }
      } else {
        // size == 0
        return size_t(0);
      }

      // ioctl failed. Almost all failures documented for these are of the form "the operation is
      // not supported for the filesystem(s) specified", so fall back to other approaches.
#endif

      off_t toPos = offset;
      off_t fromPos = fromOffset;
      off_t end = size == kj::maxValue ? off_t(kj::maxValue) : off_t(fromOffset + size);

      for (;;) {
        // Handle data.
        {
          // Find out how much data there is before the next hole.
          off_t nextHole;
#ifdef SEEK_HOLE
          KJ_SYSCALL_HANDLE_ERRORS(nextHole = lseek(*otherFd, fromPos, SEEK_HOLE)) {
            case EINVAL:
              // SEEK_HOLE probably not supported. Assume no holes.
              nextHole = end;
              break;
            case ENXIO:
              // Past EOF. Stop here.
              return fromPos - fromOffset;
            default:
              KJ_FAIL_SYSCALL("lseek(fd, pos, SEEK_HOLE)", error) { return fromPos - fromOffset; }
          }
#else
          // SEEK_HOLE not supported. Assume no holes.
          nextHole = end;
#endif

          // Copy the next chunk of data.
          off_t copyTo = kj::min(end, nextHole);
          size_t amount = copyTo - fromPos;
          if (amount > 0) {
            size_t n = copyChunk(toPos, *otherFd, fromPos, amount);
            fromPos += n;
            toPos += n;

            if (n < amount) {
              return fromPos - fromOffset;
            }
          }

          if (fromPos == end) {
            return fromPos - fromOffset;
          }
        }

#ifdef SEEK_HOLE
        // Handle hole.
        {
          // Find out how much hole there is before the next data.
          off_t nextData;
          KJ_SYSCALL_HANDLE_ERRORS(nextData = lseek(*otherFd, fromPos, SEEK_DATA)) {
            case EINVAL:
              // SEEK_DATA probably not supported. But we should only have gotten here if we
              // were expecting a hole.
              KJ_FAIL_ASSERT("can't determine hole size; SEEK_DATA not supported");
              break;
            case ENXIO:
              // No more data. Set to EOF.
              KJ_SYSCALL(nextData = lseek(*otherFd, 0, SEEK_END));
              if (nextData > end) {
                end = nextData;
              }
              break;
            default:
              KJ_FAIL_SYSCALL("lseek(fd, pos, SEEK_HOLE)", error) { return fromPos - fromOffset; }
          }

          // Write zeros.
          off_t zeroTo = kj::min(end, nextData);
          off_t amount = zeroTo - fromPos;
          if (amount > 0) {
            zero(toPos, amount);
            toPos += amount;
            fromPos = zeroTo;
          }

          if (fromPos == end) {
            return fromPos - fromOffset;
          }
        }
#endif
      }
    }

    // Indicates caller should call File::copy() default implementation.
    return nullptr;
  }

  // ReadableDirectory ---------------------------------------------------------

  template <typename Func>
  auto list(bool needTypes, Func&& func) const
      -> Array<Decay<decltype(func(instance<StringPtr>(), instance<FsNode::Type>()))>> {
    // Seek to start of directory.
    KJ_SYSCALL(lseek(fd, 0, SEEK_SET));

    // Unfortunately, fdopendir() takes ownership of the file descriptor. Therefore we need to
    // make a duplicate.
    int duped;
    KJ_SYSCALL(duped = dup(fd));
    DIR* dir = fdopendir(duped);
    if (dir == nullptr) {
      close(duped);
      KJ_FAIL_SYSCALL("fdopendir", errno);
    }

    KJ_DEFER(closedir(dir));
    typedef Decay<decltype(func(instance<StringPtr>(), instance<FsNode::Type>()))> Entry;
    kj::Vector<Entry> entries;

    for (;;) {
      errno = 0;
      struct dirent* entry = readdir(dir);
      if (entry == nullptr) {
        int error = errno;
        if (error == 0) {
          break;
        } else {
          KJ_FAIL_SYSCALL("readdir", error);
        }
      }

      kj::StringPtr name = entry->d_name;
      if (name != "." && name != ".." && !name.startsWith(HIDDEN_PREFIX)) {
#ifdef DT_UNKNOWN    // d_type is not available on all platforms.
        if (entry->d_type != DT_UNKNOWN) {
          entries.add(func(name, modeToType(DTTOIF(entry->d_type))));
        } else {
#endif
          if (needTypes) {
            // Unknown type. Fall back to stat.
            struct stat stats;
            KJ_SYSCALL(fstatat(fd, name.cStr(), &stats, AT_SYMLINK_NOFOLLOW));
            entries.add(func(name, modeToType(stats.st_mode)));
          } else {
            entries.add(func(name, FsNode::Type::OTHER));
          }
#ifdef DT_UNKNOWN
        }
#endif
      }
    }

    auto result = entries.releaseAsArray();
    std::sort(result.begin(), result.end());
    return result;
  }

  Array<String> listNames() const {
    return list(false, [](StringPtr name, FsNode::Type type) { return heapString(name); });
  }

  Array<ReadableDirectory::Entry> listEntries() const {
    return list(true, [](StringPtr name, FsNode::Type type) {
      return ReadableDirectory::Entry { type, heapString(name), };
    });
  }

  bool exists(PathPtr path) const {
    KJ_SYSCALL_HANDLE_ERRORS(faccessat(fd, path.toString().cStr(), F_OK, 0)) {
      case ENOENT:
      case ENOTDIR:
        return false;
      default:
        KJ_FAIL_SYSCALL("faccessat(fd, path)", error, path) { return false; }
    }
    return true;
  }

  Maybe<FsNode::Metadata> tryLstat(PathPtr path) const {
    struct stat stats;
    KJ_SYSCALL_HANDLE_ERRORS(fstatat(fd, path.toString().cStr(), &stats, AT_SYMLINK_NOFOLLOW)) {
      case ENOENT:
      case ENOTDIR:
        return nullptr;
      default:
        KJ_FAIL_SYSCALL("faccessat(fd, path)", error, path) { return nullptr; }
    }
    return statToMetadata(stats);
  }

  Maybe<Own<const ReadableFile>> tryOpenFile(PathPtr path) const {
    int newFd;
    KJ_SYSCALL_HANDLE_ERRORS(newFd = openat(
        fd, path.toString().cStr(), O_RDONLY | MAYBE_O_CLOEXEC)) {
      case ENOENT:
      case ENOTDIR:
        return nullptr;
      default:
        KJ_FAIL_SYSCALL("openat(fd, path, O_RDONLY)", error, path) { return nullptr; }
    }

    kj::AutoCloseFd result(newFd);
#ifndef O_CLOEXEC
    setCloexec(result);
#endif

    return newDiskReadableFile(kj::mv(result));
  }

  Maybe<AutoCloseFd> tryOpenSubdirInternal(PathPtr path) const {
    int newFd;
    KJ_SYSCALL_HANDLE_ERRORS(newFd = openat(
        fd, path.toString().cStr(), O_RDONLY | MAYBE_O_CLOEXEC | MAYBE_O_DIRECTORY)) {
      case ENOENT:
        return nullptr;
      case ENOTDIR:
        // Could mean that a parent is not a directory, which we treat as "doesn't exist".
        // Could also mean that the specified file is not a directory, which should throw.
        // Check using exists().
        if (!exists(path)) {
          return nullptr;
        }
        KJ_FALLTHROUGH;
      default:
        KJ_FAIL_SYSCALL("openat(fd, path, O_DIRECTORY)", error, path) { return nullptr; }
    }

    kj::AutoCloseFd result(newFd);
#ifndef O_CLOEXEC
    setCloexec(result);
#endif

    return kj::mv(result);
  }

  Maybe<Own<const ReadableDirectory>> tryOpenSubdir(PathPtr path) const {
    return tryOpenSubdirInternal(path).map(newDiskReadableDirectory);
  }

  Maybe<String> tryReadlink(PathPtr path) const {
    size_t trySize = 256;
    for (;;) {
      KJ_STACK_ARRAY(char, buf, trySize, 256, 4096);
      ssize_t n = readlinkat(fd, path.toString().cStr(), buf.begin(), buf.size());
      if (n < 0) {
        int error = errno;
        switch (error) {
          case EINTR:
            continue;
          case ENOENT:
          case ENOTDIR:
          case EINVAL:    // not a link
            return nullptr;
          default:
            KJ_FAIL_SYSCALL("readlinkat(fd, path)", error, path) { return nullptr; }
        }
      }

      if (n >= buf.size()) {
        // Didn't give it enough space. Better retry with a bigger buffer.
        trySize *= 2;
        continue;
      }

      return heapString(buf.begin(), n);
    }
  }

  // Directory -----------------------------------------------------------------

  bool tryMkdir(PathPtr path, WriteMode mode, bool noThrow) const {
    // Internal function to make a directory.

    auto filename = path.toString();
    mode_t acl = has(mode, WriteMode::PRIVATE) ? 0700 : 0777;

    KJ_SYSCALL_HANDLE_ERRORS(mkdirat(fd, filename.cStr(), acl)) {
      case EEXIST: {
        // Apparently this path exists.
        if (!has(mode, WriteMode::MODIFY)) {
          // Require exclusive create.
          return false;
        }

        // MODIFY is allowed, so we just need to check whether the existing entry is a directory.
        struct stat stats;
        KJ_SYSCALL_HANDLE_ERRORS(fstatat(fd, filename.cStr(), &stats, 0)) {
          default:
            // mkdir() says EEXIST but we can't stat it. Maybe it's a dangling link, or maybe
            // we can't access it for some reason. Assume failure.
            //
            // TODO(someday): Maybe we should be creating the directory at the target of the
            //   link?
            goto failed;
        }
        return (stats.st_mode & S_IFMT) == S_IFDIR;
      }
      case ENOENT:
        if (has(mode, WriteMode::CREATE_PARENT) && path.size() > 0 &&
            tryMkdir(path.parent(), WriteMode::CREATE | WriteMode::MODIFY |
                                    WriteMode::CREATE_PARENT, true)) {
          // Retry, but make sure we don't try to create the parent again.
          return tryMkdir(path, mode - WriteMode::CREATE_PARENT, noThrow);
        } else {
          goto failed;
        }
      default:
      failed:
        if (noThrow) {
          // Caller requested no throwing.
          return false;
        } else {
          KJ_FAIL_SYSCALL("mkdirat(fd, path)", error, path);
        }
    }

    return true;
  }

  kj::Maybe<String> createNamedTemporary(
      PathPtr finalName, WriteMode mode, Function<int(StringPtr)> tryCreate) const {
    // Create a temporary file which will eventually replace `finalName`.
    //
    // Calls `tryCreate` to actually create the temporary, passing in the desired path. tryCreate()
    // is expected to behave like a syscall, returning a negative value and setting `errno` on
    // error. tryCreate() MUST fail with EEXIST if the path exists -- this is not checked in
    // advance, since it needs to be checked atomically. In the case of EEXIST, tryCreate() will
    // be called again with a new path.
    //
    // Returns the temporary path that succeeded. Only returns nullptr if there was an exception
    // but we're compiled with -fno-exceptions.

    if (finalName.size() == 0) {
      KJ_FAIL_REQUIRE("can't replace self") { break; }
      return nullptr;
    }

    static uint counter = 0;
    static const pid_t pid = getpid();
    String pathPrefix;
    if (finalName.size() > 1) {
      pathPrefix = kj::str(finalName.parent(), '/');
    }
    auto path = kj::str(pathPrefix, HIDDEN_PREFIX, pid, '.', counter++, '.',
                        finalName.basename()[0], ".partial");

    KJ_SYSCALL_HANDLE_ERRORS(tryCreate(path)) {
      case EEXIST:
        return createNamedTemporary(finalName, mode, kj::mv(tryCreate));
      case ENOENT:
        if (has(mode, WriteMode::CREATE_PARENT) && finalName.size() > 1 &&
            tryMkdir(finalName.parent(), WriteMode::CREATE | WriteMode::MODIFY |
                                         WriteMode::CREATE_PARENT, true)) {
          // Retry, but make sure we don't try to create the parent again.
          mode = mode - WriteMode::CREATE_PARENT;
          return createNamedTemporary(finalName, mode, kj::mv(tryCreate));
        }
        KJ_FALLTHROUGH;
      default:
        KJ_FAIL_SYSCALL("create(path)", error, path) { break; }
        return nullptr;
    }

    return kj::mv(path);
  }

  bool tryReplaceNode(PathPtr path, WriteMode mode, Function<int(StringPtr)> tryCreate) const {
    // Replaces the given path with an object created by calling tryCreate().
    //
    // tryCreate() must behave like a syscall which creates the node at the path passed to it,
    // returning a negative value on error. If the path passed to tryCreate already exists, it
    // MUST fail with EEXIST.
    //
    // When `mode` includes MODIFY, replaceNode() reacts to EEXIST by creating the node in a
    // temporary location and then rename()ing it into place.

    if (path.size() == 0) {
      KJ_FAIL_REQUIRE("can't replace self") { return false; }
    }

    auto filename = path.toString();

    if (has(mode, WriteMode::CREATE)) {
      // First try just cerating the node in-place.
      KJ_SYSCALL_HANDLE_ERRORS(tryCreate(filename)) {
        case EEXIST:
          // Target exists.
          if (has(mode, WriteMode::MODIFY)) {
            // Fall back to MODIFY path, below.
            break;
          } else {
            return false;
          }
        case ENOENT:
          if (has(mode, WriteMode::CREATE_PARENT) && path.size() > 0 &&
              tryMkdir(path.parent(), WriteMode::CREATE | WriteMode::MODIFY |
                                      WriteMode::CREATE_PARENT, true)) {
            // Retry, but make sure we don't try to create the parent again.
            return tryReplaceNode(path, mode - WriteMode::CREATE_PARENT, kj::mv(tryCreate));
          }
          KJ_FALLTHROUGH;
        default:
          KJ_FAIL_SYSCALL("create(path)", error, path) { return false; }
      } else {
        // Success.
        return true;
      }
    }

    // Either we don't have CREATE mode or the target already exists. We need to perform a
    // replacement instead.

    KJ_IF_MAYBE(tempPath, createNamedTemporary(path, mode, kj::mv(tryCreate))) {
      if (tryCommitReplacement(filename, fd, *tempPath, mode)) {
        return true;
      } else {
        KJ_SYSCALL_HANDLE_ERRORS(unlinkat(fd, tempPath->cStr(), 0)) {
          case ENOENT:
            // meh
            break;
          default:
            KJ_FAIL_SYSCALL("unlinkat(fd, tempPath, 0)", error, *tempPath);
        }
        return false;
      }
    } else {
      // threw, but exceptions are disabled
      return false;
    }
  }

  Maybe<AutoCloseFd> tryOpenFileInternal(PathPtr path, WriteMode mode, bool append) const {
    uint flags = O_RDWR | MAYBE_O_CLOEXEC;
    mode_t acl = 0666;
    if (has(mode, WriteMode::CREATE)) {
      flags |= O_CREAT;
    }
    if (!has(mode, WriteMode::MODIFY)) {
      if (!has(mode, WriteMode::CREATE)) {
        // Neither CREATE nor MODIFY -- impossible to satisfy preconditions.
        return nullptr;
      }
      flags |= O_EXCL;
    }
    if (append) {
      flags |= O_APPEND;
    }
    if (has(mode, WriteMode::EXECUTABLE)) {
      acl = 0777;
    }
    if (has(mode, WriteMode::PRIVATE)) {
      acl &= 0700;
    }

    auto filename = path.toString();

    int newFd;
    KJ_SYSCALL_HANDLE_ERRORS(newFd = openat(fd, filename.cStr(), flags, acl)) {
      case ENOENT:
        if (has(mode, WriteMode::CREATE)) {
          // Either:
          // - The file is a broken symlink.
          // - A parent directory didn't exist.
          if (has(mode, WriteMode::CREATE_PARENT) && path.size() > 0 &&
              tryMkdir(path.parent(), WriteMode::CREATE | WriteMode::MODIFY |
                                      WriteMode::CREATE_PARENT, true)) {
            // Retry, but make sure we don't try to create the parent again.
            return tryOpenFileInternal(path, mode - WriteMode::CREATE_PARENT, append);
          }

          // Check for broken link.
          if (!has(mode, WriteMode::MODIFY) &&
              faccessat(fd, filename.cStr(), F_OK, AT_SYMLINK_NOFOLLOW) >= 0) {
            // Yep. We treat this as already-exists, which means in CREATE-only mode this is a
            // simple failure.
            return nullptr;
          }

          KJ_FAIL_REQUIRE("parent is not a directory", path) { return nullptr; }
        } else {
          // MODIFY-only mode. ENOENT = doesn't exist = return null.
          return nullptr;
        }
      case ENOTDIR:
        if (!has(mode, WriteMode::CREATE)) {
          // MODIFY-only mode. ENOTDIR = parent not a directory = doesn't exist = return null.
          return nullptr;
        }
        goto failed;
      case EEXIST:
        if (!has(mode, WriteMode::MODIFY)) {
          // CREATE-only mode. EEXIST = already exists = return null.
          return nullptr;
        }
        goto failed;
      default:
      failed:
        KJ_FAIL_SYSCALL("openat(fd, path, O_RDWR | ...)", error, path) { return nullptr; }
    }

    kj::AutoCloseFd result(newFd);
#ifndef O_CLOEXEC
    setCloexec(result);
#endif

    return kj::mv(result);
  }

  bool tryCommitReplacement(StringPtr toPath, int fromDirFd, StringPtr fromPath, WriteMode mode,
                            int* errorReason = nullptr) const {
    if (has(mode, WriteMode::CREATE) && has(mode, WriteMode::MODIFY)) {
      // Always clobber. Try it.
      KJ_SYSCALL_HANDLE_ERRORS(renameat(fromDirFd, fromPath.cStr(), fd.get(), toPath.cStr())) {
        case EISDIR:
        case ENOTDIR:
        case ENOTEMPTY:
        case EEXIST:
          // Failed because target exists and due to the various weird quirks of rename(), it
          // can't remove it for us. On Linux we can try an exchange instead. On others we have
          // to move the target out of the way.
          break;
        default:
          if (errorReason == nullptr) {
            KJ_FAIL_SYSCALL("rename(fromPath, toPath)", error, fromPath, toPath) { return false; }
          } else {
            *errorReason = error;
            return false;
          }
      } else {
        return true;
      }
    }

#if __linux__ && defined(RENAME_EXCHANGE) && defined(SYS_renameat2)
    // Try to use Linux's renameat2() to atomically check preconditions and apply.

    if (has(mode, WriteMode::MODIFY)) {
      // Use an exchange to implement modification.
      //
      // We reach this branch when performing a MODIFY-only, or when performing a CREATE | MODIFY
      // in which we determined above that there's a node of a different type blocking the
      // exchange.

      KJ_SYSCALL_HANDLE_ERRORS(syscall(SYS_renameat2,
          fromDirFd, fromPath.cStr(), fd.get(), toPath.cStr(), RENAME_EXCHANGE)) {
        case ENOSYS:  // Syscall not supported by kernel.
        case EINVAL:  // Maybe we screwed up, or maybe the syscall is not supported by the
                      // filesystem. Unfortunately, there's no way to tell, so assume the latter.
                      // ZFS in particular apparently produces EINVAL.
          break;  // fall back to traditional means
        case ENOENT:
          // Presumably because the target path doesn't exist.
          if (has(mode, WriteMode::CREATE)) {
            KJ_FAIL_ASSERT("rename(tmp, path) claimed path exists but "
                "renameat2(fromPath, toPath, EXCHANGE) said it doest; concurrent modification?",
                fromPath, toPath) { return false; }
          } else {
            // Assume target doesn't exist.
            return false;
          }
        default:
          if (errorReason == nullptr) {
            KJ_FAIL_SYSCALL("renameat2(fromPath, toPath, EXCHANGE)", error, fromPath, toPath) {
              return false;
            }
          } else {
            *errorReason = error;
            return false;
          }
      } else {
        // Successful swap! Delete swapped-out content.
        rmrf(fromDirFd, fromPath);
        return true;
      }
    } else if (has(mode, WriteMode::CREATE)) {
      KJ_SYSCALL_HANDLE_ERRORS(syscall(SYS_renameat2,
          fromDirFd, fromPath.cStr(), fd.get(), toPath.cStr(), RENAME_NOREPLACE)) {
        case ENOSYS:  // Syscall not supported by kernel.
        case EINVAL:  // Maybe we screwed up, or maybe the syscall is not supported by the
                      // filesystem. Unfortunately, there's no way to tell, so assume the latter.
                      // ZFS in particular apparently produces EINVAL.
          break;  // fall back to traditional means
        case EEXIST:
          return false;
        default:
          if (errorReason == nullptr) {
            KJ_FAIL_SYSCALL("renameat2(fromPath, toPath, NOREPLACE)", error, fromPath, toPath) {
              return false;
            }
          } else {
            *errorReason = error;
            return false;
          }
      } else {
        return true;
      }
    }
#endif

    // We're unable to do what we wanted atomically. :(

    if (has(mode, WriteMode::CREATE) && has(mode, WriteMode::MODIFY)) {
      // We failed to atomically delete the target previously. So now we need to do two calls in
      // rapid succession to move the old file away then move the new one into place.

      // Find out what kind of file exists at the target path.
      struct stat stats;
      KJ_SYSCALL(fstatat(fd, toPath.cStr(), &stats, AT_SYMLINK_NOFOLLOW)) { return false; }

      // Create a temporary location to move the existing object to. Note that rename() allows a
      // non-directory to replace a non-directory, and allows a directory to replace an empty
      // directory. So we have to create the right type.
      Path toPathParsed = Path::parse(toPath);
      String away;
      KJ_IF_MAYBE(awayPath, createNamedTemporary(toPathParsed, WriteMode::CREATE,
          [&](StringPtr candidatePath) {
        if (S_ISDIR(stats.st_mode)) {
          return mkdirat(fd, candidatePath.cStr(), 0700);
        } else {
#if __APPLE__ || __FreeBSD__
          // - No mknodat() on OSX, gotta open() a file, ugh.
          // - On a modern FreeBSD, mknodat() is reserved strictly for device nodes,
          //   you cannot create a regular file using it (EINVAL).
          int newFd = openat(fd, candidatePath.cStr(),
                             O_RDWR | O_CREAT | O_EXCL | MAYBE_O_CLOEXEC, 0700);
          if (newFd >= 0) close(newFd);
          return newFd;
#else
          return mknodat(fd, candidatePath.cStr(), S_IFREG | 0600, dev_t());
#endif
        }
      })) {
        away = kj::mv(*awayPath);
      } else {
        // Already threw.
        return false;
      }

      // OK, now move the target object to replace the thing we just created.
      KJ_SYSCALL(renameat(fd, toPath.cStr(), fd, away.cStr())) {
        // Something went wrong. Remove the thing we just created.
        unlinkat(fd, away.cStr(), S_ISDIR(stats.st_mode) ? AT_REMOVEDIR : 0);
        return false;
      }

      // Now move the source object to the target location.
      KJ_SYSCALL_HANDLE_ERRORS(renameat(fromDirFd, fromPath.cStr(), fd, toPath.cStr())) {
        default:
          // Try to put things back where they were. If this fails, though, then we have little
          // choice but to leave things broken.
          KJ_SYSCALL_HANDLE_ERRORS(renameat(fd, away.cStr(), fd, toPath.cStr())) {
            default: break;
          }

          if (errorReason == nullptr) {
            KJ_FAIL_SYSCALL("rename(fromPath, toPath)", error, fromPath, toPath) {
              return false;
            }
          } else {
            *errorReason = error;
            return false;
          }
      }

      // OK, success. Delete the old content.
      rmrf(fd, away);
      return true;
    } else {
      // Only one of CREATE or MODIFY is specified, so we need to verify non-atomically that the
      // corresponding precondition (must-not-exist or must-exist, respectively) is held.
      if (has(mode, WriteMode::CREATE)) {
        struct stat stats;
        KJ_SYSCALL_HANDLE_ERRORS(fstatat(fd.get(), toPath.cStr(), &stats, AT_SYMLINK_NOFOLLOW)) {
          case ENOENT:
          case ENOTDIR:
            break;  // doesn't exist; continue
          default:
            KJ_FAIL_SYSCALL("fstatat(fd, toPath)", error, toPath) { return false; }
        } else {
          return false;  // already exists; fail
        }
      } else if (has(mode, WriteMode::MODIFY)) {
        struct stat stats;
        KJ_SYSCALL_HANDLE_ERRORS(fstatat(fd.get(), toPath.cStr(), &stats, AT_SYMLINK_NOFOLLOW)) {
          case ENOENT:
          case ENOTDIR:
            return false;  // doesn't exist; fail
          default:
            KJ_FAIL_SYSCALL("fstatat(fd, toPath)", error, toPath) { return false; }
        } else {
          // already exists; continue
        }
      } else {
        // Neither CREATE nor MODIFY.
        return false;
      }

      // Start over in create-and-modify mode.
      return tryCommitReplacement(toPath, fromDirFd, fromPath,
                                  WriteMode::CREATE | WriteMode::MODIFY,
                                  errorReason);
    }
  }

  template <typename T>
  class ReplacerImpl final: public Directory::Replacer<T> {
  public:
    ReplacerImpl(Own<const T>&& object, const DiskHandle& handle,
                 String&& tempPath, String&& path, WriteMode mode)
        : Directory::Replacer<T>(mode),
          object(kj::mv(object)), handle(handle),
          tempPath(kj::mv(tempPath)), path(kj::mv(path)) {}

    ~ReplacerImpl() noexcept(false) {
      if (!committed) {
        rmrf(handle.fd, tempPath);
      }
    }

    const T& get() override {
      return *object;
    }

    bool tryCommit() override {
      KJ_ASSERT(!committed, "already committed") { return false; }
      return committed = handle.tryCommitReplacement(path, handle.fd, tempPath,
                                                     Directory::Replacer<T>::mode);
    }

  private:
    Own<const T> object;
    const DiskHandle& handle;
    String tempPath;
    String path;
    bool committed = false;  // true if *successfully* committed (in which case tempPath is gone)
  };

  template <typename T>
  class BrokenReplacer final: public Directory::Replacer<T> {
    // For recovery path when exceptions are disabled.

  public:
    BrokenReplacer(Own<const T> inner)
        : Directory::Replacer<T>(WriteMode::CREATE | WriteMode::MODIFY),
          inner(kj::mv(inner)) {}

    const T& get() override { return *inner; }
    bool tryCommit() override { return false; }

  private:
    Own<const T> inner;
  };

  Maybe<Own<const File>> tryOpenFile(PathPtr path, WriteMode mode) const {
    return tryOpenFileInternal(path, mode, false).map(newDiskFile);
  }

  Own<Directory::Replacer<File>> replaceFile(PathPtr path, WriteMode mode) const {
    mode_t acl = 0666;
    if (has(mode, WriteMode::EXECUTABLE)) {
      acl = 0777;
    }
    if (has(mode, WriteMode::PRIVATE)) {
      acl &= 0700;
    }

    int newFd_;
    KJ_IF_MAYBE(temp, createNamedTemporary(path, mode,
        [&](StringPtr candidatePath) {
      return newFd_ = openat(fd, candidatePath.cStr(),
                             O_RDWR | O_CREAT | O_EXCL | MAYBE_O_CLOEXEC, acl);
    })) {
      AutoCloseFd newFd(newFd_);
#ifndef O_CLOEXEC
      setCloexec(newFd);
#endif
      return heap<ReplacerImpl<File>>(newDiskFile(kj::mv(newFd)), *this, kj::mv(*temp),
                                      path.toString(), mode);
    } else {
      // threw, but exceptions are disabled
      return heap<BrokenReplacer<File>>(newInMemoryFile(nullClock()));
    }
  }

  Own<const File> createTemporary() const {
    int newFd_;

#if __linux__ && defined(O_TMPFILE)
    // Use syscall() to work around glibc bug with O_TMPFILE:
    //     https://sourceware.org/bugzilla/show_bug.cgi?id=17523
    KJ_SYSCALL_HANDLE_ERRORS(newFd_ = syscall(
        SYS_openat, fd.get(), ".", O_RDWR | O_TMPFILE, 0700)) {
      case EOPNOTSUPP:
      case EINVAL:
      case EISDIR:
        // Maybe not supported by this kernel / filesystem. Fall back to below.
        break;
      default:
        KJ_FAIL_SYSCALL("open(O_TMPFILE)", error) { break; }
        break;
    } else {
      AutoCloseFd newFd(newFd_);
#ifndef O_CLOEXEC
      setCloexec(newFd);
#endif
      return newDiskFile(kj::mv(newFd));
    }
#endif

    KJ_IF_MAYBE(temp, createNamedTemporary(Path("unnamed"), WriteMode::CREATE,
        [&](StringPtr path) {
      return newFd_ = openat(fd, path.cStr(), O_RDWR | O_CREAT | O_EXCL | MAYBE_O_CLOEXEC, 0600);
    })) {
      AutoCloseFd newFd(newFd_);
#ifndef O_CLOEXEC
      setCloexec(newFd);
#endif
      auto result = newDiskFile(kj::mv(newFd));
      KJ_SYSCALL(unlinkat(fd, temp->cStr(), 0)) { break; }
      return kj::mv(result);
    } else {
      // threw, but exceptions are disabled
      return newInMemoryFile(nullClock());
    }
  }

  Maybe<Own<AppendableFile>> tryAppendFile(PathPtr path, WriteMode mode) const {
    return tryOpenFileInternal(path, mode, true).map(newDiskAppendableFile);
  }

  Maybe<Own<const Directory>> tryOpenSubdir(PathPtr path, WriteMode mode) const {
    // Must create before open.
    if (has(mode, WriteMode::CREATE)) {
      if (!tryMkdir(path, mode, false)) return nullptr;
    }

    return tryOpenSubdirInternal(path).map(newDiskDirectory);
  }

  Own<Directory::Replacer<Directory>> replaceSubdir(PathPtr path, WriteMode mode) const {
    mode_t acl = has(mode, WriteMode::PRIVATE) ? 0700 : 0777;

    KJ_IF_MAYBE(temp, createNamedTemporary(path, mode,
        [&](StringPtr candidatePath) {
      return mkdirat(fd, candidatePath.cStr(), acl);
    })) {
      int subdirFd_;
      KJ_SYSCALL_HANDLE_ERRORS(subdirFd_ = openat(
          fd, temp->cStr(), O_RDONLY | MAYBE_O_CLOEXEC | MAYBE_O_DIRECTORY)) {
        default:
          KJ_FAIL_SYSCALL("open(just-created-temporary)", error);
          return heap<BrokenReplacer<Directory>>(newInMemoryDirectory(nullClock()));
      }

      AutoCloseFd subdirFd(subdirFd_);
#ifndef O_CLOEXEC
      setCloexec(subdirFd);
#endif
      return heap<ReplacerImpl<Directory>>(
          newDiskDirectory(kj::mv(subdirFd)), *this, kj::mv(*temp), path.toString(), mode);
    } else {
      // threw, but exceptions are disabled
      return heap<BrokenReplacer<Directory>>(newInMemoryDirectory(nullClock()));
    }
  }

  bool trySymlink(PathPtr linkpath, StringPtr content, WriteMode mode) const {
    return tryReplaceNode(linkpath, mode, [&](StringPtr candidatePath) {
      return symlinkat(content.cStr(), fd, candidatePath.cStr());
    });
  }

  bool tryTransfer(PathPtr toPath, WriteMode toMode,
                   const Directory& fromDirectory, PathPtr fromPath,
                   TransferMode mode, const Directory& self) const {
    KJ_REQUIRE(toPath.size() > 0, "can't replace self") { return false; }

    if (mode == TransferMode::LINK) {
      KJ_IF_MAYBE(fromFd, fromDirectory.getFd()) {
        // Other is a disk directory, so we can hopefully do an efficient move/link.
        return tryReplaceNode(toPath, toMode, [&](StringPtr candidatePath) {
          return linkat(*fromFd, fromPath.toString().cStr(), fd, candidatePath.cStr(), 0);
        });
      };
    } else if (mode == TransferMode::MOVE) {
      KJ_IF_MAYBE(fromFd, fromDirectory.getFd()) {
        KJ_ASSERT(mode == TransferMode::MOVE);

        int error = 0;
        if (tryCommitReplacement(toPath.toString(), *fromFd, fromPath.toString(), toMode,
                                 &error)) {
          return true;
        } else switch (error) {
          case 0:
            // Plain old WriteMode precondition failure.
            return false;
          case EXDEV:
            // Can't move between devices. Fall back to default implementation, which does
            // copy/delete.
            break;
          case ENOENT:
            // Either the destination directory doesn't exist or the source path doesn't exist.
            // Unfortunately we don't really know. If CREATE_PARENT was provided, try creating
            // the parent directory. Otherwise, we don't actually need to distinguish between
            // these two errors; just return false.
            if (has(toMode, WriteMode::CREATE) && has(toMode, WriteMode::CREATE_PARENT) &&
                toPath.size() > 0 && tryMkdir(toPath.parent(),
                    WriteMode::CREATE | WriteMode::MODIFY | WriteMode::CREATE_PARENT, true)) {
              // Retry, but make sure we don't try to create the parent again.
              return tryTransfer(toPath, toMode - WriteMode::CREATE_PARENT,
                                 fromDirectory, fromPath, mode, self);
            }
            return false;
          default:
            KJ_FAIL_SYSCALL("rename(fromPath, toPath)", error, fromPath, toPath) {
              return false;
            }
        }
      }
    }

    // OK, we can't do anything efficient using the OS. Fall back to default implementation.
    return self.Directory::tryTransfer(toPath, toMode, fromDirectory, fromPath, mode);
  }

  bool tryRemove(PathPtr path) const {
    return rmrf(fd, path.toString());
  }

protected:
  AutoCloseFd fd;
};

#define FSNODE_METHODS(classname)                                   \
  Maybe<int> getFd() const override { return DiskHandle::getFd(); } \
                                                                    \
  Own<const FsNode> cloneFsNode() const override {                  \
    return heap<classname>(DiskHandle::clone());                    \
  }                                                                 \
                                                                    \
  Metadata stat() const override { return DiskHandle::stat(); }     \
  void sync() const override { DiskHandle::sync(); }                \
  void datasync() const override { DiskHandle::datasync(); }

class DiskReadableFile final: public ReadableFile, public DiskHandle {
public:
  DiskReadableFile(AutoCloseFd&& fd): DiskHandle(kj::mv(fd)) {}

  FSNODE_METHODS(DiskReadableFile);

  size_t read(uint64_t offset, ArrayPtr<byte> buffer) const override {
    return DiskHandle::read(offset, buffer);
  }
  Array<const byte> mmap(uint64_t offset, uint64_t size) const override {
    return DiskHandle::mmap(offset, size);
  }
  Array<byte> mmapPrivate(uint64_t offset, uint64_t size) const override {
    return DiskHandle::mmapPrivate(offset, size);
  }
};

class DiskAppendableFile final: public AppendableFile, public DiskHandle, public FdOutputStream {
public:
  DiskAppendableFile(AutoCloseFd&& fd)
      : DiskHandle(kj::mv(fd)),
        FdOutputStream(DiskHandle::fd.get()) {}

  FSNODE_METHODS(DiskAppendableFile);

  void write(const void* buffer, size_t size) override {
    FdOutputStream::write(buffer, size);
  }
  void write(ArrayPtr<const ArrayPtr<const byte>> pieces) override {
    FdOutputStream::write(pieces);
  }
};

class DiskFile final: public File, public DiskHandle {
public:
  DiskFile(AutoCloseFd&& fd): DiskHandle(kj::mv(fd)) {}

  FSNODE_METHODS(DiskFile);

  size_t read(uint64_t offset, ArrayPtr<byte> buffer) const override {
    return DiskHandle::read(offset, buffer);
  }
  Array<const byte> mmap(uint64_t offset, uint64_t size) const override {
    return DiskHandle::mmap(offset, size);
  }
  Array<byte> mmapPrivate(uint64_t offset, uint64_t size) const override {
    return DiskHandle::mmapPrivate(offset, size);
  }

  void write(uint64_t offset, ArrayPtr<const byte> data) const override {
    DiskHandle::write(offset, data);
  }
  void zero(uint64_t offset, uint64_t size) const override {
    DiskHandle::zero(offset, size);
  }
  void truncate(uint64_t size) const override {
    DiskHandle::truncate(size);
  }
  Own<const WritableFileMapping> mmapWritable(uint64_t offset, uint64_t size) const override {
    return DiskHandle::mmapWritable(offset, size);
  }
  size_t copy(uint64_t offset, const ReadableFile& from,
              uint64_t fromOffset, uint64_t size) const override {
    KJ_IF_MAYBE(result, DiskHandle::copy(offset, from, fromOffset, size)) {
      return *result;
    } else {
      return File::copy(offset, from, fromOffset, size);
    }
  }
};

class DiskReadableDirectory final: public ReadableDirectory, public DiskHandle {
public:
  DiskReadableDirectory(AutoCloseFd&& fd): DiskHandle(kj::mv(fd)) {}

  FSNODE_METHODS(DiskReadableDirectory);

  Array<String> listNames() const override { return DiskHandle::listNames(); }
  Array<Entry> listEntries() const override { return DiskHandle::listEntries(); }
  bool exists(PathPtr path) const override { return DiskHandle::exists(path); }
  Maybe<FsNode::Metadata> tryLstat(PathPtr path) const override {
    return DiskHandle::tryLstat(path);
  }
  Maybe<Own<const ReadableFile>> tryOpenFile(PathPtr path) const override {
    return DiskHandle::tryOpenFile(path);
  }
  Maybe<Own<const ReadableDirectory>> tryOpenSubdir(PathPtr path) const override {
    return DiskHandle::tryOpenSubdir(path);
  }
  Maybe<String> tryReadlink(PathPtr path) const override { return DiskHandle::tryReadlink(path); }
};

class DiskDirectory final: public Directory, public DiskHandle {
public:
  DiskDirectory(AutoCloseFd&& fd): DiskHandle(kj::mv(fd)) {}

  FSNODE_METHODS(DiskDirectory);

  Array<String> listNames() const override { return DiskHandle::listNames(); }
  Array<Entry> listEntries() const override { return DiskHandle::listEntries(); }
  bool exists(PathPtr path) const override { return DiskHandle::exists(path); }
  Maybe<FsNode::Metadata> tryLstat(PathPtr path) const override {
    return DiskHandle::tryLstat(path);
  }
  Maybe<Own<const ReadableFile>> tryOpenFile(PathPtr path) const override {
    return DiskHandle::tryOpenFile(path);
  }
  Maybe<Own<const ReadableDirectory>> tryOpenSubdir(PathPtr path) const override {
    return DiskHandle::tryOpenSubdir(path);
  }
  Maybe<String> tryReadlink(PathPtr path) const override { return DiskHandle::tryReadlink(path); }

  Maybe<Own<const File>> tryOpenFile(PathPtr path, WriteMode mode) const override {
    return DiskHandle::tryOpenFile(path, mode);
  }
  Own<Replacer<File>> replaceFile(PathPtr path, WriteMode mode) const override {
    return DiskHandle::replaceFile(path, mode);
  }
  Own<const File> createTemporary() const override {
    return DiskHandle::createTemporary();
  }
  Maybe<Own<AppendableFile>> tryAppendFile(PathPtr path, WriteMode mode) const override {
    return DiskHandle::tryAppendFile(path, mode);
  }
  Maybe<Own<const Directory>> tryOpenSubdir(PathPtr path, WriteMode mode) const override {
    return DiskHandle::tryOpenSubdir(path, mode);
  }
  Own<Replacer<Directory>> replaceSubdir(PathPtr path, WriteMode mode) const override {
    return DiskHandle::replaceSubdir(path, mode);
  }
  bool trySymlink(PathPtr linkpath, StringPtr content, WriteMode mode) const override {
    return DiskHandle::trySymlink(linkpath, content, mode);
  }
  bool tryTransfer(PathPtr toPath, WriteMode toMode,
                   const Directory& fromDirectory, PathPtr fromPath,
                   TransferMode mode) const override {
    return DiskHandle::tryTransfer(toPath, toMode, fromDirectory, fromPath, mode, *this);
  }
  // tryTransferTo() not implemented because we have nothing special we can do.
  bool tryRemove(PathPtr path) const override {
    return DiskHandle::tryRemove(path);
  }
};

class DiskFilesystem final: public Filesystem {
public:
  DiskFilesystem()
      : root(openDir("/")),
        current(openDir(".")),
        currentPath(computeCurrentPath()) {
    // We sometimes like to use qemu-user to test arm64 binaries cross-compiled from an x64 host
    // machine. But, because it intercepts and rewrites system calls from userspace rather than
    // emulating a whole kernel, it has a lot of quirks. One quirk that hits kj::Filesystem pretty
    // badly is that open("/") actually returns a file descriptor for "/usr/aarch64-linux-gnu".
    // Attempts to openat() any files within there then don't work. We can detect this problem and
    // correct for it here.
    struct stat realRoot, fsRoot;
    KJ_SYSCALL_HANDLE_ERRORS(stat("/dev/..", &realRoot)) {
      default:
        // stat("/dev/..") failed? Give up.
        return;
    }
    KJ_SYSCALL(fstat(root.DiskHandle::getFd(), &fsRoot));
    if (realRoot.st_ino != fsRoot.st_ino) {
      KJ_LOG(WARNING, "root dir file descriptor is broken, probably because of qemu; compensating");
      root.setFd(openDir("/dev/.."));
    }
  }

  const Directory& getRoot() const override {
    return root;
  }

  const Directory& getCurrent() const override {
    return current;
  }

  PathPtr getCurrentPath() const override {
    return currentPath;
  }

private:
  DiskDirectory root;
  DiskDirectory current;
  Path currentPath;

  static AutoCloseFd openDir(const char* dir) {
    int newFd;
    KJ_SYSCALL(newFd = open(dir, O_RDONLY | MAYBE_O_CLOEXEC | MAYBE_O_DIRECTORY));
    AutoCloseFd result(newFd);
#ifndef O_CLOEXEC
    setCloexec(result);
#endif
    return result;
  }

  static Path computeCurrentPath() {
    // If env var PWD is set and points to the current directory, use it. This captures the current
    // path according to the user's shell, which may differ from the kernel's idea in the presence
    // of symlinks.
    const char* pwd = getenv("PWD");
    if (pwd != nullptr) {
      Path result = nullptr;
      struct stat pwdStat, dotStat;
      KJ_IF_MAYBE(e, kj::runCatchingExceptions([&]() {
        KJ_ASSERT(pwd[0] == '/') { return; }
        result = Path::parse(pwd + 1);
        KJ_SYSCALL(lstat(result.toString(true).cStr(), &pwdStat), result) { return; }
        KJ_SYSCALL(lstat(".", &dotStat)) { return; }
      })) {
        // failed, give up on PWD
        KJ_LOG(WARNING, "PWD environment variable seems invalid", pwd, *e);
      } else {
        if (pwdStat.st_ino == dotStat.st_ino &&
            pwdStat.st_dev == dotStat.st_dev) {
          return kj::mv(result);
        } else {
          // It appears PWD doesn't actually point at the current directory. In practice, only
          // shells tend to update PWD. Other programs, like build tools, may do `chdir()` without
          // actually updating PWD to match. Arguably they are buggy, but realistically we have to
          // live with them. So, we will treat an incorrect PWD the same as an absent PWD, and fall
          // back to using the current directory's canonical path.
          //
          // We used to log a WARNING here but it was deemed too noisy, so we changed it to INFO.
          KJ_LOG(INFO, "PWD environment variable doesn't match current directory", pwd);
        }
      }
    }

    size_t size = 256;
  retry:
    KJ_STACK_ARRAY(char, buf, size, 256, 4096);
    if (getcwd(buf.begin(), size) == nullptr) {
      int error = errno;
      if (error == ERANGE) {
        size *= 2;
        goto retry;
      } else {
        KJ_FAIL_SYSCALL("getcwd()", error);
      }
    }

    StringPtr path = buf.begin();

    // On Linux, the path will start with "(unreachable)" if the working directory is not a subdir
    // of the root directory, which is possible via chroot() or mount namespaces.
    KJ_ASSERT(!path.startsWith("(unreachable)"),
        "working directory is not reachable from root", path);
    KJ_ASSERT(path.startsWith("/"), "current directory is not absolute", path);

    return Path::parse(path.slice(1));
  }
};

} // namespace

Own<ReadableFile> newDiskReadableFile(kj::AutoCloseFd fd) {
  return heap<DiskReadableFile>(kj::mv(fd));
}
Own<AppendableFile> newDiskAppendableFile(kj::AutoCloseFd fd) {
  return heap<DiskAppendableFile>(kj::mv(fd));
}
Own<File> newDiskFile(kj::AutoCloseFd fd) {
  return heap<DiskFile>(kj::mv(fd));
}
Own<ReadableDirectory> newDiskReadableDirectory(kj::AutoCloseFd fd) {
  return heap<DiskReadableDirectory>(kj::mv(fd));
}
Own<Directory> newDiskDirectory(kj::AutoCloseFd fd) {
  return heap<DiskDirectory>(kj::mv(fd));
}

Own<Filesystem> newDiskFilesystem() {
  return heap<DiskFilesystem>();
}

} // namespace kj

#endif  // !_WIN32
