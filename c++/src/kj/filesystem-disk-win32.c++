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

#if _WIN32
// For Unix implementation, see filesystem-disk-unix.c++.

// Request Vista-level APIs.
#include "win32-api-version.h"

#include "filesystem.h"
#include "debug.h"
#include "encoding.h"
#include "vector.h"
#include <algorithm>
#include <wchar.h>

#include <windows.h>
#include <winioctl.h>
#include "windows-sanity.h"

namespace kj {

static Own<ReadableDirectory> newDiskReadableDirectory(AutoCloseHandle fd, Path&& path);
static Own<Directory> newDiskDirectory(AutoCloseHandle fd, Path&& path);

static AutoCloseHandle* getHandlePointerHack(File& file) { return nullptr; }
static AutoCloseHandle* getHandlePointerHack(Directory& dir);
static Path* getPathPointerHack(File& file) { return nullptr; }
static Path* getPathPointerHack(Directory& dir);

namespace {

struct REPARSE_DATA_BUFFER {
  // From ntifs.h, which is part of the driver development kit so not necessarily available I
  // guess.
  ULONG ReparseTag;
  USHORT ReparseDataLength;
  USHORT Reserved;
  union {
    struct {
      USHORT SubstituteNameOffset;
      USHORT SubstituteNameLength;
      USHORT PrintNameOffset;
      USHORT PrintNameLength;
      ULONG Flags;
      WCHAR PathBuffer[1];
    } SymbolicLinkReparseBuffer;
    struct {
      USHORT SubstituteNameOffset;
      USHORT SubstituteNameLength;
      USHORT PrintNameOffset;
      USHORT PrintNameLength;
      WCHAR PathBuffer[1];
    } MountPointReparseBuffer;
    struct {
      UCHAR DataBuffer[1];
    } GenericReparseBuffer;
  };
};

#define HIDDEN_PREFIX ".kj-tmp."
// Prefix for temp files which should be hidden when listing a directory.
//
// If you change this, make sure to update the unit test.

static constexpr int64_t WIN32_EPOCH_OFFSET = 116444736000000000ull;
// Number of 100ns intervals from Jan 1, 1601 to Jan 1, 1970.

static Date toKjDate(FILETIME t) {
  int64_t value = (static_cast<uint64_t>(t.dwHighDateTime) << 32) | t.dwLowDateTime;
  return (value - WIN32_EPOCH_OFFSET) * (100 * kj::NANOSECONDS) + UNIX_EPOCH;
}

static FsNode::Type modeToType(DWORD attrs, DWORD reparseTag) {
  if ((attrs & FILE_ATTRIBUTE_REPARSE_POINT) &&
      reparseTag == IO_REPARSE_TAG_SYMLINK) {
    return FsNode::Type::SYMLINK;
  }
  if (attrs & FILE_ATTRIBUTE_DIRECTORY) return FsNode::Type::DIRECTORY;
  return FsNode::Type::FILE;
}

static FsNode::Metadata statToMetadata(const BY_HANDLE_FILE_INFORMATION& stats) {
  uint64_t size = (implicitCast<uint64_t>(stats.nFileSizeHigh) << 32) | stats.nFileSizeLow;

  // Assume file index is usually a small number, i.e. nFileIndexHigh is usually 0. So we try to
  // put the serial number in the upper 32 bits and the index in the lower.
  uint64_t hash = ((uint64_t(stats.dwVolumeSerialNumber) << 32)
                 ^ (uint64_t(stats.nFileIndexHigh) << 32))
                | (uint64_t(stats.nFileIndexLow));

  return FsNode::Metadata {
    modeToType(stats.dwFileAttributes, 0),
    size,
    // In theory, spaceUsed should be based on GetCompressedFileSize(), but requiring an extra
    // syscall for something rarely used would be sad.
    size,
    toKjDate(stats.ftLastWriteTime),
    stats.nNumberOfLinks,
    hash
  };
}

static FsNode::Metadata statToMetadata(const WIN32_FIND_DATAW& stats) {
  uint64_t size = (implicitCast<uint64_t>(stats.nFileSizeHigh) << 32) | stats.nFileSizeLow;

  return FsNode::Metadata {
    modeToType(stats.dwFileAttributes, stats.dwReserved0),
    size,
    // In theory, spaceUsed should be based on GetCompressedFileSize(), but requiring an extra
    // syscall for something rarely used would be sad.
    size,
    toKjDate(stats.ftLastWriteTime),
    // We can't get the number of links without opening the file, apparently. Meh.
    1,
    // We can't produce a reliable hashCode without opening the file.
    0
  };
}

static Array<wchar_t> join16(ArrayPtr<const wchar_t> path, const wchar_t* file) {
  // Assumes `path` ends with a NUL terminator (and `file` is of course NUL terminated as well).

  size_t len = wcslen(file) + 1;
  auto result = kj::heapArray<wchar_t>(path.size() + len);
  memcpy(result.begin(), path.begin(), path.asBytes().size() - sizeof(wchar_t));
  result[path.size() - 1] = '\\';
  memcpy(result.begin() + path.size(), file, len * sizeof(wchar_t));
  return result;
}

static String dbgStr(ArrayPtr<const wchar_t> wstr) {
  if (wstr.size() > 0 && wstr[wstr.size() - 1] == L'\0') {
    wstr = wstr.slice(0, wstr.size() - 1);
  }
  return decodeWideString(wstr);
}

static void rmrfChildren(ArrayPtr<const wchar_t> path) {
  auto glob = join16(path, L"*");

  WIN32_FIND_DATAW data;
  // TODO(security): If `path` is a reparse point (symlink), this will follow it and delete the
  //   contents. We check for reparse points before recursing, but there is still a TOCTOU race
  //   condition.
  //
  //   Apparently, there is a whole different directory-listing API we could be using here:
  //   `GetFileInformationByHandleEx()`, with the `FileIdBothDirectoryInfo` flag. This lets us
  //   list the contents of a directory from its already-open handle -- it's probably how we should
  //   do directory listing in general! If we open a file with FILE_FLAG_OPEN_REPARSE_POINT, then
  //   the handle will represent the reparse point itself, and attempting to list it will produce
  //   no entries. I had no idea this API existed when I wrote much of this code; I wish I had
  //   because it seems much cleaner than the ancient FindFirstFile/FindNextFile API!
  HANDLE handle = FindFirstFileW(glob.begin(), &data);
  if (handle == INVALID_HANDLE_VALUE) {
    auto error = GetLastError();
    if (error == ERROR_FILE_NOT_FOUND) return;
    KJ_FAIL_WIN32("FindFirstFile", error, dbgStr(glob)) { return; }
  }
  KJ_DEFER(KJ_WIN32(FindClose(handle)) { break; });

  do {
    // Ignore "." and "..", ugh.
    if (data.cFileName[0] == L'.') {
      if (data.cFileName[1] == L'\0' ||
          (data.cFileName[1] == L'.' && data.cFileName[2] == L'\0')) {
        continue;
      }
    }

    auto child = join16(path, data.cFileName);
    // For rmrf purposes, we assume any "reparse points" are symlink-like, even if they aren't
    // actually the "symbolic link" reparse type, because we don't want to recursively delete any
    // shared content.
    if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
        !(data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
      rmrfChildren(child);
      uint retryCount = 0;
    retry:
      KJ_WIN32_HANDLE_ERRORS(RemoveDirectoryW(child.begin())) {
        case ERROR_DIR_NOT_EMPTY:
          // On Windows, deleting a file actually only schedules it for deletion. Under heavy
          // load it may take a bit for the deletion to go through. Or, if another process has
          // the file open, it may not be deleted until that process closes it.
          //
          // We'll repeatedly retry for up to 100ms, then give up. This is awful but there's no
          // way to tell for sure if the system is just being slow or if someone has the file
          // open.
          if (retryCount++ < 10) {
            Sleep(10);
            goto retry;
          }
          KJ_FALLTHROUGH;
        default:
          KJ_FAIL_WIN32("RemoveDirectory", error, dbgStr(child)) { break; }
      }
    } else {
      KJ_WIN32(DeleteFileW(child.begin()));
    }
  } while (FindNextFileW(handle, &data));

  auto error = GetLastError();
  if (error != ERROR_NO_MORE_FILES) {
    KJ_FAIL_WIN32("FindNextFile", error, dbgStr(path)) { return; }
  }
}

static bool rmrf(ArrayPtr<const wchar_t> path) {
  // Figure out whether this is a file or a directory.
  //
  // We use FindFirstFileW() because in the case of symlinks it will return info about the
  // symlink rather than info about the target.
  WIN32_FIND_DATAW data;
  HANDLE handle = FindFirstFileW(path.begin(), &data);
  if (handle == INVALID_HANDLE_VALUE) {
    auto error = GetLastError();
    if (error == ERROR_FILE_NOT_FOUND) return false;
    KJ_FAIL_WIN32("FindFirstFile", error, dbgStr(path));
  }
  KJ_WIN32(FindClose(handle));

  // For remove purposes, we assume any "reparse points" are symlink-like, even if they aren't
  // actually the "symbolic link" reparse type, because we don't want to recursively delete any
  // shared content.
  if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
      !(data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
    // directory
    rmrfChildren(path);
    KJ_WIN32(RemoveDirectoryW(path.begin()), dbgStr(path));
  } else {
    KJ_WIN32(DeleteFileW(path.begin()), dbgStr(path));
  }

  return true;
}

static Path getPathFromHandle(HANDLE handle) {
  DWORD tryLen = MAX_PATH;
  for (;;) {
    auto temp = kj::heapArray<wchar_t>(tryLen + 1);
    DWORD len = GetFinalPathNameByHandleW(handle, temp.begin(), tryLen, 0);
    if (len == 0) {
      KJ_FAIL_WIN32("GetFinalPathNameByHandleW", GetLastError());
    }
    if (len < temp.size()) {
      return Path::parseWin32Api(temp.slice(0, len));
    }
    // Try again with new length.
    tryLen = len;
  }
}

struct MmapRange {
  uint64_t offset;
  uint64_t size;
};

static size_t getAllocationGranularity() {
  SYSTEM_INFO info;
  GetSystemInfo(&info);
  return info.dwAllocationGranularity;
};

static MmapRange getMmapRange(uint64_t offset, uint64_t size) {
  // Rounds the given offset down to the nearest page boundary, and adjusts the size up to match.
  // (This is somewhat different from Unix: we do NOT round the size up to an even multiple of
  // pages.)
  static const uint64_t pageSize = getAllocationGranularity();
  uint64_t pageMask = pageSize - 1;

  uint64_t realOffset = offset & ~pageMask;

  uint64_t end = offset + size;

  return { realOffset, end - realOffset };
}

class MmapDisposer: public ArrayDisposer {
protected:
  void disposeImpl(void* firstElement, size_t elementSize, size_t elementCount,
                   size_t capacity, void (*destroyElement)(void*)) const {
    auto range = getMmapRange(reinterpret_cast<uintptr_t>(firstElement),
                              elementSize * elementCount);
    void* mapping = reinterpret_cast<void*>(range.offset);
    if (mapping != nullptr) {
      KJ_ASSERT(UnmapViewOfFile(mapping)) { break; }
    }
  }
};

#if _MSC_VER && _MSC_VER < 1910 && !defined(__clang__)
// TODO(msvc): MSVC 2015 can't initialize a constexpr's vtable correctly.
const MmapDisposer mmapDisposer = MmapDisposer();
#else
constexpr MmapDisposer mmapDisposer = MmapDisposer();
#endif

void* win32Mmap(HANDLE handle, MmapRange range, DWORD pageProtect, DWORD access) {
  HANDLE mappingHandle;
  KJ_WIN32(mappingHandle = CreateFileMappingW(handle, NULL, pageProtect, 0, 0, NULL));
  KJ_DEFER(KJ_WIN32(CloseHandle(mappingHandle)) { break; });

  void* mapping = MapViewOfFile(mappingHandle, access,
      static_cast<DWORD>(range.offset >> 32), static_cast<DWORD>(range.offset), range.size);
  if (mapping == nullptr) {
    KJ_FAIL_WIN32("MapViewOfFile", GetLastError());
  }

  // It's unclear from the documentation whether mappings will always start at a multiple of the
  // allocation granularity, but we depend on that later, so check it...
  KJ_ASSERT(getMmapRange(reinterpret_cast<uintptr_t>(mapping), 0).size == 0);

  return mapping;
}

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
  DiskHandle(AutoCloseHandle&& handle, Maybe<Path> dirPath)
      : handle(kj::mv(handle)), dirPath(kj::mv(dirPath)) {}

