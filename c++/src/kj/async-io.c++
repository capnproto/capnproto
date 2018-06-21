// Copyright (c) 2013-2017 Sandstorm Development Group, Inc. and contributors
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
// Request Vista-level APIs.
#define WINVER 0x0600
#define _WIN32_WINNT 0x0600
#endif

#include "async-io.h"
#include "async-io-internal.h"
#include "debug.h"
#include "vector.h"
#include "io.h"
#include "one-of.h"

#if _WIN32
#include <winsock2.h>
#include <ws2ipdef.h>
#include <ws2tcpip.h>
#include "windows-sanity.h"
#define inet_pton InetPtonA
#define inet_ntop InetNtopA
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/un.h>
#endif

namespace kj {

Promise<void> AsyncInputStream::read(void* buffer, size_t bytes) {
  return read(buffer, bytes, bytes).then([](size_t) {});
}

Promise<size_t> AsyncInputStream::read(void* buffer, size_t minBytes, size_t maxBytes) {
  return tryRead(buffer, minBytes, maxBytes).then([=](size_t result) {
    if (result >= minBytes) {
      return result;
    } else {
      kj::throwRecoverableException(KJ_EXCEPTION(DISCONNECTED, "stream disconnected prematurely"));
      // Pretend we read zeros from the input.
      memset(reinterpret_cast<byte*>(buffer) + result, 0, minBytes - result);
      return minBytes;
    }
  });
}

Maybe<uint64_t> AsyncInputStream::tryGetLength() { return nullptr; }

namespace {

class AsyncPump {
public:
  AsyncPump(AsyncInputStream& input, AsyncOutputStream& output, uint64_t limit)
      : input(input), output(output), limit(limit) {}

  Promise<uint64_t> pump() {
    // TODO(perf): This could be more efficient by reading half a buffer at a time and then
    //   starting the next read concurrent with writing the data from the previous read.

    uint64_t n = kj::min(limit - doneSoFar, sizeof(buffer));
    if (n == 0) return doneSoFar;

    return input.tryRead(buffer, 1, n)
        .then([this](size_t amount) -> Promise<uint64_t> {
      if (amount == 0) return doneSoFar;  // EOF
      doneSoFar += amount;
      return output.write(buffer, amount)
          .then([this]() {
        return pump();
      });
    });
  }

private:
  AsyncInputStream& input;
  AsyncOutputStream& output;
  uint64_t limit;
  uint64_t doneSoFar = 0;
  byte buffer[4096];
};

}  // namespace

Promise<uint64_t> AsyncInputStream::pumpTo(
    AsyncOutputStream& output, uint64_t amount) {
  // See if output wants to dispatch on us.
  KJ_IF_MAYBE(result, output.tryPumpFrom(*this, amount)) {
    return kj::mv(*result);
  }

  // OK, fall back to naive approach.
  auto pump = heap<AsyncPump>(*this, output, amount);
  auto promise = pump->pump();
  return promise.attach(kj::mv(pump));
}

namespace {

class AllReader {
public:
  AllReader(AsyncInputStream& input): input(input) {}

  Promise<Array<byte>> readAllBytes(uint64_t limit) {
    return loop(limit).then([this, limit](uint64_t headroom) {
      auto out = heapArray<byte>(limit - headroom);
      copyInto(out);
      return out;
    });
  }

  Promise<String> readAllText(uint64_t limit) {
    return loop(limit).then([this, limit](uint64_t headroom) {
      auto out = heapArray<char>(limit - headroom + 1);
      copyInto(out.slice(0, out.size() - 1).asBytes());
      out.back() = '\0';
      return String(kj::mv(out));
    });
  }

private:
  AsyncInputStream& input;
  Vector<Array<byte>> parts;

  Promise<uint64_t> loop(uint64_t limit) {
    KJ_REQUIRE(limit > 0, "Reached limit before EOF.");

    auto part = heapArray<byte>(kj::min(4096, limit));
    auto partPtr = part.asPtr();
    parts.add(kj::mv(part));
    return input.tryRead(partPtr.begin(), partPtr.size(), partPtr.size())
        .then([this,KJ_CPCAP(partPtr),limit](size_t amount) mutable -> Promise<uint64_t> {
      limit -= amount;
      if (amount < partPtr.size()) {
        return limit;
      } else {
        return loop(limit);
      }
    });
  }

  void copyInto(ArrayPtr<byte> out) {
    size_t pos = 0;
    for (auto& part: parts) {
      size_t n = kj::min(part.size(), out.size() - pos);
      memcpy(out.begin() + pos, part.begin(), n);
      pos += n;
    }
  }
};

}  // namespace

Promise<Array<byte>> AsyncInputStream::readAllBytes(uint64_t limit) {
  auto reader = kj::heap<AllReader>(*this);
  auto promise = reader->readAllBytes(limit);
  return promise.attach(kj::mv(reader));
}

Promise<String> AsyncInputStream::readAllText(uint64_t limit) {
  auto reader = kj::heap<AllReader>(*this);
  auto promise = reader->readAllText(limit);
  return promise.attach(kj::mv(reader));
}

Maybe<Promise<uint64_t>> AsyncOutputStream::tryPumpFrom(
    AsyncInputStream& input, uint64_t amount) {
  return nullptr;
}

namespace {

class AsyncPipe final: public AsyncIoStream, public Refcounted {
public:
  ~AsyncPipe() noexcept(false) {
    KJ_REQUIRE(state == nullptr || ownState.get() != nullptr,
        "destroying AsyncPipe with operation still in-progress; probably going to segfault") {
      // Don't std::terminate().
      break;
    }
  }

  Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
    if (minBytes == 0) {
      return size_t(0);
    } else KJ_IF_MAYBE(s, state) {
      return s->tryRead(buffer, minBytes, maxBytes);
    } else {
      return newAdaptedPromise<size_t, BlockedRead>(
          *this, arrayPtr(reinterpret_cast<byte*>(buffer), maxBytes), minBytes);
    }
  }

  Promise<uint64_t> pumpTo(AsyncOutputStream& output, uint64_t amount) override {
    if (amount == 0) {
      return uint64_t(0);
    } else KJ_IF_MAYBE(s, state) {
      return s->pumpTo(output, amount);
    } else {
      return newAdaptedPromise<uint64_t, BlockedPumpTo>(*this, output, amount);
    }
  }

  void abortRead() override {
    KJ_IF_MAYBE(s, state) {
      s->abortRead();
    } else {
      ownState = kj::heap<AbortedRead>();
      state = *ownState;
    }
  }

  Promise<void> write(const void* buffer, size_t size) override {
    if (size == 0) {
      return READY_NOW;
    } else KJ_IF_MAYBE(s, state) {
      return s->write(buffer, size);
    } else {
      return newAdaptedPromise<void, BlockedWrite>(
          *this, arrayPtr(reinterpret_cast<const byte*>(buffer), size), nullptr);
    }
  }

  Promise<void> write(ArrayPtr<const ArrayPtr<const byte>> pieces) override {
    while (pieces.size() > 0 && pieces[0].size() == 0) {
      pieces = pieces.slice(1, pieces.size());
    }

    if (pieces.size() == 0) {
      return kj::READY_NOW;
    } else KJ_IF_MAYBE(s, state) {
      return s->write(pieces);
    } else {
      return newAdaptedPromise<void, BlockedWrite>(
          *this, pieces[0], pieces.slice(1, pieces.size()));
    }
  }

