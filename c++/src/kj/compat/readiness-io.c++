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

#include "readiness-io.h"
#include <kj/debug.h>

namespace kj {

static size_t copyInto(kj::ArrayPtr<byte> dst, kj::ArrayPtr<const byte>& src) {
  size_t n = kj::min(dst.size(), src.size());
  memcpy(dst.begin(), src.begin(), n);
  src = src.slice(n, src.size());
  return n;
}

// =======================================================================================

ReadyInputStreamWrapper::ReadyInputStreamWrapper(AsyncInputStream& input): input(input) {}
ReadyInputStreamWrapper::~ReadyInputStreamWrapper() noexcept(false) {}

kj::Maybe<size_t> ReadyInputStreamWrapper::read(kj::ArrayPtr<byte> dst) {
  if (eof || dst.size() == 0) return size_t(0);

  if (content.size() == 0) {
    // No data available. Try to read more.
    if (!isPumping) {
      isPumping = true;
      pumpTask = kj::evalNow([&]() {
        return input.tryRead(buffer, 1, sizeof(buffer)).then([this](size_t n) {
          if (n == 0) {
            eof = true;
          } else {
            content = kj::arrayPtr(buffer, n);
          }
          isPumping = false;
        });
      }).fork();
    }

    return nullptr;
  }

  return copyInto(dst, content);
}

kj::Promise<void> ReadyInputStreamWrapper::whenReady() {
  return pumpTask.addBranch();
}

// =======================================================================================

ReadyOutputStreamWrapper::ReadyOutputStreamWrapper(AsyncOutputStream& output): output(output) {}
ReadyOutputStreamWrapper::~ReadyOutputStreamWrapper() noexcept(false) {}

kj::Maybe<size_t> ReadyOutputStreamWrapper::write(kj::ArrayPtr<const byte> data) {
  KJ_REQUIRE(!ended, "already ended");

  if (filled == sizeof(buffer)) {
    // No space.
    return nullptr;
  }

  uint end = start + filled;
  size_t result = 0;
  if (end < sizeof(buffer)) {
    // The filled part of the buffer is somewhere in the middle.

    // Copy into space after filled space.
    result += copyInto(kj::arrayPtr(buffer + end, buffer + sizeof(buffer)), data);

    // Copy into space before filled space.
    result += copyInto(kj::arrayPtr(buffer, buffer + start), data);
  } else {
    // Fill currently loops, so we only have one segment of empty space to copy into.

    // Copy into the space between the fill's end and the fill's start.
    result += copyInto(kj::arrayPtr(buffer + end % sizeof(buffer), buffer + start), data);
  }

  filled += result;

  if (!isPumping && shouldPump()) {
    isPumping = true;
    pumpTask = kj::evalNow([&]() {
      return pump();
    }).fork();
  }

  return result;
}

kj::Promise<void> ReadyOutputStreamWrapper::flush(AsyncOutputStream::WriteType type) {
  KJ_REQUIRE(!ended, "already ended");

  ended = type == AsyncOutputStream::WriteType::END;

  nextWriteType = kj::max(type, nextWriteType);
  if (!isPumping && shouldPump()) {
    isPumping = true;
    pumpTask = kj::evalNow([&]() {
      return pump();
    }).fork();
  }

  if (isPumping) {
    return pumpTask.addBranch();
  } else {
    return kj::READY_NOW;
  }
}

kj::Promise<void> ReadyOutputStreamWrapper::whenReady() {
  if (isPumping) {
    // While there might be space in the buffer now, it's best if the caller waits until the
    // underlying output signals that it wants more data.
    return pumpTask.addBranch();
  } else {
    return kj::READY_NOW;
  }
}

inline bool ReadyOutputStreamWrapper::shouldPump() {
  // Only actively pump if the buffer is more than half full or we need to apply a writeType.
  return nextWriteType != AsyncOutputStream::WriteType::PARTIAL || filled > sizeof(buffer) / 2;
}

kj::Promise<void> ReadyOutputStreamWrapper::pump() {
  uint oldFilled = filled;
  uint end = start + filled;

  kj::Promise<void> promise = nullptr;
  if (end <= sizeof(buffer)) {
    promise = output.write(nextWriteType, kj::arrayPtr(buffer + start, filled));
  } else {
    end = end % sizeof(buffer);
    kj::ArrayPtr<const byte> data = kj::arrayPtr(buffer + start, buffer + sizeof(buffer));
    moreData[0] = kj::arrayPtr(buffer, buffer + end);
    promise = output.write(nextWriteType, data, moreData);
  }

  nextWriteType = AsyncOutputStream::WriteType::PARTIAL;

  return promise.then([this,oldFilled,end]() -> kj::Promise<void> {
    filled -= oldFilled;
    start = end;

    if (shouldPump()) {
      return pump();
    } else {
      isPumping = false;
      return kj::READY_NOW;
    }
  });
}

}  // namespace kj