  AutoCloseHandle handle;
  kj::Maybe<Path> dirPath;  // needed for directories, empty for files

  Array<wchar_t> nativePath(PathPtr path) const {
    return KJ_ASSERT_NONNULL(dirPath).append(path).forWin32Api(true);
  }

  // OsHandle ------------------------------------------------------------------

  AutoCloseHandle clone() const {
    HANDLE newHandle;
    KJ_WIN32(DuplicateHandle(GetCurrentProcess(), handle, GetCurrentProcess(), &newHandle,
                             0, FALSE, DUPLICATE_SAME_ACCESS));
    return AutoCloseHandle(newHandle);
  }

  HANDLE getWin32Handle() const {
    return handle.get();
  }

  // FsNode --------------------------------------------------------------------

  FsNode::Metadata stat() const {
    BY_HANDLE_FILE_INFORMATION stats;
    KJ_WIN32(GetFileInformationByHandle(handle, &stats));
    auto metadata = statToMetadata(stats);

    // Get space usage, e.g. for sparse files. Apparently the correct way to do this is to query
    // "compression".
    FILE_COMPRESSION_INFO compInfo;
    KJ_WIN32_HANDLE_ERRORS(GetFileInformationByHandleEx(
        handle, FileCompressionInfo, &compInfo, sizeof(compInfo))) {
      case ERROR_CALL_NOT_IMPLEMENTED:
        // Probably WINE.
        break;
      default:
        KJ_FAIL_WIN32("GetFileInformationByHandleEx(FileCompressionInfo)", error) { break; }
        break;
    } else {
      metadata.spaceUsed = compInfo.CompressedFileSize.QuadPart;
    }

    return metadata;
  }