  Maybe<Promise<uint64_t>> tryPumpFrom(
      AsyncInputStream& input, uint64_t amount) override {
    if (amount == 0) {
      return Promise<uint64_t>(uint64_t(0));
    } else KJ_IF_MAYBE(s, state) {
      return s->tryPumpFrom(input, amount);
    } else {
      return newAdaptedPromise<uint64_t, BlockedPumpFrom>(*this, input, amount);
    }
  }

  void shutdownWrite() override {
    KJ_IF_MAYBE(s, state) {
      s->shutdownWrite();
    } else {
      ownState = kj::heap<ShutdownedWrite>();
      state = *ownState;
    }
  }

private:
  Maybe<AsyncIoStream&> state;
  // Object-oriented state! If any method call is blocked waiting on activity from the other end,
  // then `state` is non-null and method calls should be forwarded to it. If no calls are
  // outstanding, `state` is null.

  kj::Own<AsyncIoStream> ownState;

  void endState(AsyncIoStream& obj) {
    KJ_IF_MAYBE(s, state) {
      if (s == &obj) {
        state = nullptr;
      }
    }
  }

  class BlockedWrite final: public AsyncIoStream {
    // AsyncPipe state when a write() is currently waiting for a corresponding read().

  public:
    BlockedWrite(PromiseFulfiller<void>& fulfiller, AsyncPipe& pipe,
                 ArrayPtr<const byte> writeBuffer,
                 ArrayPtr<const ArrayPtr<const byte>> morePieces)
        : fulfiller(fulfiller), pipe(pipe), writeBuffer(writeBuffer), morePieces(morePieces) {
      KJ_REQUIRE(pipe.state == nullptr);
      pipe.state = *this;
    }

    ~BlockedWrite() noexcept(false) {
      pipe.endState(*this);
    }

    Promise<size_t> tryRead(void* readBufferPtr, size_t minBytes, size_t maxBytes) override {
      KJ_REQUIRE(canceler.isEmpty(), "already pumping");

      auto readBuffer = arrayPtr(reinterpret_cast<byte*>(readBufferPtr), maxBytes);

      size_t totalRead = 0;
      while (readBuffer.size() >= writeBuffer.size()) {
        // The whole current write buffer can be copied into the read buffer.

        {
          auto n = writeBuffer.size();
          memcpy(readBuffer.begin(), writeBuffer.begin(), n);
          totalRead += n;
          readBuffer = readBuffer.slice(n, readBuffer.size());
        }

        if (morePieces.size() == 0) {
          // All done writing.
          fulfiller.fulfill();
          pipe.endState(*this);

          if (totalRead >= minBytes) {
            // Also all done reading.
            return totalRead;
          } else {
            return pipe.tryRead(readBuffer.begin(), minBytes - totalRead, readBuffer.size())
                .then([totalRead](size_t amount) { return amount + totalRead; });
          }
        }

        writeBuffer = morePieces[0];
        morePieces = morePieces.slice(1, morePieces.size());
      }

      // At this point, the read buffer is smaller than the current write buffer, so we can fill
      // it completely.
      {
        auto n = readBuffer.size();
        memcpy(readBuffer.begin(), writeBuffer.begin(), n);
        writeBuffer = writeBuffer.slice(n, writeBuffer.size());
        totalRead += n;
      }

      return totalRead;
    }

    Promise<uint64_t> pumpTo(AsyncOutputStream& output, uint64_t amount) override {
      KJ_REQUIRE(canceler.isEmpty(), "already pumping");

      if (amount < writeBuffer.size()) {
        // Consume a portion of the write buffer.
        return canceler.wrap(output.write(writeBuffer.begin(), amount)
            .then([this,amount]() {
          writeBuffer = writeBuffer.slice(amount, writeBuffer.size());
          // We pumped the full amount, so we're done pumping.
          return amount;
        }));
      }

      // First piece doesn't cover the whole pump. Figure out how many more pieces to add.
      uint64_t actual = writeBuffer.size();
      size_t i = 0;
      while (i < morePieces.size() &&
             amount >= actual + morePieces[i].size()) {
        actual += morePieces[i++].size();
      }

      // Write the first piece.
      auto promise = output.write(writeBuffer.begin(), writeBuffer.size());

      // Write full pieces as a single gather-write.
      if (i > 0) {
        auto more = morePieces.slice(0, i);
        promise = promise.then([&output,more]() { return output.write(more); });
      }

      if (i == morePieces.size()) {
        // This will complete the write.
        return canceler.wrap(promise.then([this,&output,amount,actual]() -> Promise<uint64_t> {
          canceler.release();
          fulfiller.fulfill();
          pipe.endState(*this);

          if (actual == amount) {
            // Oh, we had exactly enough.
            return actual;
          } else {
            return pipe.pumpTo(output, amount - actual)
                .then([actual](uint64_t actual2) { return actual + actual2; });
          }
        }));
      } else {
        // Pump ends mid-piece. Write the last, partial piece.
        auto n = amount - actual;
        auto splitPiece = morePieces[i];
        KJ_ASSERT(n <= splitPiece.size());
        auto newWriteBuffer = splitPiece.slice(n, splitPiece.size());
        auto newMorePieces = morePieces.slice(i + 1, morePieces.size());
        auto prefix = splitPiece.slice(0, n);
        if (prefix.size() > 0) {
          promise = promise.then([&output,prefix]() {
            return output.write(prefix.begin(), prefix.size());
          });
        }

        return canceler.wrap(promise.then([this,newWriteBuffer,newMorePieces,amount]() {
          writeBuffer = newWriteBuffer;
          morePieces = newMorePieces;
          canceler.release();
          return amount;
        }));
      }
    }

    void abortRead() override {
      canceler.cancel("abortRead() was called");
      fulfiller.reject(KJ_EXCEPTION(DISCONNECTED, "read end of pipe was aborted"));
      pipe.endState(*this);
      pipe.abortRead();
    }

    Promise<void> write(const void* buffer, size_t size) override {
      KJ_FAIL_REQUIRE("can't write() again until previous write() completes");
    }
    Promise<void> write(ArrayPtr<const ArrayPtr<const byte>> pieces) override {
      KJ_FAIL_REQUIRE("can't write() again until previous write() completes");
    }
    Maybe<Promise<uint64_t>> tryPumpFrom(AsyncInputStream& input, uint64_t amount) override {
      KJ_FAIL_REQUIRE("can't tryPumpFrom() again until previous write() completes");
    }
    void shutdownWrite() override {
      KJ_FAIL_REQUIRE("can't shutdownWrite() until previous write() completes");
    }

  private:
    PromiseFulfiller<void>& fulfiller;
    AsyncPipe& pipe;
    ArrayPtr<const byte> writeBuffer;
    ArrayPtr<const ArrayPtr<const byte>> morePieces;
    Canceler canceler;
  };

  class BlockedPumpFrom final: public AsyncIoStream {
    // AsyncPipe state when a tryPumpFrom() is currently waiting for a corresponding read().

  public:
    BlockedPumpFrom(PromiseFulfiller<uint64_t>& fulfiller, AsyncPipe& pipe,
                    AsyncInputStream& input, uint64_t amount)
        : fulfiller(fulfiller), pipe(pipe), input(input), amount(amount) {
      KJ_REQUIRE(pipe.state == nullptr);
      pipe.state = *this;
    }

    ~BlockedPumpFrom() noexcept(false) {
      pipe.endState(*this);
    }

