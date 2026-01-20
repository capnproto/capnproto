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

#include <new> // for placement new

namespace kj {

template <typename Allocator> 
struct CoroutineFrame {
  // Represents allocated coroutine frame that tracks its size and allocator

  inline CoroutineFrame(size_t dataSize, Allocator &allocator) noexcept
      : dataSize(dataSize), allocator(allocator) {}

  size_t dataSize;
  Allocator &allocator;
  kj::byte data[];

  inline constexpr kj::byte *dataBegin() { return data; }
  inline constexpr size_t allocSize() {
    return sizeof(CoroutineFrame) + dataSize;
  }
  inline constexpr static size_t allocSize(size_t dataSize) {
    return sizeof(CoroutineFrame) + dataSize;
  }
  inline constexpr static CoroutineFrame *fromDataPtr(void *dataPtr) {
    return reinterpret_cast<CoroutineFrame *>(
        reinterpret_cast<kj::byte *>(dataPtr) - sizeof(CoroutineFrame));
  }
};

struct DebugCoroutineAllocator : public _::CoroutineAllocator {
  // Debug coroutine allocator that:
  // - keeps track of allocation statistics
  // - asserts that all coroutines were freed at its destructor.

  ~DebugCoroutineAllocator() {
    KJ_IREQUIRE(totalAllocCount == totalFreeCount, "Alloc/Free count mismatch");
    KJ_IREQUIRE(totalAllocSize == totalFreeSize, "Alloc/Free size mismatch");
  }

  using Frame = CoroutineFrame<DebugCoroutineAllocator>;

  void *alloc(std::size_t frameSize) {
    auto allocSize = Frame::allocSize(frameSize);
    auto ptr = ::operator new(allocSize);
    auto frame = new (ptr) Frame(frameSize, *this);

    totalAllocCount += 1;
    // increment by frame size since clients are not interested in our
    // implementation details
    totalAllocSize += frameSize;
    return frame->dataBegin();
  }

  static void free(void *dataPtr, size_t frameSize) {
    auto frame = Frame::fromDataPtr(dataPtr);
    KJ_IREQUIRE(frame->dataSize == frameSize, "Frame size mismatch");
    frame->allocator.free(frame);
  }

  static void free(void *dataPtr) {
    auto frame = Frame::fromDataPtr(dataPtr);
    frame->allocator.free(frame);
  }

  size_t totalAllocCount = 0;
  size_t totalAllocSize = 0;
  size_t totalFreeCount = 0;
  size_t totalFreeSize = 0;

private:
  void free(Frame *frame) {
    totalFreeCount++;
    totalFreeSize += frame->dataSize;
    // Use unsized delete for maximum compatibility - performance is irrelevant here.
    ::operator delete(reinterpret_cast<void *>(frame));
  }
};

} // namespace kj