  void sync() const { KJ_WIN32(FlushFileBuffers(handle)); }
  void datasync() const { KJ_WIN32(FlushFileBuffers(handle)); }

  // ReadableFile --------------------------------------------------------------

  size_t read(uint64_t offset, ArrayPtr<byte> buffer) const {
    // ReadFile() probably never returns short reads unless it hits EOF. Unfortunately, though,
    // this is not documented, and it's unclear whether we can rely on it.

    size_t total = 0;
    while (buffer.size() > 0) {
      // Apparently, the way to fake pread() on Windows is to provide an OVERLAPPED structure even
      // though we're not doing overlapped I/O.
      OVERLAPPED overlapped;
      memset(&overlapped, 0, sizeof(overlapped));
      overlapped.Offset = static_cast<DWORD>(offset);
      overlapped.OffsetHigh = static_cast<DWORD>(offset >> 32);

      DWORD n;
      KJ_WIN32_HANDLE_ERRORS(ReadFile(handle, buffer.begin(), buffer.size(), &n, &overlapped)) {
        case ERROR_HANDLE_EOF:
          // The documentation claims this shouldn't happen for synchronous reads, but it seems
          // to happen for me, at least under WINE.
          n = 0;
          break;
        default:
          KJ_FAIL_WIN32("ReadFile", offset, buffer.size()) { return total; }
      }
      if (n == 0) break;
      total += n;
      offset += n;
      buffer = buffer.slice(n, buffer.size());
    }
    return total;
  }

  Array<const byte> mmap(uint64_t offset, uint64_t size) const {
    if (size == 0) return nullptr;  // Windows won't allow zero-length mappings
    auto range = getMmapRange(offset, size);
    const void* mapping = win32Mmap(handle, range, PAGE_READONLY, FILE_MAP_READ);
    return Array<const byte>(reinterpret_cast<const byte*>(mapping) + (offset - range.offset),
                             size, mmapDisposer);
  }

  Array<byte> mmapPrivate(uint64_t offset, uint64_t size) const {
    if (size == 0) return nullptr;  // Windows won't allow zero-length mappings
    auto range = getMmapRange(offset, size);
    void* mapping = win32Mmap(handle, range, PAGE_READONLY, FILE_MAP_COPY);
    return Array<byte>(reinterpret_cast<byte*>(mapping) + (offset - range.offset),
                       size, mmapDisposer);
  }

  // File ----------------------------------------------------------------------

  void write(uint64_t offset, ArrayPtr<const byte> data) const {
    // WriteFile() probably never returns short writes unless there's no space left on disk.
    // Unfortunately, though, this is not documented, and it's unclear whether we can rely on it.

    while (data.size() > 0) {
      // Apparently, the way to fake pwrite() on Windows is to provide an OVERLAPPED structure even
      // though we're not doing overlapped I/O.
      OVERLAPPED overlapped;
      memset(&overlapped, 0, sizeof(overlapped));
      overlapped.Offset = static_cast<DWORD>(offset);
      overlapped.OffsetHigh = static_cast<DWORD>(offset >> 32);

      DWORD n;
      KJ_WIN32(WriteFile(handle, data.begin(), data.size(), &n, &overlapped));
      KJ_ASSERT(n > 0, "WriteFile() returned zero?");
      offset += n;
      data = data.slice(n, data.size());
    }
  }

  void zero(uint64_t offset, uint64_t size) const {
    FILE_ZERO_DATA_INFORMATION info;
    memset(&info, 0, sizeof(info));
    info.FileOffset.QuadPart = offset;
    info.BeyondFinalZero.QuadPart = offset + size;

    DWORD dummy;
    KJ_WIN32_HANDLE_ERRORS(DeviceIoControl(handle, FSCTL_SET_ZERO_DATA, &info,
                                           sizeof(info), NULL, 0, &dummy, NULL)) {
      case ERROR_NOT_SUPPORTED: {
        // Dang. Let's do it the hard way.
        static const byte ZEROS[4096] = { 0 };

        while (size > sizeof(ZEROS)) {
          write(offset, ZEROS);
          size -= sizeof(ZEROS);
          offset += sizeof(ZEROS);
        }
        write(offset, kj::arrayPtr(ZEROS, size));
        break;
      }

      default:
        KJ_FAIL_WIN32("DeviceIoControl(FSCTL_SET_ZERO_DATA)", error);
        break;
    }
  }

  void truncate(uint64_t size) const {
    // SetEndOfFile() would require seeking the file. It looks like SetFileInformationByHandle()
    // lets us avoid this!
    FILE_END_OF_FILE_INFO info;
    memset(&info, 0, sizeof(info));
    info.EndOfFile.QuadPart = size;
    KJ_WIN32_HANDLE_ERRORS(
        SetFileInformationByHandle(handle, FileEndOfFileInfo, &info, sizeof(info))) {
      case ERROR_CALL_NOT_IMPLEMENTED: {
        // Wine doesn't implement this. :(

        LONG currentHigh = 0;
        LONG currentLow = SetFilePointer(handle, 0, &currentHigh, FILE_CURRENT);
        if (currentLow == INVALID_SET_FILE_POINTER) {
          KJ_FAIL_WIN32("SetFilePointer", GetLastError());
        }
        uint64_t current = (uint64_t(currentHigh) << 32) | uint64_t((ULONG)currentLow);

        LONG endLow = size & 0x00000000ffffffffull;
        LONG endHigh = size >> 32;
        if (SetFilePointer(handle, endLow, &endHigh, FILE_BEGIN) == INVALID_SET_FILE_POINTER) {
          KJ_FAIL_WIN32("SetFilePointer", GetLastError());
        }

        KJ_WIN32(SetEndOfFile(handle));

        if (current < size) {
          if (SetFilePointer(handle, currentLow, &currentHigh, FILE_BEGIN) ==
                  INVALID_SET_FILE_POINTER) {
            KJ_FAIL_WIN32("SetFilePointer", GetLastError());
          }
        }

        break;
      }
      default:
        KJ_FAIL_WIN32("SetFileInformationByHandle", error);
    }
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

      // Nothing needed here -- NT tracks dirty pages.
    }

    void sync(ArrayPtr<byte> slice) const override {
      KJ_REQUIRE(slice.begin() >= bytes.begin() && slice.end() <= bytes.end(),
                 "byte range is not part of this mapping");

      // Zero is treated specially by FlushViewOfFile(), so check for it. (This also handles the
      // case where `bytes` is actually empty and not a real mapping.)
      if (slice.size() > 0) {
        KJ_WIN32(FlushViewOfFile(slice.begin(), slice.size()));
      }
    }

  private:
    Array<byte> bytes;
  };