    Promise<size_t> tryRead(void* readBuffer, size_t minBytes, size_t maxBytes) override {
      KJ_REQUIRE(canceler.isEmpty(), "already pumping");

      auto pumpLeft = amount - pumpedSoFar;
      auto min = kj::min(pumpLeft, minBytes);
      auto max = kj::min(pumpLeft, maxBytes);
      return canceler.wrap(input.tryRead(readBuffer, min, max)
          .then([this,readBuffer,minBytes,maxBytes,min](size_t actual) -> kj::Promise<size_t> {
        canceler.release();
        pumpedSoFar += actual;
        KJ_ASSERT(pumpedSoFar <= amount);

        if (pumpedSoFar == amount || actual < min) {
          // Either we pumped all we wanted or we hit EOF.
          fulfiller.fulfill(kj::cp(pumpedSoFar));
          pipe.endState(*this);
        }

        if (actual >= minBytes) {
          return actual;
        } else {
          return pipe.tryRead(reinterpret_cast<byte*>(readBuffer) + actual,
                              minBytes - actual, maxBytes - actual)
              .then([actual](size_t actual2) { return actual + actual2; });
        }
      }));
    }

    Promise<uint64_t> pumpTo(AsyncOutputStream& output, uint64_t amount2) override {
      KJ_REQUIRE(canceler.isEmpty(), "already pumping");

      auto n = kj::min(amount2, amount - pumpedSoFar);
      return canceler.wrap(input.pumpTo(output, n)
          .then([this,&output,amount2,n](uint64_t actual) -> Promise<uint64_t> {
        canceler.release();
        pumpedSoFar += actual;
        KJ_ASSERT(pumpedSoFar <= amount);
        if (pumpedSoFar == amount) {
          fulfiller.fulfill(kj::cp(amount));
          pipe.endState(*this);
        }

        KJ_ASSERT(actual <= amount2);
        if (actual == amount2) {
          // Completed entire pumpTo amount.
          return amount2;
        } else if (actual < n) {
          // Received less than requested, presumably because EOF.
          return actual;
        } else {
          // We received all the bytes that were requested but it didn't complete the pump.
          KJ_ASSERT(pumpedSoFar == amount);
          return pipe.pumpTo(output, amount2 - actual);
        }
      }));
    }

    void abortRead() override {
      canceler.cancel("abortRead() was called");

      // The input might have reached EOF, but we haven't detected it yet because we haven't tried
      // to read that far. If we had not optimized tryPumpFrom() and instead used the default
      // pumpTo() implementation, then the input would not have called write() again once it
      // reached EOF, and therefore the abortRead() on the other end would *not* propagate an
      // exception! We need the same behavior here. To that end, we need to detect if we're at EOF
      // by reading one last byte.
      checkEofTask = kj::evalNow([&]() {
        static char junk;
        return input.tryRead(&junk, 1, 1).then([this](uint64_t n) {
          if (n == 0) {
            fulfiller.fulfill(kj::cp(pumpedSoFar));
          } else {
            fulfiller.reject(KJ_EXCEPTION(DISCONNECTED, "read end of pipe was aborted"));
          }
        }).eagerlyEvaluate([this](kj::Exception&& e) {
          fulfiller.reject(kj::mv(e));
        });
      });

      pipe.endState(*this);
      pipe.abortRead();
    }

    Promise<void> write(const void* buffer, size_t size) override {
      KJ_FAIL_REQUIRE("can't write() again until previous tryPumpFrom() completes");
    }
    Promise<void> write(ArrayPtr<const ArrayPtr<const byte>> pieces) override {
      KJ_FAIL_REQUIRE("can't write() again until previous tryPumpFrom() completes");
    }
    Maybe<Promise<uint64_t>> tryPumpFrom(AsyncInputStream& input, uint64_t amount) override {
      KJ_FAIL_REQUIRE("can't tryPumpFrom() again until previous tryPumpFrom() completes");
    }
    void shutdownWrite() override {
      KJ_FAIL_REQUIRE("can't shutdownWrite() until previous tryPumpFrom() completes");
    }

  private:
    PromiseFulfiller<uint64_t>& fulfiller;
    AsyncPipe& pipe;
    AsyncInputStream& input;
    uint64_t amount;
    uint64_t pumpedSoFar = 0;
    Canceler canceler;
    kj::Promise<void> checkEofTask = nullptr;
  };

  class BlockedRead final: public AsyncIoStream {
    // AsyncPipe state when a tryRead() is currently waiting for a corresponding write().

  public:
    BlockedRead(PromiseFulfiller<size_t>& fulfiller, AsyncPipe& pipe,
                ArrayPtr<byte> readBuffer, size_t minBytes)
        : fulfiller(fulfiller), pipe(pipe), readBuffer(readBuffer), minBytes(minBytes) {
      KJ_REQUIRE(pipe.state == nullptr);
      pipe.state = *this;
    }

    ~BlockedRead() noexcept(false) {
      pipe.endState(*this);
    }

    Promise<size_t> tryRead(void* readBuffer, size_t minBytes, size_t maxBytes) override {
      KJ_FAIL_REQUIRE("can't read() again until previous read() completes");
    }
    Promise<uint64_t> pumpTo(AsyncOutputStream& output, uint64_t amount) override {
      KJ_FAIL_REQUIRE("can't read() again until previous read() completes");
    }

    void abortRead() override {
      canceler.cancel("abortRead() was called");
      fulfiller.reject(KJ_EXCEPTION(DISCONNECTED, "read end of pipe was aborted"));
      pipe.endState(*this);
      pipe.abortRead();
    }

    Promise<void> write(const void* writeBuffer, size_t size) override {
      KJ_REQUIRE(canceler.isEmpty(), "already pumping");

      if (size < readBuffer.size()) {
        // Consume a portion of the read buffer.
        memcpy(readBuffer.begin(), writeBuffer, size);
        readSoFar += size;
        readBuffer = readBuffer.slice(size, readBuffer.size());
        if (readSoFar >= minBytes) {
          // We've read enough to close out this read.
          fulfiller.fulfill(kj::cp(readSoFar));
          pipe.endState(*this);
        }
        return READY_NOW;
      } else {
        // Consume entire read buffer.
        auto n = readBuffer.size();
        fulfiller.fulfill(readSoFar + n);
        pipe.endState(*this);
        memcpy(readBuffer.begin(), writeBuffer, n);
        if (n == size) {
          // That's it.
          return READY_NOW;
        } else {
          return pipe.write(reinterpret_cast<const byte*>(writeBuffer) + n, size - n);
        }
      }
    }

