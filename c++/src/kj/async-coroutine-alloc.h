// Copyright (c) 2026 Cloudflare, Inc. and contributors
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

// Various async coroutine allocators.

#pragma once

#include <kj/async.h>

#include <new>  // for placement new

#if __has_feature(address_sanitizer)
#include <sanitizer/asan_interface.h>
#define ASAN_POISON_MEMORY_REGION(addr, size) __asan_poison_memory_region((addr), (size))
#define ASAN_UNPOISON_MEMORY_REGION(addr, size) __asan_unpoison_memory_region((addr), (size))
#else
#define ASAN_POISON_MEMORY_REGION(addr, size) ((void)(addr), (void)(size))
#define ASAN_UNPOISON_MEMORY_REGION(addr, size) ((void)(addr), (void)(size))
#endif  // __SANITIZE_ADDRESS__

namespace kj {

template <typename Allocator>
struct CoroutineFrameHeader {
  // Every allocated coroutine frame starts with this header that tracks its allocator
  // (and possibly size in environments without sized deallocation).

  inline CoroutineFrameHeader(size_t dataSize, Allocator& allocator) noexcept
      :
#if !defined(__cpp_sized_deallocation)
        dataSize(dataSize),
#endif
        allocator(allocator) {
    static_assert(sizeof(CoroutineFrameHeader<Allocator>) % __STDCPP_DEFAULT_NEW_ALIGNMENT__ == 0,
                  "Frame header is not properly sized");
  }

#if !defined(__cpp_sized_deallocation)
  size_t dataSize;
#endif
  Allocator& allocator;

  alignas(__STDCPP_DEFAULT_NEW_ALIGNMENT__) kj::byte data[];
  // We need to guarantee the same alignment for data that new operator returns by default.
  // TODO(someday) consider using new operator with explicit align_val_t parameter.

  inline constexpr kj::byte* dataBegin() {
    return data;
  }
  inline constexpr static size_t allocSize(size_t dataSize) {
    return sizeof(CoroutineFrameHeader) + dataSize;
  }
  inline constexpr static CoroutineFrameHeader* fromDataPtr(void* dataPtr) {
    return reinterpret_cast<CoroutineFrameHeader*>(
        reinterpret_cast<kj::byte*>(dataPtr) - sizeof(CoroutineFrameHeader));
  }

#if defined(__cpp_sized_deallocation)
  inline static void free(void* dataPtr, size_t frameSize) {
    auto frame = fromDataPtr(dataPtr);
    frame->allocator.free(frame, frameSize);
  }
#else
  static void free(void* dataPtr) {
    auto frame = fromDataPtr(dataPtr);
    frame->allocator.free(frame, frame->dataSize);
  }
#endif
};

struct DebugCoroutineAllocator: public _::CoroutineAllocator {
  // Debug coroutine allocator that:
  // - keeps track of allocation statistics
  // - asserts that all coroutines were freed at its destructor.

  ~DebugCoroutineAllocator() {
    KJ_IREQUIRE(totalAllocCount == totalFreeCount, "Alloc/Free count mismatch");
    KJ_IREQUIRE(totalAllocSize == totalFreeSize, "Alloc/Free size mismatch");
  }

  using FrameHeader = CoroutineFrameHeader<DebugCoroutineAllocator>;

  void* alloc(std::size_t frameSize) {
    auto allocSize = FrameHeader::allocSize(frameSize);
    auto ptr = ::operator new(allocSize);
    auto frame = new (ptr) FrameHeader(frameSize, *this);

    totalAllocCount += 1;
    // increment by frame size since clients are not interested in our
    // implementation details
    totalAllocSize += frameSize;
    return frame->dataBegin();
  }

#if defined(__cpp_sized_deallocation)
  inline static void free(void* dataPtr, size_t frameSize) {
    FrameHeader::free(dataPtr, frameSize);
  }
#else
  static void free(void* dataPtr) { FrameHeader::free(dataPtr); }
#endif

  size_t totalAllocCount = 0;
  size_t totalAllocSize = 0;
  size_t totalFreeCount = 0;
  size_t totalFreeSize = 0;

 private:
  void free(FrameHeader* frame, size_t frameSize) {
    totalFreeCount++;
    totalFreeSize += frameSize;
    // Use unsized delete for maximum compatibility - performance is irrelevant
    // here.
    ::operator delete(reinterpret_cast<void*>(frame));
  }

  friend FrameHeader;
};

class CoroutineStack: public _::CoroutineAllocator {
  using FrameHeader = CoroutineFrameHeader<CoroutineStack>;

  struct Chunk {
    // Single chunk of stack data. Right now stack consists of only one chunk,
    // but this might change.

    Chunk() { ASAN_POISON_MEMORY_REGION(bytes, sizeof(bytes)); }

    kj::byte bytes[32 * 1024];
    size_t offset = 0;

    constexpr size_t size() { return sizeof(bytes); };

    kj::byte* alloc(size_t n) {
      KJ_IREQUIRE(offset + n < size(), "stack overflow: not implemented");
      auto ptr = bytes + offset;
      offset += n;
      ASAN_UNPOISON_MEMORY_REGION(ptr, n);
      return ptr;
    }

    void free(kj::byte* ptr, size_t n) {
      KJ_IREQUIRE(ptr + n == bytes + offset, "out of order free");
      offset -= n;
      ASAN_POISON_MEMORY_REGION(bytes + offset, n);
    }
  };

 public:

  // Allocator interface

  inline void* alloc(std::size_t frameSize) {
    auto allocSize = FrameHeader::allocSize(frameSize);
    auto ptr = chunk.alloc(allocSize);
    auto frame = new (ptr) FrameHeader(frameSize, *this);
    return frame->dataBegin();
  }

#if defined(__cpp_sized_deallocation)
  inline static void free(void* dataPtr, size_t frameSize) {
    FrameHeader::free(dataPtr, frameSize);
  }
#else
  static void free(void* dataPtr) { FrameHeader::free(dataPtr); }
#endif

private:
  inline void free(FrameHeader* frame, size_t dataSize) {
    chunk.free(reinterpret_cast<kj::byte*>(frame), FrameHeader::allocSize(dataSize));
  }

  Chunk chunk;

  friend FrameHeader;
};

}  // namespace kj