  Own<const WritableFileMapping> mmapWritable(uint64_t offset, uint64_t size) const {
    if (size == 0) {
      // Windows won't allow zero-length mappings
      return heap<WritableFileMappingImpl>(nullptr);
    }
    auto range = getMmapRange(offset, size);
    void* mapping = win32Mmap(handle, range, PAGE_READWRITE, FILE_MAP_ALL_ACCESS);
    auto array = Array<byte>(reinterpret_cast<byte*>(mapping) + (offset - range.offset),
                             size, mmapDisposer);
    return heap<WritableFileMappingImpl>(kj::mv(array));
  }

  // copy() is not optimized on Windows.

  // ReadableDirectory ---------------------------------------------------------

  template <typename Func>
  auto list(bool needTypes, Func&& func) const
      -> Array<Decay<decltype(func(instance<StringPtr>(), instance<FsNode::Type>()))>> {
    PathPtr path = KJ_ASSERT_NONNULL(dirPath);
    auto glob = join16(path.forWin32Api(true), L"*");

    // TODO(someday): Use GetFileInformationByHandleEx() with FileIdBothDirectoryInfo to enumerate
    //   directories instead. It's much cleaner.
    WIN32_FIND_DATAW data;
    HANDLE handle = FindFirstFileW(glob.begin(), &data);
    if (handle == INVALID_HANDLE_VALUE) {
      auto error = GetLastError();
      if (error == ERROR_FILE_NOT_FOUND) return nullptr;
      KJ_FAIL_WIN32("FindFirstFile", error, dbgStr(glob));
    }
    KJ_DEFER(KJ_WIN32(FindClose(handle)) { break; });

    typedef Decay<decltype(func(instance<StringPtr>(), instance<FsNode::Type>()))> Entry;
    kj::Vector<Entry> entries;

    do {
      auto name = decodeUtf16(
          arrayPtr(reinterpret_cast<char16_t*>(data.cFileName), wcslen(data.cFileName)));
      if (name != "." && name != ".." && !name.startsWith(HIDDEN_PREFIX)) {
        entries.add(func(name, modeToType(data.dwFileAttributes, data.dwReserved0)));
      }
    } while (FindNextFileW(handle, &data));

    auto error = GetLastError();
    if (error != ERROR_NO_MORE_FILES) {
      KJ_FAIL_WIN32("FindNextFile", error, path);
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
    DWORD result = GetFileAttributesW(nativePath(path).begin());
    if (result == INVALID_FILE_ATTRIBUTES) {
      auto error = GetLastError();
      switch (error) {
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
          return false;
        default:
          KJ_FAIL_WIN32("GetFileAttributesEx(path)", error, path) { return false; }
      }
    } else {
      return true;
    }
  }

  Maybe<FsNode::Metadata> tryLstat(PathPtr path) const {
    // We use FindFirstFileW() because in the case of symlinks it will return info about the
    // symlink rather than info about the target.
    WIN32_FIND_DATAW data;
    HANDLE handle = FindFirstFileW(nativePath(path).begin(), &data);
    if (handle == INVALID_HANDLE_VALUE) {
      auto error = GetLastError();
      if (error == ERROR_FILE_NOT_FOUND) return nullptr;
      KJ_FAIL_WIN32("FindFirstFile", error, path);
    } else {
      KJ_WIN32(FindClose(handle));
      return statToMetadata(data);
    }
  }

  Maybe<Own<const ReadableFile>> tryOpenFile(PathPtr path) const {
    HANDLE newHandle;
    KJ_WIN32_HANDLE_ERRORS(newHandle = CreateFileW(
        nativePath(path).begin(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL)) {
      case ERROR_FILE_NOT_FOUND:
      case ERROR_PATH_NOT_FOUND:
        return nullptr;
      default:
        KJ_FAIL_WIN32("CreateFile(path, OPEN_EXISTING)", error, path) { return nullptr; }
    }

    return newDiskReadableFile(kj::AutoCloseHandle(newHandle));
  }

  Maybe<AutoCloseHandle> tryOpenSubdirInternal(PathPtr path) const {
    HANDLE newHandle;
    KJ_WIN32_HANDLE_ERRORS(newHandle = CreateFileW(
        nativePath(path).begin(),
        GENERIC_READ,
        // When opening directories, we do NOT use FILE_SHARE_DELETE, because we need the directory
        // path to remain valid.
        //
        // TODO(someday): Use NtCreateFile() and related "internal" APIs that allow for
        //   openat()-like behavior?
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,  // apparently, this flag is required for directories
        NULL)) {
      case ERROR_FILE_NOT_FOUND:
      case ERROR_PATH_NOT_FOUND:
        return nullptr;
      default:
        KJ_FAIL_WIN32("CreateFile(directoryPath, OPEN_EXISTING)", error, path) { return nullptr; }
    }

    kj::AutoCloseHandle ownHandle(newHandle);

    BY_HANDLE_FILE_INFORMATION info;
    KJ_WIN32(GetFileInformationByHandle(ownHandle, &info));

    KJ_REQUIRE(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY, "not a directory", path);
    return kj::mv(ownHandle);
  }

  Maybe<Own<const ReadableDirectory>> tryOpenSubdir(PathPtr path) const {
    return tryOpenSubdirInternal(path).map([&](AutoCloseHandle&& handle) {
      return newDiskReadableDirectory(kj::mv(handle), KJ_ASSERT_NONNULL(dirPath).append(path));
    });
  }

  Maybe<String> tryReadlink(PathPtr path) const {
    // Windows symlinks work differently from Unix. Generally they are set up by the system
    // administrator and apps are expected to treat them transparently. Hence, on Windows, we act
    // as if nothing is a symlink by always returning null here.
    // TODO(someday): If we want to treat Windows symlinks more like Unix ones, start by reverting
    //   the comment that added this comment.
    return nullptr;
  }

  // Directory -----------------------------------------------------------------

  static LPSECURITY_ATTRIBUTES makeSecAttr(WriteMode mode) {
    if (has(mode, WriteMode::PRIVATE)) {
      KJ_UNIMPLEMENTED("WriteMode::PRIVATE on Win32 is not implemented");
    }

    return nullptr;
  }

  bool tryMkdir(PathPtr path, WriteMode mode, bool noThrow) const {
    // Internal function to make a directory.

    auto filename = nativePath(path);

    KJ_WIN32_HANDLE_ERRORS(CreateDirectoryW(filename.begin(), makeSecAttr(mode))) {
      case ERROR_ALREADY_EXISTS:
      case ERROR_FILE_EXISTS: {
        // Apparently this path exists.
        if (!has(mode, WriteMode::MODIFY)) {
          // Require exclusive create.
          return false;
        }

        // MODIFY is allowed, so we just need to check whether the existing entry is a directory.
        DWORD attr = GetFileAttributesW(filename.begin());
        if (attr == INVALID_FILE_ATTRIBUTES) {
          // CreateDirectory() says it already exists but we can't get attributes. Maybe it's a
          // dangling link, or maybe we can't access it for some reason. Assume failure.
          //
          // TODO(someday): Maybe we should be creating the directory at the target of the
          //   link?
          goto failed;
        }
        return attr & FILE_ATTRIBUTE_DIRECTORY;
      }
      case ERROR_PATH_NOT_FOUND:
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
          KJ_FAIL_WIN32("CreateDirectory", error, path);
        }
    }

    return true;
  }

  kj::Maybe<Array<wchar_t>> createNamedTemporary(
      PathPtr finalName, WriteMode mode, Path& kjTempPath,
      Function<BOOL(const wchar_t*)> tryCreate) const {
    // Create a temporary file which will eventually replace `finalName`.
    //
    // Calls `tryCreate` to actually create the temporary, passing in the desired path. tryCreate()
    // is expected to behave like a win32 call, returning a BOOL and setting `GetLastError()` on
    // error. tryCreate() MUST fail with ERROR_{FILE,ALREADY}_EXISTS if the path exists -- this is
    // not checked in advance, since it needs to be checked atomically. In the case of
    // ERROR_*_EXISTS, tryCreate() will be called again with a new path.
    //
    // Returns the temporary path that succeeded. Only returns nullptr if there was an exception
    // but we're compiled with -fno-exceptions.
    //
    // The optional parameter `kjTempPath` is filled in with the KJ Path of the temporary.

    if (finalName.size() == 0) {
      KJ_FAIL_REQUIRE("can't replace self") { break; }
      return nullptr;
    }

    static uint counter = 0;
    static const DWORD pid = GetCurrentProcessId();
    auto tempName = kj::str(HIDDEN_PREFIX, pid, '.', counter++, '.',
                            finalName.basename()[0], ".partial");
    kjTempPath = finalName.parent().append(tempName);
    auto path = nativePath(kjTempPath);

    KJ_WIN32_HANDLE_ERRORS(tryCreate(path.begin())) {
      case ERROR_ALREADY_EXISTS:
      case ERROR_FILE_EXISTS:
        // Try again with a new counter value.
        return createNamedTemporary(finalName, mode, kj::mv(tryCreate));
      case ERROR_PATH_NOT_FOUND:
        if (has(mode, WriteMode::CREATE_PARENT) && finalName.size() > 1 &&
            tryMkdir(finalName.parent(), WriteMode::CREATE | WriteMode::MODIFY |
                                         WriteMode::CREATE_PARENT, true)) {
          // Retry, but make sure we don't try to create the parent again.
          mode = mode - WriteMode::CREATE_PARENT;
          return createNamedTemporary(finalName, mode, kj::mv(tryCreate));
        }
        KJ_FALLTHROUGH;
      default:
        KJ_FAIL_WIN32("create(path)", error, path) { break; }
        return nullptr;
    }

    return kj::mv(path);
  }

  kj::Maybe<Array<wchar_t>> createNamedTemporary(
      PathPtr finalName, WriteMode mode, Function<BOOL(const wchar_t*)> tryCreate) const {
    Path dummy = nullptr;
    return createNamedTemporary(finalName, mode, dummy, kj::mv(tryCreate));
  }

  bool tryReplaceNode(PathPtr path, WriteMode mode,
                      Function<BOOL(const wchar_t*)> tryCreate) const {
    // Replaces the given path with an object created by calling tryCreate().
    //
    // tryCreate() must behave like a win32 call which creates the node at the path passed to it,
    // returning FALSE error. If the path passed to tryCreate already exists, it MUST fail with
    // ERROR_{FILE,ALREADY}_EXISTS.
    //
    // When `mode` includes MODIFY, replaceNode() reacts to ERROR_*_EXISTS by creating the
    // node in a temporary location and then rename()ing it into place.

    if (path.size() == 0) {
      KJ_FAIL_REQUIRE("can't replace self") { return false; }
    }

    auto filename = nativePath(path);

    if (has(mode, WriteMode::CREATE)) {
      // First try just cerating the node in-place.
      KJ_WIN32_HANDLE_ERRORS(tryCreate(filename.begin())) {
        case ERROR_ALREADY_EXISTS:
        case ERROR_FILE_EXISTS:
          // Target exists.
          if (has(mode, WriteMode::MODIFY)) {
            // Fall back to MODIFY path, below.
            break;
          } else {
            return false;
          }
        case ERROR_PATH_NOT_FOUND:
          if (has(mode, WriteMode::CREATE_PARENT) && path.size() > 0 &&
              tryMkdir(path.parent(), WriteMode::CREATE | WriteMode::MODIFY |
                                      WriteMode::CREATE_PARENT, true)) {
            // Retry, but make sure we don't try to create the parent again.
            return tryReplaceNode(path, mode - WriteMode::CREATE_PARENT, kj::mv(tryCreate));
          }
          KJ_FALLTHROUGH;
        default:
          KJ_FAIL_WIN32("create(path)", error, path) { return false; }
      } else {
        // Success.
        return true;
      }
    }

    // Either we don't have CREATE mode or the target already exists. We need to perform a
    // replacement instead.

    KJ_IF_MAYBE(tempPath, createNamedTemporary(path, mode, kj::mv(tryCreate))) {
      if (tryCommitReplacement(path, *tempPath, mode)) {
        return true;
      } else {
        KJ_WIN32_HANDLE_ERRORS(DeleteFileW(tempPath->begin())) {
          case ERROR_FILE_NOT_FOUND:
            // meh
            break;
          default:
            KJ_FAIL_WIN32("DeleteFile(tempPath)", error, dbgStr(*tempPath));
        }
        return false;
      }
    } else {
      // threw, but exceptions are disabled
      return false;
    }
  }

  Maybe<AutoCloseHandle> tryOpenFileInternal(PathPtr path, WriteMode mode, bool append) const {
    DWORD disposition;
    if (has(mode, WriteMode::MODIFY)) {
      if (has(mode, WriteMode::CREATE)) {
        disposition = OPEN_ALWAYS;
      } else {
        disposition = OPEN_EXISTING;
      }
    } else {
      if (has(mode, WriteMode::CREATE)) {
        disposition = CREATE_NEW;
      } else {
        // Neither CREATE nor MODIFY -- impossible to satisfy preconditions.
        return nullptr;
      }
    }

    DWORD access = GENERIC_READ | GENERIC_WRITE;
    if (append) {
      // FILE_GENERIC_WRITE includes both FILE_APPEND_DATA and FILE_WRITE_DATA, but we only want
      // the former. There are also a zillion other bits that we need, annoyingly.
      access = (FILE_READ_ATTRIBUTES | FILE_GENERIC_WRITE) & ~FILE_WRITE_DATA;
    }

    auto filename = path.toString();

    HANDLE newHandle;
    KJ_WIN32_HANDLE_ERRORS(newHandle = CreateFileW(
        nativePath(path).begin(),
        access,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        makeSecAttr(mode),
        disposition,
        FILE_ATTRIBUTE_NORMAL,
        NULL)) {
      case ERROR_PATH_NOT_FOUND:
        if (has(mode, WriteMode::CREATE)) {
          // A parent directory didn't exist. Maybe create it.
          if (has(mode, WriteMode::CREATE_PARENT) && path.size() > 0 &&
              tryMkdir(path.parent(), WriteMode::CREATE | WriteMode::MODIFY |
                                      WriteMode::CREATE_PARENT, true)) {
            // Retry, but make sure we don't try to create the parent again.
            return tryOpenFileInternal(path, mode - WriteMode::CREATE_PARENT, append);
          }

          KJ_FAIL_REQUIRE("parent is not a directory", path) { return nullptr; }
        } else {
          // MODIFY-only mode. ERROR_PATH_NOT_FOUND = parent path doesn't exist = return null.
          return nullptr;
        }
      case ERROR_FILE_NOT_FOUND:
        if (!has(mode, WriteMode::CREATE)) {
          // MODIFY-only mode. ERROR_FILE_NOT_FOUND = doesn't exist = return null.
          return nullptr;
        }
        goto failed;
      case ERROR_ALREADY_EXISTS:
      case ERROR_FILE_EXISTS:
        if (!has(mode, WriteMode::MODIFY)) {
          // CREATE-only mode. ERROR_ALREADY_EXISTS = already exists = return null.
          return nullptr;
        }
        goto failed;
      default:
      failed:
        KJ_FAIL_WIN32("CreateFile", error, path) { return nullptr; }
    }

    return kj::AutoCloseHandle(newHandle);
  }

  bool tryCommitReplacement(
      PathPtr toPath, ArrayPtr<const wchar_t> fromPath,
      WriteMode mode, kj::Maybe<kj::PathPtr> pathForCreatingParents = nullptr) const {
    // Try to use MoveFileEx() to replace `toPath` with `fromPath`.

    auto wToPath = nativePath(toPath);

    DWORD flags = has(mode, WriteMode::MODIFY) ? MOVEFILE_REPLACE_EXISTING : 0;

    if (!has(mode, WriteMode::CREATE)) {
      // Non-atomically verify that target exists. There's no way to make this atomic.
      DWORD result = GetFileAttributesW(wToPath.begin());
      if (result == INVALID_FILE_ATTRIBUTES) {
        auto error = GetLastError();
        switch (error) {
          case ERROR_FILE_NOT_FOUND:
          case ERROR_PATH_NOT_FOUND:
            return false;
          default:
            KJ_FAIL_WIN32("GetFileAttributesEx(toPath)", error, toPath) { return false; }
        }
      }
    }

    KJ_WIN32_HANDLE_ERRORS(MoveFileExW(fromPath.begin(), wToPath.begin(), flags)) {
      case ERROR_ALREADY_EXISTS:
      case ERROR_FILE_EXISTS:
        // We must not be in MODIFY mode.
        return false;
      case ERROR_PATH_NOT_FOUND:
        KJ_IF_MAYBE(p, pathForCreatingParents) {
          if (has(mode, WriteMode::CREATE_PARENT) &&
              p->size() > 0 && tryMkdir(p->parent(),
                  WriteMode::CREATE | WriteMode::MODIFY | WriteMode::CREATE_PARENT, true)) {
            // Retry, but make sure we don't try to create the parent again.
            return tryCommitReplacement(toPath, fromPath, mode - WriteMode::CREATE_PARENT);
          }
        }
        goto default_;

      case ERROR_ACCESS_DENIED: {
        // This often means that the target already exists and cannot be replaced, e.g. because
        // it is a directory. Move it out of the way first, then move our replacement in, then
        // delete the old thing.

        if (has(mode, WriteMode::MODIFY)) {
          KJ_IF_MAYBE(tempName,
              createNamedTemporary(toPath, WriteMode::CREATE, [&](const wchar_t* tempName2) {
            return MoveFileW(wToPath.begin(), tempName2);
          })) {
            KJ_WIN32_HANDLE_ERRORS(MoveFileW(fromPath.begin(), wToPath.begin())) {
              default:
                // Try to move back.
                MoveFileW(tempName->begin(), wToPath.begin());
                KJ_FAIL_WIN32("MoveFile", error, dbgStr(fromPath), dbgStr(wToPath)) {
                  return false;
                }
            }

            // Succeeded, delete temporary.
            rmrf(*tempName);
            return true;
          } else {
            // createNamedTemporary() threw exception but exceptions are disabled.
            return false;
          }
        } else {
          // Not MODIFY, so no overwrite allowed. If the file really does exist, we need to return
          // false.
          if (GetFileAttributesW(wToPath.begin()) != INVALID_FILE_ATTRIBUTES) {
            return false;
          }
        }

        goto default_;
      }

      default:
      default_:
        KJ_FAIL_WIN32("MoveFileEx", error, dbgStr(wToPath), dbgStr(fromPath)) { return false; }
    }

    return true;
  }

  template <typename T>
  class ReplacerImpl final: public Directory::Replacer<T> {
  public:
    ReplacerImpl(Own<T>&& object, const DiskHandle& parentDirectory,
                 Array<wchar_t>&& tempPath, Path&& path, WriteMode mode)
        : Directory::Replacer<T>(mode),
          object(kj::mv(object)), parentDirectory(parentDirectory),
          tempPath(kj::mv(tempPath)), path(kj::mv(path)) {}

    ~ReplacerImpl() noexcept(false) {
      if (!committed) {
        object = Own<T>();  // Force close of handle before trying to delete.

        if (kj::isSameType<T, File>()) {
          KJ_WIN32(DeleteFileW(tempPath.begin())) { break; }
        } else {
          rmrfChildren(tempPath);
          KJ_WIN32(RemoveDirectoryW(tempPath.begin())) { break; }
        }
      }
    }

    const T& get() override {
      return *object;
    }

    bool tryCommit() override {
      KJ_ASSERT(!committed, "already committed") { return false; }

      // For directories, we intentionally don't use FILE_SHARE_DELETE on our handle because if the
      // directory name changes our paths would be wrong. But, this means we can't rename the
      // directory here to commit it. So, we need to close the handle and then re-open it
      // afterwards. Ick.
      AutoCloseHandle* objectHandle = getHandlePointerHack(*object);
      if (kj::isSameType<T, Directory>()) {
        *objectHandle = nullptr;
      }
      KJ_DEFER({
        if (kj::isSameType<T, Directory>()) {
          HANDLE newHandle = nullptr;
          KJ_WIN32(newHandle = CreateFileW(
              committed ? parentDirectory.nativePath(path).begin() : tempPath.begin(),
              GENERIC_READ,
              FILE_SHARE_READ | FILE_SHARE_WRITE,
              NULL,
              OPEN_EXISTING,
              FILE_FLAG_BACKUP_SEMANTICS,  // apparently, this flag is required for directories
              NULL)) { return; }
          *objectHandle = AutoCloseHandle(newHandle);
          *getPathPointerHack(*object) = KJ_ASSERT_NONNULL(parentDirectory.dirPath).append(path);
        }
      });

      return committed = parentDirectory.tryCommitReplacement(
          path, tempPath, Directory::Replacer<T>::mode);
    }

  private:
    Own<T> object;
    const DiskHandle& parentDirectory;
    Array<wchar_t> tempPath;
    Path path;
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
    HANDLE newHandle_;
    KJ_IF_MAYBE(temp, createNamedTemporary(path, mode,
        [&](const wchar_t* candidatePath) {
      newHandle_ = CreateFileW(
          candidatePath,
          GENERIC_READ | GENERIC_WRITE,
          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
          makeSecAttr(mode),
          CREATE_NEW,
          FILE_ATTRIBUTE_NORMAL,
          NULL);
      return newHandle_ != INVALID_HANDLE_VALUE;
    })) {
      AutoCloseHandle newHandle(newHandle_);
      return heap<ReplacerImpl<File>>(newDiskFile(kj::mv(newHandle)), *this, kj::mv(*temp),
                                      path.clone(), mode);
    } else {
      // threw, but exceptions are disabled
      return heap<BrokenReplacer<File>>(newInMemoryFile(nullClock()));
    }
  }

  Own<const File> createTemporary() const {
    HANDLE newHandle_;
    KJ_IF_MAYBE(temp, createNamedTemporary(Path("unnamed"), WriteMode::CREATE,
        [&](const wchar_t* candidatePath) {
      newHandle_ = CreateFileW(
          candidatePath,
          GENERIC_READ | GENERIC_WRITE,
          0,
          NULL,   // TODO(someday): makeSecAttr(WriteMode::PRIVATE), when it's implemented
          CREATE_NEW,
          FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,
          NULL);
      return newHandle_ != INVALID_HANDLE_VALUE;
    })) {
      AutoCloseHandle newHandle(newHandle_);
      return newDiskFile(kj::mv(newHandle));
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

    return tryOpenSubdirInternal(path).map([&](AutoCloseHandle&& handle) {
      return newDiskDirectory(kj::mv(handle), KJ_ASSERT_NONNULL(dirPath).append(path));
    });
  }

  Own<Directory::Replacer<Directory>> replaceSubdir(PathPtr path, WriteMode mode) const {
    Path kjTempPath = nullptr;
    KJ_IF_MAYBE(temp, createNamedTemporary(path, mode, kjTempPath,
        [&](const wchar_t* candidatePath) {
      return CreateDirectoryW(candidatePath, makeSecAttr(mode));
    })) {
      HANDLE subdirHandle_;
      KJ_WIN32_HANDLE_ERRORS(subdirHandle_ = CreateFileW(
          temp->begin(),
          GENERIC_READ,
          FILE_SHARE_READ | FILE_SHARE_WRITE,
          NULL,
          OPEN_EXISTING,
          FILE_FLAG_BACKUP_SEMANTICS,  // apparently, this flag is required for directories
          NULL)) {
        default:
          KJ_FAIL_WIN32("CreateFile(just-created-temporary, OPEN_EXISTING)", error, path) {
            goto fail;
          }
      }

      AutoCloseHandle subdirHandle(subdirHandle_);
      return heap<ReplacerImpl<Directory>>(
          newDiskDirectory(kj::mv(subdirHandle),
              KJ_ASSERT_NONNULL(dirPath).append(kj::mv(kjTempPath))),
          *this, kj::mv(*temp), path.clone(), mode);
    } else {
      // threw, but exceptions are disabled
    fail:
      return heap<BrokenReplacer<Directory>>(newInMemoryDirectory(nullClock()));
    }
  }

  bool trySymlink(PathPtr linkpath, StringPtr content, WriteMode mode) const {
    // We can't really create symlinks on Windows. Reasons:
    // - We'd need to know whether the target is a file or a directory to pass the correct flags.
    //   That means we'd need to evaluate the link content and track down the target. What if the
    //   target doesn't exist? It's unclear if this is even allowed on Windows.
    // - Apparently, creating symlinks is a privileged operation on Windows prior to Windows 10.
    //   The flag SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE is very new.
    KJ_UNIMPLEMENTED(
        "Creating symbolic links is not supported on Windows due to semantic differences.");
  }

  bool tryTransfer(PathPtr toPath, WriteMode toMode,
                   const Directory& fromDirectory, PathPtr fromPath,
                   TransferMode mode, const Directory& self) const {
    KJ_REQUIRE(toPath.size() > 0, "can't replace self") { return false; }

    // Try to get the "from" path.
    Array<wchar_t> rawFromPath;
#if !KJ_NO_RTTI
    // Oops, dynamicDowncastIfAvailable() doesn't work since this isn't a downcast, it's a
    // side-cast...
    if (auto dh = dynamic_cast<const DiskHandle*>(&fromDirectory)) {
      rawFromPath = dh->nativePath(fromPath);
    } else
#endif
    KJ_IF_MAYBE(h, fromDirectory.getWin32Handle()) {
      // Can't downcast to DiskHandle, but getWin32Handle() returns a handle... maybe RTTI is
      // disabled? Or maybe this is some kind of wrapper?
      rawFromPath = getPathFromHandle(*h).append(fromPath).forWin32Api(true);
    } else {
      // Not a disk directory, so fall back to default implementation.
      return self.Directory::tryTransfer(toPath, toMode, fromDirectory, fromPath, mode);
    }

    if (mode == TransferMode::LINK) {
      return tryReplaceNode(toPath, toMode, [&](const wchar_t* candidatePath) {
        return CreateHardLinkW(candidatePath, rawFromPath.begin(), NULL);
      });
    } else if (mode == TransferMode::MOVE) {
      return tryCommitReplacement(toPath, rawFromPath, toMode, toPath);
    } else if (mode == TransferMode::COPY) {
      // We can accellerate copies on Windows.

      if (!has(toMode, WriteMode::CREATE)) {
        // Non-atomically verify that target exists. There's no way to make this atomic.
        if (!exists(toPath)) return false;
      }

      bool failIfExists = !has(toMode, WriteMode::MODIFY);
      KJ_WIN32_HANDLE_ERRORS(
          CopyFileW(rawFromPath.begin(), nativePath(toPath).begin(), failIfExists)) {
        case ERROR_ALREADY_EXISTS:
        case ERROR_FILE_EXISTS:
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
          return false;
        case ERROR_ACCESS_DENIED:
          // This usually means that fromPath was a directory or toPath was a directory. Fall back
          // to default implementation.
          break;
        default:
          KJ_FAIL_WIN32("CopyFile", error, fromPath, toPath) { return false; }
      } else {
        // Copy succeeded.
        return true;
      }
    }

    // OK, we can't do anything efficient using the OS. Fall back to default implementation.
    return self.Directory::tryTransfer(toPath, toMode, fromDirectory, fromPath, mode);
  }

  bool tryRemove(PathPtr path) const {
    return rmrf(nativePath(path));
  }
};

#define FSNODE_METHODS                                              \
  Maybe<void*> getWin32Handle() const override { return DiskHandle::getWin32Handle(); } \
                                                                    \
  Metadata stat() const override { return DiskHandle::stat(); }     \
  void sync() const override { DiskHandle::sync(); }                \
  void datasync() const override { DiskHandle::datasync(); }

class DiskReadableFile final: public ReadableFile, public DiskHandle {
public:
  DiskReadableFile(AutoCloseHandle&& handle): DiskHandle(kj::mv(handle), nullptr) {}

  Own<const FsNode> cloneFsNode() const override {
    return heap<DiskReadableFile>(DiskHandle::clone());
  }

  FSNODE_METHODS

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

class DiskAppendableFile final: public AppendableFile, public DiskHandle {
public:
  DiskAppendableFile(AutoCloseHandle&& handle)
      : DiskHandle(kj::mv(handle), nullptr),
        stream(DiskHandle::handle.get()) {}

  Own<const FsNode> cloneFsNode() const override {
    return heap<DiskAppendableFile>(DiskHandle::clone());
  }

  FSNODE_METHODS

  void write(const void* buffer, size_t size) override { stream.write(buffer, size); }
  void write(ArrayPtr<const ArrayPtr<const byte>> pieces) override {
    implicitCast<OutputStream&>(stream).write(pieces);
  }

private:
  HandleOutputStream stream;
};

class DiskFile final: public File, public DiskHandle {
public:
  DiskFile(AutoCloseHandle&& handle): DiskHandle(kj::mv(handle), nullptr) {}

  Own<const FsNode> cloneFsNode() const override {
    return heap<DiskFile>(DiskHandle::clone());
  }

  FSNODE_METHODS

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
  // copy() is not optimized on Windows.
};

class DiskReadableDirectory final: public ReadableDirectory, public DiskHandle {
public:
  DiskReadableDirectory(AutoCloseHandle&& handle, Path&& path)
      : DiskHandle(kj::mv(handle), kj::mv(path)) {}

  Own<const FsNode> cloneFsNode() const override {
    return heap<DiskReadableDirectory>(DiskHandle::clone(), KJ_ASSERT_NONNULL(dirPath).clone());
  }

  FSNODE_METHODS

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

class DiskDirectoryBase: public Directory, public DiskHandle {
public:
  DiskDirectoryBase(AutoCloseHandle&& handle, Path&& path)
      : DiskHandle(kj::mv(handle), kj::mv(path)) {}

  bool exists(PathPtr path) const override { return DiskHandle::exists(path); }
  Maybe<FsNode::Metadata> tryLstat(PathPtr path) const override { return DiskHandle::tryLstat(path); }
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

class DiskDirectory final: public DiskDirectoryBase {
public:
  DiskDirectory(AutoCloseHandle&& handle, Path&& path)
      : DiskDirectoryBase(kj::mv(handle), kj::mv(path)) {}

  Own<const FsNode> cloneFsNode() const override {
    return heap<DiskDirectory>(DiskHandle::clone(), KJ_ASSERT_NONNULL(dirPath).clone());
  }

  FSNODE_METHODS

  Array<String> listNames() const override { return DiskHandle::listNames(); }
  Array<Entry> listEntries() const override { return DiskHandle::listEntries(); }
  Own<const File> createTemporary() const override {
    return DiskHandle::createTemporary();
  }
};

class RootDiskDirectory final: public DiskDirectoryBase {
  // On Windows, the root directory is special.
  //
  // HACK: We only override a few functions of DiskDirectory, and we rely on the fact that
  //   Path::forWin32Api(true) throws an exception complaining about missing drive letter if the
  //   path is totally empty.

public:
  RootDiskDirectory(): DiskDirectoryBase(nullptr, Path(nullptr)) {}

  Own<const FsNode> cloneFsNode() const override {
    return heap<RootDiskDirectory>();
  }

  Metadata stat() const override {
    return { Type::DIRECTORY, 0, 0, UNIX_EPOCH, 1, 0 };
  }
  void sync() const override {}
  void datasync() const override {}

  Array<String> listNames() const override {
    return KJ_MAP(e, listEntries()) { return kj::mv(e.name); };
  }
  Array<Entry> listEntries() const override {
    DWORD drives = GetLogicalDrives();
    if (drives == 0) {
      KJ_FAIL_WIN32("GetLogicalDrives()", GetLastError()) { return nullptr; }
    }

    Vector<Entry> results;
    for (uint i = 0; i < 26; i++) {
      if (drives & (1 << i)) {
        char name[2] = { static_cast<char>('A' + i), ':' };
        results.add(Entry { FsNode::Type::DIRECTORY, kj::heapString(name, 2) });
      }
    }

    return results.releaseAsArray();
  }

  Own<const File> createTemporary() const override {
    KJ_FAIL_REQUIRE("can't create temporaries in Windows pseudo-root directory (the drive list)");
  }
};

class DiskFilesystem final: public Filesystem {
public:
  DiskFilesystem()
      : DiskFilesystem(computeCurrentPath()) {}
  DiskFilesystem(Path currentPath)
      : current(KJ_ASSERT_NONNULL(root.tryOpenSubdirInternal(currentPath),
                      "path returned by GetCurrentDirectory() doesn't exist?"),
                kj::mv(currentPath)) {}

  const Directory& getRoot() const override {
    return root;
  }

  const Directory& getCurrent() const override {
    return current;
  }

  PathPtr getCurrentPath() const override {
    return KJ_ASSERT_NONNULL(current.dirPath);
  }

private:
  RootDiskDirectory root;
  DiskDirectory current;

  static Path computeCurrentPath() {
    DWORD tryLen = MAX_PATH;
    for (;;) {
      auto temp = kj::heapArray<wchar_t>(tryLen + 1);
      DWORD len = GetCurrentDirectoryW(temp.size(), temp.begin());
      if (len == 0) {
        KJ_FAIL_WIN32("GetCurrentDirectory", GetLastError()) { break; }
        return Path(".");
      }
      if (len < temp.size()) {
        return Path::parseWin32Api(temp.slice(0, len));
      }
      // Try again with new length.
      tryLen = len;
    }
  }
};

} // namespace

Own<ReadableFile> newDiskReadableFile(AutoCloseHandle fd) {
  return heap<DiskReadableFile>(kj::mv(fd));
}
Own<AppendableFile> newDiskAppendableFile(AutoCloseHandle fd) {
  return heap<DiskAppendableFile>(kj::mv(fd));
}
Own<File> newDiskFile(AutoCloseHandle fd) {
  return heap<DiskFile>(kj::mv(fd));
}
Own<ReadableDirectory> newDiskReadableDirectory(AutoCloseHandle fd) {
  return heap<DiskReadableDirectory>(kj::mv(fd), getPathFromHandle(fd));
}
static Own<ReadableDirectory> newDiskReadableDirectory(AutoCloseHandle fd, Path&& path) {
  return heap<DiskReadableDirectory>(kj::mv(fd), kj::mv(path));
}
Own<Directory> newDiskDirectory(AutoCloseHandle fd) {
  return heap<DiskDirectory>(kj::mv(fd), getPathFromHandle(fd));
}
static Own<Directory> newDiskDirectory(AutoCloseHandle fd, Path&& path) {
  return heap<DiskDirectory>(kj::mv(fd), kj::mv(path));
}

Own<Filesystem> newDiskFilesystem() {
  return heap<DiskFilesystem>();
}

static AutoCloseHandle* getHandlePointerHack(Directory& dir) {
  return &static_cast<DiskDirectoryBase&>(dir).handle;
}
static Path* getPathPointerHack(Directory& dir) {
  return &KJ_ASSERT_NONNULL(static_cast<DiskDirectoryBase&>(dir).dirPath);
}

} // namespace kj

#endif  // _WIN32