    Promise<void> write(ArrayPtr<const ArrayPtr<const byte>> pieces) override {
      KJ_REQUIRE(canceler.isEmpty(), "already pumping");

      while (pieces.size() > 0) {
        if (pieces[0].size() < readBuffer.size()) {
          // Consume a portion of the read buffer.
          auto n = pieces[0].size();
          memcpy(readBuffer.begin(), pieces[0].begin(), n);
          readSoFar += n;
          readBuffer = readBuffer.slice(n, readBuffer.size());
          pieces = pieces.slice(1, pieces.size());
          // loop
        } else {
          // Consume entire read buffer.
          auto n = readBuffer.size();
          fulfiller.fulfill(readSoFar + n);
          pipe.endState(*this);
          memcpy(readBuffer.begin(), pieces[0].begin(), n);

          auto restOfPiece = pieces[0].slice(n, pieces[0].size());
          pieces = pieces.slice(1, pieces.size());
          if (restOfPiece.size() == 0) {
            // We exactly finished the current piece, so just issue a write for the remaining
            // pieces.
            if (pieces.size() == 0) {
              // Nothing left.
              return READY_NOW;
            } else {
              // Write remaining pieces.
              return pipe.write(pieces);
            }
          } else {
            // Unfortunately we have to execute a separate write() for the remaining part of this
            // piece, because we can't modify the pieces array.
            auto promise = pipe.write(restOfPiece.begin(), restOfPiece.size());
            if (pieces.size() > 0) {
              // No more pieces so that's it.
              return kj::mv(promise);
            } else {
              // Also need to write the remaining pieces.
              auto& pipeRef = pipe;
              return promise.then([pieces,&pipeRef]() {
                return pipeRef.write(pieces);
              });
            }
          }
        }
      }

      // Consumed all written pieces.
      if (readSoFar >= minBytes) {
        // We've read enough to close out this read.
        fulfiller.fulfill(kj::cp(readSoFar));
        pipe.endState(*this);
      }

      return READY_NOW;
    }

    Maybe<Promise<uint64_t>> tryPumpFrom(AsyncInputStream& input, uint64_t amount) override {
      KJ_REQUIRE(canceler.isEmpty(), "already pumping");

      KJ_ASSERT(minBytes > readSoFar);
      auto minToRead = kj::min(amount, minBytes - readSoFar);
      auto maxToRead = kj::min(amount, readBuffer.size());

      return canceler.wrap(input.tryRead(readBuffer.begin(), minToRead, maxToRead)
          .then([this,&input,amount,minToRead](size_t actual) -> Promise<uint64_t> {
        readBuffer = readBuffer.slice(actual, readBuffer.size());
        readSoFar += actual;

        if (readSoFar >= minBytes || actual < minToRead) {
          // We've read enough to close out this read (readSoFar >= minBytes)
          // OR we reached EOF and couldn't complete the read (actual < minToRead)
          // Either way, we want to close out this read.
          canceler.release();
          fulfiller.fulfill(kj::cp(readSoFar));
          pipe.endState(*this);

          if (actual < amount) {
            // We din't complete pumping. Restart from the pipe.
            return input.pumpTo(pipe, amount - actual)
                .then([actual](uint64_t actual2) -> uint64_t { return actual + actual2; });
          }
        }

        // If we read less than `actual`, but more than `minToRead`, it can only have been
        // because we reached `minBytes`, so the conditional above would have executed. So, here
        // we know that actual == amount.
        KJ_ASSERT(actual == amount);

        // We pumped the full amount, so we're done pumping.
        return amount;
      }));
    }

    void shutdownWrite() override {
      canceler.cancel("shutdownWrite() was called");
      fulfiller.fulfill(kj::cp(readSoFar));
      pipe.endState(*this);
      pipe.shutdownWrite();
    }

  private:
    PromiseFulfiller<size_t>& fulfiller;
    AsyncPipe& pipe;
    ArrayPtr<byte> readBuffer;
    size_t minBytes;
    size_t readSoFar = 0;
    Canceler canceler;
  };

  class BlockedPumpTo final: public AsyncIoStream {
    // AsyncPipe state when a pumpTo() is currently waiting for a corresponding write().

  public:
    BlockedPumpTo(PromiseFulfiller<uint64_t>& fulfiller, AsyncPipe& pipe,
                  AsyncOutputStream& output, uint64_t amount)
        : fulfiller(fulfiller), pipe(pipe), output(output), amount(amount) {
      KJ_REQUIRE(pipe.state == nullptr);
      pipe.state = *this;
    }

    ~BlockedPumpTo() noexcept(false) {
      pipe.endState(*this);
    }

    Promise<size_t> tryRead(void* readBuffer, size_t minBytes, size_t maxBytes) override {
      KJ_FAIL_REQUIRE("can't read() again until previous pumpTo() completes");
    }
    Promise<uint64_t> pumpTo(AsyncOutputStream& output, uint64_t amount) override {
      KJ_FAIL_REQUIRE("can't read() again until previous pumpTo() completes");
    }

    void abortRead() override {
      canceler.cancel("abortRead() was called");
      fulfiller.reject(KJ_EXCEPTION(DISCONNECTED, "read end of pipe was aborted"));
      pipe.endState(*this);
      pipe.abortRead();
    }

    Promise<void> write(const void* writeBuffer, size_t size) override {
      KJ_REQUIRE(canceler.isEmpty(), "already pumping");

      auto actual = kj::min(amount - pumpedSoFar, size);
      return canceler.wrap(output.write(writeBuffer, actual)
          .then([this,size,actual,writeBuffer]() -> kj::Promise<void> {
        canceler.release();
        pumpedSoFar += actual;

        KJ_ASSERT(pumpedSoFar <= amount);
        KJ_ASSERT(actual <= size);

        if (pumpedSoFar == amount) {
          // Done with pump.
          fulfiller.fulfill(kj::cp(pumpedSoFar));
          pipe.endState(*this);
        }

        if (actual == size) {
          return kj::READY_NOW;
        } else {
          KJ_ASSERT(pumpedSoFar == amount);
          return pipe.write(reinterpret_cast<const byte*>(writeBuffer) + actual, size - actual);
        }
      }));
    }

    Promise<void> write(ArrayPtr<const ArrayPtr<const byte>> pieces) override {
      KJ_REQUIRE(canceler.isEmpty(), "already pumping");

      size_t size = 0;
      size_t needed = amount - pumpedSoFar;
      for (auto i: kj::indices(pieces)) {
        if (pieces[i].size() > needed) {
          // The pump ends in the middle of this write.

          auto promise = output.write(pieces.slice(0, i));

          if (needed > 0) {
            // The pump includes part of this piece, but not all. Unfortunately we need to split
            // writes.
            auto partial = pieces[i].slice(0, needed);
            promise = promise.then([this,partial]() {
              return output.write(partial.begin(), partial.size());
            });
            auto partial2 = pieces[i].slice(needed, pieces[i].size());
            promise = canceler.wrap(promise.then([this,partial2]() {
              canceler.release();
              fulfiller.fulfill(kj::cp(amount));
              pipe.endState(*this);
              return pipe.write(partial2.begin(), partial2.size());
            }));
            ++i;
          } else {
            // The pump ends exactly at the end of a piece, how nice.
            promise = canceler.wrap(promise.then([this]() {
              canceler.release();
              fulfiller.fulfill(kj::cp(amount));
              pipe.endState(*this);
            }));
          }

          auto remainder = pieces.slice(i, pieces.size());
          if (remainder.size() > 0) {
            auto& pipeRef = pipe;
            promise = promise.then([&pipeRef,remainder]() {
              return pipeRef.write(remainder);
            });
          }

          return promise;
        } else {
          size += pieces[i].size();
          needed -= pieces[i].size();
        }
      }

      // Turns out we can forward this whole write.
      KJ_ASSERT(size <= amount - pumpedSoFar);
      return canceler.wrap(output.write(pieces).then([this,size]() {
        pumpedSoFar += size;
        KJ_ASSERT(pumpedSoFar <= amount);
        if (pumpedSoFar == amount) {
          // Done pumping.
          canceler.release();
          fulfiller.fulfill(kj::cp(amount));
          pipe.endState(*this);
        }
      }));
    }

    Maybe<Promise<uint64_t>> tryPumpFrom(AsyncInputStream& input, uint64_t amount2) override {
      KJ_REQUIRE(canceler.isEmpty(), "already pumping");

      auto n = kj::min(amount2, amount - pumpedSoFar);
      return output.tryPumpFrom(input, n)
          .map([&](Promise<uint64_t> subPump) {
        return canceler.wrap(subPump
            .then([this,&input,amount2,n](uint64_t actual) -> Promise<uint64_t> {
          canceler.release();
          pumpedSoFar += actual;
          KJ_ASSERT(pumpedSoFar <= amount);
          if (pumpedSoFar == amount) {
            fulfiller.fulfill(kj::cp(amount));
            pipe.endState(*this);
          }

          KJ_ASSERT(actual <= amount2);
          if (actual == amount2) {
            // Completed entire tryPumpFrom amount.
            return amount2;
          } else if (actual < n) {
            // Received less than requested, presumably because EOF.
            return actual;
          } else {
            // We received all the bytes that were requested but it didn't complete the pump.
            KJ_ASSERT(pumpedSoFar == amount);
            return input.pumpTo(pipe, amount2 - actual);
          }
        }));
      });
    }

    void shutdownWrite() override {
      canceler.cancel("shutdownWrite() was called");
      fulfiller.fulfill(kj::cp(pumpedSoFar));
      pipe.endState(*this);
      pipe.shutdownWrite();
    }

  private:
    PromiseFulfiller<uint64_t>& fulfiller;
    AsyncPipe& pipe;
    AsyncOutputStream& output;
    uint64_t amount;
    size_t pumpedSoFar = 0;
    Canceler canceler;
  };

  class AbortedRead final: public AsyncIoStream {
    // AsyncPipe state when abortRead() has been called.

  public:
    Promise<size_t> tryRead(void* readBufferPtr, size_t minBytes, size_t maxBytes) override {
      KJ_FAIL_REQUIRE("abortRead() has been called");
    }
    Promise<uint64_t> pumpTo(AsyncOutputStream& output, uint64_t amount) override {
      KJ_FAIL_REQUIRE("abortRead() has been called");
    }
    void abortRead() override {
      // ignore repeated abort
    }

    Promise<void> write(const void* buffer, size_t size) override {
      KJ_FAIL_REQUIRE("abortRead() has been called");
    }
    Promise<void> write(ArrayPtr<const ArrayPtr<const byte>> pieces) override {
      KJ_FAIL_REQUIRE("abortRead() has been called");
    }
    Maybe<Promise<uint64_t>> tryPumpFrom(AsyncInputStream& input, uint64_t amount) override {
      KJ_FAIL_REQUIRE("abortRead() has been called");
    }
    void shutdownWrite() override {
      // ignore -- currently shutdownWrite() actually means that the PipeWriteEnd was dropped,
      // which is not an error even if reads have been aborted.
    }
  };

  class ShutdownedWrite final: public AsyncIoStream {
    // AsyncPipe state when shutdownWrite() has been called.

  public:
    Promise<size_t> tryRead(void* readBufferPtr, size_t minBytes, size_t maxBytes) override {
      return size_t(0);
    }
    Promise<uint64_t> pumpTo(AsyncOutputStream& output, uint64_t amount) override {
      return uint64_t(0);
    }
    void abortRead() override {
      // ignore
    }

    Promise<void> write(const void* buffer, size_t size) override {
      KJ_FAIL_REQUIRE("shutdownWrite() has been called");
    }
    Promise<void> write(ArrayPtr<const ArrayPtr<const byte>> pieces) override {
      KJ_FAIL_REQUIRE("shutdownWrite() has been called");
    }
    Maybe<Promise<uint64_t>> tryPumpFrom(AsyncInputStream& input, uint64_t amount) override {
      KJ_FAIL_REQUIRE("shutdownWrite() has been called");
    }
    void shutdownWrite() override {
      // ignore -- currently shutdownWrite() actually means that the PipeWriteEnd was dropped,
      // so it will only be called once anyhow.
    }
  };
};

class PipeReadEnd final: public AsyncInputStream {
public:
  PipeReadEnd(kj::Own<AsyncPipe> pipe): pipe(kj::mv(pipe)) {}
  ~PipeReadEnd() noexcept(false) {
    unwind.catchExceptionsIfUnwinding([&]() {
      pipe->abortRead();
    });
  }

  Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
    return pipe->tryRead(buffer, minBytes, maxBytes);
  }

  Promise<uint64_t> pumpTo(AsyncOutputStream& output, uint64_t amount) override {
    return pipe->pumpTo(output, amount);
  }

private:
  Own<AsyncPipe> pipe;
  UnwindDetector unwind;
};

class PipeWriteEnd final: public AsyncOutputStream {
public:
  PipeWriteEnd(kj::Own<AsyncPipe> pipe): pipe(kj::mv(pipe)) {}
  ~PipeWriteEnd() noexcept(false) {
    unwind.catchExceptionsIfUnwinding([&]() {
      pipe->shutdownWrite();
    });
  }

  Promise<void> write(const void* buffer, size_t size) override {
    return pipe->write(buffer, size);
  }

  Promise<void> write(ArrayPtr<const ArrayPtr<const byte>> pieces) override {
    return pipe->write(pieces);
  }

  Maybe<Promise<uint64_t>> tryPumpFrom(
      AsyncInputStream& input, uint64_t amount) override {
    return pipe->tryPumpFrom(input, amount);
  }

private:
  Own<AsyncPipe> pipe;
  UnwindDetector unwind;
};

class TwoWayPipeEnd final: public AsyncIoStream {
public:
  TwoWayPipeEnd(kj::Own<AsyncPipe> in, kj::Own<AsyncPipe> out)
      : in(kj::mv(in)), out(kj::mv(out)) {}
  ~TwoWayPipeEnd() noexcept(false) {
    unwind.catchExceptionsIfUnwinding([&]() {
      out->shutdownWrite();
      in->abortRead();
    });
  }

  Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
    return in->tryRead(buffer, minBytes, maxBytes);
  }
  Promise<uint64_t> pumpTo(AsyncOutputStream& output, uint64_t amount) override {
    return in->pumpTo(output, amount);
  }
  void abortRead() override {
    in->abortRead();
  }

  Promise<void> write(const void* buffer, size_t size) override {
    return out->write(buffer, size);
  }
  Promise<void> write(ArrayPtr<const ArrayPtr<const byte>> pieces) override {
    return out->write(pieces);
  }
  Maybe<Promise<uint64_t>> tryPumpFrom(
      AsyncInputStream& input, uint64_t amount) override {
    return out->tryPumpFrom(input, amount);
  }
  void shutdownWrite() override {
    out->shutdownWrite();
  }

private:
  kj::Own<AsyncPipe> in;
  kj::Own<AsyncPipe> out;
  UnwindDetector unwind;
};

class LimitedInputStream final: public AsyncInputStream {
public:
  LimitedInputStream(kj::Own<AsyncInputStream> inner, uint64_t limit)
      : inner(kj::mv(inner)), limit(limit) {
    if (limit == 0) {
      inner = nullptr;
    }
  }

  Maybe<uint64_t> tryGetLength() override {
    return limit;
  }

  Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
    if (limit == 0) return size_t(0);
    return inner->tryRead(buffer, kj::min(minBytes, limit), kj::min(maxBytes, limit))
        .then([this,minBytes](size_t actual) {
      decreaseLimit(actual, minBytes);
      return actual;
    });
  }

  Promise<uint64_t> pumpTo(AsyncOutputStream& output, uint64_t amount) override {
    if (limit == 0) return uint64_t(0);
    auto requested = kj::min(amount, limit);
    return inner->pumpTo(output, requested)
        .then([this,requested](uint64_t actual) {
      decreaseLimit(actual, requested);
      return actual;
    });
  }

private:
  Own<AsyncInputStream> inner;
  uint64_t limit;

  void decreaseLimit(uint64_t amount, uint64_t requested) {
    KJ_ASSERT(limit >= amount);
    limit -= amount;
    if (limit == 0) {
      inner = nullptr;
    } else if (amount < requested) {
      KJ_FAIL_REQUIRE("pipe ended prematurely");
    }
  }
};

}  // namespace

OneWayPipe newOneWayPipe(kj::Maybe<uint64_t> expectedLength) {
  auto impl = kj::refcounted<AsyncPipe>();
  Own<AsyncInputStream> readEnd = kj::heap<PipeReadEnd>(kj::addRef(*impl));
  KJ_IF_MAYBE(l, expectedLength) {
    readEnd = kj::heap<LimitedInputStream>(kj::mv(readEnd), *l);
  }
  Own<AsyncOutputStream> writeEnd = kj::heap<PipeWriteEnd>(kj::mv(impl));
  return { kj::mv(readEnd), kj::mv(writeEnd) };
}

TwoWayPipe newTwoWayPipe() {
  auto pipe1 = kj::refcounted<AsyncPipe>();
  auto pipe2 = kj::refcounted<AsyncPipe>();
  auto end1 = kj::heap<TwoWayPipeEnd>(kj::addRef(*pipe1), kj::addRef(*pipe2));
  auto end2 = kj::heap<TwoWayPipeEnd>(kj::mv(pipe2), kj::mv(pipe1));
  return { { kj::mv(end1), kj::mv(end2) } };
}

Promise<Own<AsyncCapabilityStream>> AsyncCapabilityStream::receiveStream() {
  return tryReceiveStream()
      .then([](Maybe<Own<AsyncCapabilityStream>>&& result)
            -> Promise<Own<AsyncCapabilityStream>> {
    KJ_IF_MAYBE(r, result) {
      return kj::mv(*r);
    } else {
      return KJ_EXCEPTION(FAILED, "EOF when expecting to receive capability");
    }
  });
}

Promise<AutoCloseFd> AsyncCapabilityStream::receiveFd() {
  return tryReceiveFd().then([](Maybe<AutoCloseFd>&& result) -> Promise<AutoCloseFd> {
    KJ_IF_MAYBE(r, result) {
      return kj::mv(*r);
    } else {
      return KJ_EXCEPTION(FAILED, "EOF when expecting to receive capability");
    }
  });
}
Promise<Maybe<AutoCloseFd>> AsyncCapabilityStream::tryReceiveFd() {
  return KJ_EXCEPTION(UNIMPLEMENTED, "this stream cannot receive file descriptors");
}
Promise<void> AsyncCapabilityStream::sendFd(int fd) {
  return KJ_EXCEPTION(UNIMPLEMENTED, "this stream cannot send file descriptors");
}

void AsyncIoStream::getsockopt(int level, int option, void* value, uint* length) {
  KJ_UNIMPLEMENTED("Not a socket.");
}
void AsyncIoStream::setsockopt(int level, int option, const void* value, uint length) {
  KJ_UNIMPLEMENTED("Not a socket.");
}
void AsyncIoStream::getsockname(struct sockaddr* addr, uint* length) {
  KJ_UNIMPLEMENTED("Not a socket.");
}
void AsyncIoStream::getpeername(struct sockaddr* addr, uint* length) {
  KJ_UNIMPLEMENTED("Not a socket.");
}
void ConnectionReceiver::getsockopt(int level, int option, void* value, uint* length) {
  KJ_UNIMPLEMENTED("Not a socket.");
}
void ConnectionReceiver::setsockopt(int level, int option, const void* value, uint length) {
  KJ_UNIMPLEMENTED("Not a socket.");
}
void DatagramPort::getsockopt(int level, int option, void* value, uint* length) {
  KJ_UNIMPLEMENTED("Not a socket.");
}
void DatagramPort::setsockopt(int level, int option, const void* value, uint length) {
  KJ_UNIMPLEMENTED("Not a socket.");
}
Own<DatagramPort> NetworkAddress::bindDatagramPort() {
  KJ_UNIMPLEMENTED("Datagram sockets not implemented.");
}
Own<DatagramPort> LowLevelAsyncIoProvider::wrapDatagramSocketFd(
    Fd fd, LowLevelAsyncIoProvider::NetworkFilter& filter, uint flags) {
  KJ_UNIMPLEMENTED("Datagram sockets not implemented.");
}
#if !_WIN32
Own<AsyncCapabilityStream> LowLevelAsyncIoProvider::wrapUnixSocketFd(Fd fd, uint flags) {
  KJ_UNIMPLEMENTED("Unix socket with FD passing not implemented.");
}
#endif
CapabilityPipe AsyncIoProvider::newCapabilityPipe() {
  KJ_UNIMPLEMENTED("Capability pipes not implemented.");
}

Own<AsyncInputStream> LowLevelAsyncIoProvider::wrapInputFd(OwnFd&& fd, uint flags) {
  return wrapInputFd(reinterpret_cast<Fd>(fd.release()), flags | TAKE_OWNERSHIP);
}
Own<AsyncOutputStream> LowLevelAsyncIoProvider::wrapOutputFd(OwnFd&& fd, uint flags) {
  return wrapOutputFd(reinterpret_cast<Fd>(fd.release()), flags | TAKE_OWNERSHIP);
}
Own<AsyncIoStream> LowLevelAsyncIoProvider::wrapSocketFd(OwnFd&& fd, uint flags) {
  return wrapSocketFd(reinterpret_cast<Fd>(fd.release()), flags | TAKE_OWNERSHIP);
}
#if !_WIN32
Own<AsyncCapabilityStream> LowLevelAsyncIoProvider::wrapUnixSocketFd(OwnFd&& fd, uint flags) {
  return wrapUnixSocketFd(reinterpret_cast<Fd>(fd.release()), flags | TAKE_OWNERSHIP);
}
#endif
Promise<Own<AsyncIoStream>> LowLevelAsyncIoProvider::wrapConnectingSocketFd(
    OwnFd&& fd, const struct sockaddr* addr, uint addrlen, uint flags) {
  return wrapConnectingSocketFd(reinterpret_cast<Fd>(fd.release()), addr, addrlen,
                                flags | TAKE_OWNERSHIP);
}
Own<ConnectionReceiver> LowLevelAsyncIoProvider::wrapListenSocketFd(
    OwnFd&& fd, NetworkFilter& filter, uint flags) {
  return wrapListenSocketFd(reinterpret_cast<Fd>(fd.release()), filter, flags | TAKE_OWNERSHIP);
}
Own<ConnectionReceiver> LowLevelAsyncIoProvider::wrapListenSocketFd(OwnFd&& fd, uint flags) {
  return wrapListenSocketFd(reinterpret_cast<Fd>(fd.release()), flags | TAKE_OWNERSHIP);
}
Own<DatagramPort> LowLevelAsyncIoProvider::wrapDatagramSocketFd(
    OwnFd&& fd, NetworkFilter& filter, uint flags) {
  return wrapDatagramSocketFd(reinterpret_cast<Fd>(fd.release()), filter, flags | TAKE_OWNERSHIP);
}
Own<DatagramPort> LowLevelAsyncIoProvider::wrapDatagramSocketFd(OwnFd&& fd, uint flags) {
  return wrapDatagramSocketFd(reinterpret_cast<Fd>(fd.release()), flags | TAKE_OWNERSHIP);
}

namespace {

class DummyNetworkFilter: public kj::LowLevelAsyncIoProvider::NetworkFilter {
public:
  bool shouldAllow(const struct sockaddr* addr, uint addrlen) override { return true; }
};

}  // namespace

LowLevelAsyncIoProvider::NetworkFilter& LowLevelAsyncIoProvider::NetworkFilter::getAllAllowed() {
  static DummyNetworkFilter result;
  return result;
}

// =======================================================================================
// Convenience adapters.

Promise<Own<AsyncIoStream>> CapabilityStreamConnectionReceiver::accept() {
  return inner.receiveStream()
      .then([](Own<AsyncCapabilityStream>&& stream) -> Own<AsyncIoStream> {
    return kj::mv(stream);
  });
}

uint CapabilityStreamConnectionReceiver::getPort() {
  return 0;
}

Promise<Own<AsyncIoStream>> CapabilityStreamNetworkAddress::connect() {
  auto pipe = provider.newCapabilityPipe();
  auto result = kj::mv(pipe.ends[0]);
  return inner.sendStream(kj::mv(pipe.ends[1]))
      .then(kj::mvCapture(result, [](Own<AsyncIoStream>&& result) {
    return kj::mv(result);
  }));
}
Own<ConnectionReceiver> CapabilityStreamNetworkAddress::listen() {
  return kj::heap<CapabilityStreamConnectionReceiver>(inner);
}

Own<NetworkAddress> CapabilityStreamNetworkAddress::clone() {
  KJ_UNIMPLEMENTED("can't clone CapabilityStreamNetworkAddress");
}
String CapabilityStreamNetworkAddress::toString() {
  return kj::str("<CapabilityStreamNetworkAddress>");
}

// =======================================================================================

namespace _ {  // private

#if !_WIN32

kj::ArrayPtr<const char> safeUnixPath(const struct sockaddr_un* addr, uint addrlen) {
  KJ_REQUIRE(addr->sun_family == AF_UNIX, "not a unix address");
  KJ_REQUIRE(addrlen >= offsetof(sockaddr_un, sun_path), "invalid unix address");

  size_t maxPathlen = addrlen - offsetof(sockaddr_un, sun_path);

  size_t pathlen;
  if (maxPathlen > 0 && addr->sun_path[0] == '\0') {
    // Linux "abstract" unix address
    pathlen = strnlen(addr->sun_path + 1, maxPathlen - 1) + 1;
  } else {
    pathlen = strnlen(addr->sun_path, maxPathlen);
  }
  return kj::arrayPtr(addr->sun_path, pathlen);
}

#endif  // !_WIN32

CidrRange::CidrRange(StringPtr pattern) {
  size_t slashPos = KJ_REQUIRE_NONNULL(pattern.findFirst('/'), "invalid CIDR", pattern);

  bitCount = pattern.slice(slashPos + 1).parseAs<uint>();

  KJ_STACK_ARRAY(char, addr, slashPos + 1, 128, 128);
  memcpy(addr.begin(), pattern.begin(), slashPos);
  addr[slashPos] = '\0';

  if (pattern.findFirst(':') == nullptr) {
    family = AF_INET;
    KJ_REQUIRE(bitCount <= 32, "invalid CIDR", pattern);
  } else {
    family = AF_INET6;
    KJ_REQUIRE(bitCount <= 128, "invalid CIDR", pattern);
  }

  KJ_ASSERT(inet_pton(family, addr.begin(), bits) > 0, "invalid CIDR", pattern);
  zeroIrrelevantBits();
}

CidrRange::CidrRange(int family, ArrayPtr<const byte> bits, uint bitCount)
    : family(family), bitCount(bitCount) {
  if (family == AF_INET) {
    KJ_REQUIRE(bitCount <= 32);
  } else {
    KJ_REQUIRE(bitCount <= 128);
  }
  KJ_REQUIRE(bits.size() * 8 >= bitCount);
  size_t byteCount = (bitCount + 7) / 8;
  memcpy(this->bits, bits.begin(), byteCount);
  memset(this->bits + byteCount, 0, sizeof(this->bits) - byteCount);

  zeroIrrelevantBits();
}

CidrRange CidrRange::inet4(ArrayPtr<const byte> bits, uint bitCount) {
  return CidrRange(AF_INET, bits, bitCount);
}
CidrRange CidrRange::inet6(
    ArrayPtr<const uint16_t> prefix, ArrayPtr<const uint16_t> suffix,
    uint bitCount) {
  KJ_REQUIRE(prefix.size() + suffix.size() <= 8);

  byte bits[16] = { 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, };

  for (size_t i: kj::indices(prefix)) {
    bits[i * 2] = prefix[i] >> 8;
    bits[i * 2 + 1] = prefix[i] & 0xff;
  }

  byte* suffixBits = bits + (16 - suffix.size() * 2);
  for (size_t i: kj::indices(suffix)) {
    suffixBits[i * 2] = suffix[i] >> 8;
    suffixBits[i * 2 + 1] = suffix[i] & 0xff;
  }

  return CidrRange(AF_INET6, bits, bitCount);
}

bool CidrRange::matches(const struct sockaddr* addr) const {
  const byte* otherBits;

  switch (family) {
    case AF_INET:
      if (addr->sa_family == AF_INET6) {
        otherBits = reinterpret_cast<const struct sockaddr_in6*>(addr)->sin6_addr.s6_addr;
        static constexpr byte V6MAPPED[12] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff };
        if (memcmp(otherBits, V6MAPPED, sizeof(V6MAPPED)) == 0) {
          // We're an ipv4 range and the address is ipv6, but it's a "v6 mapped" address, meaning
          // it's equivalent to an ipv4 address. Try to match against the ipv4 part.
          otherBits = otherBits + sizeof(V6MAPPED);
        } else {
          return false;
        }
      } else if (addr->sa_family == AF_INET) {
        otherBits = reinterpret_cast<const byte*>(
            &reinterpret_cast<const struct sockaddr_in*>(addr)->sin_addr.s_addr);
      } else {
        return false;
      }

      break;

    case AF_INET6:
      if (addr->sa_family != AF_INET6) return false;

      otherBits = reinterpret_cast<const struct sockaddr_in6*>(addr)->sin6_addr.s6_addr;
      break;

    default:
      KJ_UNREACHABLE;
  }

  if (memcmp(bits, otherBits, bitCount / 8) != 0) return false;

  return bitCount == 128 ||
      bits[bitCount / 8] == (otherBits[bitCount / 8] & (0xff00 >> (bitCount % 8)));
}

bool CidrRange::matchesFamily(int family) const {
  switch (family) {
    case AF_INET:
      return this->family == AF_INET;
    case AF_INET6:
      // Even if we're a v4 CIDR, we can match v6 addresses in the v4-mapped range.
      return true;
    default:
      return false;
  }
}

String CidrRange::toString() const {
  char result[128];
  KJ_ASSERT(inet_ntop(family, (void*)bits, result, sizeof(result)) == result);
  return kj::str(result, '/', bitCount);
}

void CidrRange::zeroIrrelevantBits() {
  // Mask out insignificant bits of partial byte.
  if (bitCount < 128) {
    bits[bitCount / 8] &= 0xff00 >> (bitCount % 8);

    // Zero the remaining bytes.
    size_t n = bitCount / 8 + 1;
    memset(bits + n, 0, sizeof(bits) - n);
  }
}

// -----------------------------------------------------------------------------

ArrayPtr<const CidrRange> localCidrs() {
  static const CidrRange result[] = {
    // localhost
    "127.0.0.0/8"_kj,
    "::1/128"_kj,

    // Trying to *connect* to 0.0.0.0 on many systems is equivalent to connecting to localhost.
    // (wat)
    "0.0.0.0/32"_kj,
    "::/128"_kj,
  };

  // TODO(cleanup): A bug in GCC 4.8, fixed in 4.9, prevents result from implicitly
  //   casting to our return type.
  return kj::arrayPtr(result, kj::size(result));
}

ArrayPtr<const CidrRange> privateCidrs() {
  static const CidrRange result[] = {
    "10.0.0.0/8"_kj,            // RFC1918 reserved for internal network
    "100.64.0.0/10"_kj,         // RFC6598 "shared address space" for carrier-grade NAT
    "169.254.0.0/16"_kj,        // RFC3927 "link local" (auto-configured LAN in absence of DHCP)
    "172.16.0.0/12"_kj,         // RFC1918 reserved for internal network
    "192.168.0.0/16"_kj,        // RFC1918 reserved for internal network

    "fc00::/7"_kj,              // RFC4193 unique private network
    "fe80::/10"_kj,             // RFC4291 "link local" (auto-configured LAN in absence of DHCP)
  };

  // TODO(cleanup): A bug in GCC 4.8, fixed in 4.9, prevents result from implicitly
  //   casting to our return type.
  return kj::arrayPtr(result, kj::size(result));
}

ArrayPtr<const CidrRange> reservedCidrs() {
  static const CidrRange result[] = {
    "192.0.0.0/24"_kj,          // RFC6890 reserved for special protocols
    "224.0.0.0/4"_kj,           // RFC1112 multicast
    "240.0.0.0/4"_kj,           // RFC1112 multicast / reserved for future use
    "255.255.255.255/32"_kj,    // RFC0919 broadcast address

    "2001::/23"_kj,             // RFC2928 reserved for special protocols
    "ff00::/8"_kj,              // RFC4291 multicast
  };

  // TODO(cleanup): A bug in GCC 4.8, fixed in 4.9, prevents result from implicitly
  //   casting to our return type.
  return kj::arrayPtr(result, kj::size(result));
}

ArrayPtr<const CidrRange> exampleAddresses() {
  static const CidrRange result[] = {
    "192.0.2.0/24"_kj,          // RFC5737 "example address" block 1 -- like example.com for IPs
    "198.51.100.0/24"_kj,       // RFC5737 "example address" block 2 -- like example.com for IPs
    "203.0.113.0/24"_kj,        // RFC5737 "example address" block 3 -- like example.com for IPs
    "2001:db8::/32"_kj,         // RFC3849 "example address" block -- like example.com for IPs
  };

  // TODO(cleanup): A bug in GCC 4.8, fixed in 4.9, prevents result from implicitly
  //   casting to our return type.
  return kj::arrayPtr(result, kj::size(result));
}

NetworkFilter::NetworkFilter()
    : allowUnix(true), allowAbstractUnix(true) {
  allowCidrs.add(CidrRange::inet4({0,0,0,0}, 0));
  allowCidrs.add(CidrRange::inet6({}, {}, 0));
  denyCidrs.addAll(reservedCidrs());
}

NetworkFilter::NetworkFilter(ArrayPtr<const StringPtr> allow, ArrayPtr<const StringPtr> deny,
                             NetworkFilter& next)
    : allowUnix(false), allowAbstractUnix(false), next(next) {
  for (auto rule: allow) {
    if (rule == "local") {
      allowCidrs.addAll(localCidrs());
    } else if (rule == "network") {
      allowCidrs.add(CidrRange::inet4({0,0,0,0}, 0));
      allowCidrs.add(CidrRange::inet6({}, {}, 0));
      denyCidrs.addAll(localCidrs());
    } else if (rule == "private") {
      allowCidrs.addAll(privateCidrs());
      allowCidrs.addAll(localCidrs());
    } else if (rule == "public") {
      allowCidrs.add(CidrRange::inet4({0,0,0,0}, 0));
      allowCidrs.add(CidrRange::inet6({}, {}, 0));
      denyCidrs.addAll(privateCidrs());
      denyCidrs.addAll(localCidrs());
    } else if (rule == "unix") {
      allowUnix = true;
    } else if (rule == "unix-abstract") {
      allowAbstractUnix = true;
    } else {
      allowCidrs.add(CidrRange(rule));
    }
  }

  for (auto rule: deny) {
    if (rule == "local") {
      denyCidrs.addAll(localCidrs());
    } else if (rule == "network") {
      KJ_FAIL_REQUIRE("don't deny 'network', allow 'local' instead");
    } else if (rule == "private") {
      denyCidrs.addAll(privateCidrs());
    } else if (rule == "public") {
      // Tricky: What if we allow 'network' and deny 'public'?
      KJ_FAIL_REQUIRE("don't deny 'public', allow 'private' instead");
    } else if (rule == "unix") {
      allowUnix = false;
    } else if (rule == "unix-abstract") {
      allowAbstractUnix = false;
    } else {
      denyCidrs.add(CidrRange(rule));
    }
  }
}

bool NetworkFilter::shouldAllow(const struct sockaddr* addr, uint addrlen) {
  KJ_REQUIRE(addrlen >= sizeof(addr->sa_family));

#if !_WIN32
  if (addr->sa_family == AF_UNIX) {
    auto path = safeUnixPath(reinterpret_cast<const struct sockaddr_un*>(addr), addrlen);
    if (path.size() > 0 && path[0] == '\0') {
      return allowAbstractUnix;
    } else {
      return allowUnix;
    }
  }
#endif

  bool allowed = false;
  uint allowSpecificity = 0;
  for (auto& cidr: allowCidrs) {
    if (cidr.matches(addr)) {
      allowSpecificity = kj::max(allowSpecificity, cidr.getSpecificity());
      allowed = true;
    }
  }
  if (!allowed) return false;
  for (auto& cidr: denyCidrs) {
    if (cidr.matches(addr)) {
      if (cidr.getSpecificity() >= allowSpecificity) return false;
    }
  }

  KJ_IF_MAYBE(n, next) {
    return n->shouldAllow(addr, addrlen);
  } else {
    return true;
  }
}

bool NetworkFilter::shouldAllowParse(const struct sockaddr* addr, uint addrlen) {
  bool matched = false;
#if !_WIN32
  if (addr->sa_family == AF_UNIX) {
    auto path = safeUnixPath(reinterpret_cast<const struct sockaddr_un*>(addr), addrlen);
    if (path.size() > 0 && path[0] == '\0') {
      if (allowAbstractUnix) matched = true;
    } else {
      if (allowUnix) matched = true;
    }
  } else {
#endif
    for (auto& cidr: allowCidrs) {
      if (cidr.matchesFamily(addr->sa_family)) {
        matched = true;
      }
    }
#if !_WIN32
  }
#endif

  if (matched) {
    KJ_IF_MAYBE(n, next) {
      return n->shouldAllowParse(addr, addrlen);
    } else {
      return true;
    }
  } else {
    // No allow rule matches this address family, so don't even allow parsing it.
    return false;
  }
}

}  // namespace _ (private)
}  // namespace kj
