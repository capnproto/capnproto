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
#include <deque>

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

      readAborted = true;
      KJ_IF_MAYBE(f, readAbortFulfiller) {
        f->get()->fulfill();
        readAbortFulfiller = nullptr;
      }
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

  Promise<void> whenWriteDisconnected() override {
    if (readAborted) {
      return kj::READY_NOW;
    } else KJ_IF_MAYBE(p, readAbortPromise) {
      return p->addBranch();
    } else {
      auto paf = newPromiseAndFulfiller<void>();
      readAbortFulfiller = kj::mv(paf.fulfiller);
      auto fork = paf.promise.fork();
      auto result = fork.addBranch();
      readAbortPromise = kj::mv(fork);
      return result;
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

  bool readAborted = false;
  Maybe<Own<PromiseFulfiller<void>>> readAbortFulfiller = nullptr;
  Maybe<ForkedPromise<void>> readAbortPromise = nullptr;

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

    Promise<void> whenWriteDisconnected() override {
      KJ_FAIL_ASSERT("can't get here -- implemented by AsyncPipe");
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
        if (pumpedSoFar == amount || actual < n) {
          // Either we pumped all we wanted or we hit EOF.
          fulfiller.fulfill(kj::cp(pumpedSoFar));
          pipe.endState(*this);
          return pipe.pumpTo(output, amount2 - actual)
              .then([actual](uint64_t actual2) { return actual + actual2; });
        }

        // Completed entire pumpTo amount.
        KJ_ASSERT(actual == amount2);
        return amount2;
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

    Promise<void> whenWriteDisconnected() override {
      KJ_FAIL_ASSERT("can't get here -- implemented by AsyncPipe");
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
            if (pieces.size() == 0) {
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
          .then([this,&input,amount](size_t actual) -> Promise<uint64_t> {
        readBuffer = readBuffer.slice(actual, readBuffer.size());
        readSoFar += actual;

        if (readSoFar >= minBytes) {
          // We've read enough to close out this read (readSoFar >= minBytes).
          canceler.release();
          fulfiller.fulfill(kj::cp(readSoFar));
          pipe.endState(*this);

          if (actual < amount) {
            // We didn't read as much data as the pump requested, but we did fulfill the read, so
            // we don't know whether we reached EOF on the input. We need to continue the pump,
            // replacing the BlockedRead state.
            return input.pumpTo(pipe, amount - actual)
                .then([actual](uint64_t actual2) -> uint64_t { return actual + actual2; });
          } else {
            // We pumped as much data as was requested, so we can return that now.
            return actual;
          }
        } else {
          // The pump completed without fulfilling the read. This either means that the pump
          // reached EOF or the `amount` requested was not enough to satisfy the read in the first
          // place. Pumps do not propagate EOF, so either way we want to leave the BlockedRead in
          // place waiting for more data.
          return actual;
        }
      }));
    }

    void shutdownWrite() override {
      canceler.cancel("shutdownWrite() was called");
      fulfiller.fulfill(kj::cp(readSoFar));
      pipe.endState(*this);
      pipe.shutdownWrite();
    }

    Promise<void> whenWriteDisconnected() override {
      KJ_FAIL_ASSERT("can't get here -- implemented by AsyncPipe");
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

    Promise<void> whenWriteDisconnected() override {
      KJ_FAIL_ASSERT("can't get here -- implemented by AsyncPipe");
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
      return KJ_EXCEPTION(DISCONNECTED, "abortRead() has been called");
    }
    Promise<uint64_t> pumpTo(AsyncOutputStream& output, uint64_t amount) override {
      return KJ_EXCEPTION(DISCONNECTED, "abortRead() has been called");
    }
    void abortRead() override {
      // ignore repeated abort
    }

    Promise<void> write(const void* buffer, size_t size) override {
      return KJ_EXCEPTION(DISCONNECTED, "abortRead() has been called");
    }
    Promise<void> write(ArrayPtr<const ArrayPtr<const byte>> pieces) override {
      return KJ_EXCEPTION(DISCONNECTED, "abortRead() has been called");
    }
    Maybe<Promise<uint64_t>> tryPumpFrom(AsyncInputStream& input, uint64_t amount) override {
      // There might not actually be any data in `input`, in which case a pump wouldn't actually
      // write anything and wouldn't fail.

      if (input.tryGetLength().orDefault(1) == 0) {
        // Yeah a pump would pump nothing.
        return Promise<uint64_t>(uint64_t(0));
      } else {
        // While we *could* just return nullptr here, it would probably then fall back to a normal
        // buffered pump, which would allocate a big old buffer just to find there's nothing to
        // read. Let's try reading 1 byte to avoid that allocation.
        static char c;
        return input.tryRead(&c, 1, 1).then([](size_t n) {
          if (n == 0) {
            // Yay, we're at EOF as hoped.
            return uint64_t(0);
          } else {
            // There was data in the input. The pump would have thrown.
            kj::throwRecoverableException(
                KJ_EXCEPTION(DISCONNECTED, "abortRead() has been called"));
            return uint64_t(0);
          }
        });
      }
    }
    void shutdownWrite() override {
      // ignore -- currently shutdownWrite() actually means that the PipeWriteEnd was dropped,
      // which is not an error even if reads have been aborted.
    }
    Promise<void> whenWriteDisconnected() override {
      KJ_FAIL_ASSERT("can't get here -- implemented by AsyncPipe");
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
    Promise<void> whenWriteDisconnected() override {
      KJ_FAIL_ASSERT("can't get here -- implemented by AsyncPipe");
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

  Promise<void> whenWriteDisconnected() override {
    return pipe->whenWriteDisconnected();
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
  Promise<void> whenWriteDisconnected() override {
    return out->whenWriteDisconnected();
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
      this->inner = nullptr;
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

namespace {

class AsyncTee final: public Refcounted {
public:
  using BranchId = uint;

  explicit AsyncTee(Own<AsyncInputStream> inner, uint64_t bufferSizeLimit)
      : inner(mv(inner)), bufferSizeLimit(bufferSizeLimit), length(this->inner->tryGetLength()) {}
  ~AsyncTee() noexcept(false) {
    bool hasBranches = false;
    for (auto& branch: branches) {
      hasBranches = hasBranches || branch != nullptr;
    }
    KJ_ASSERT(!hasBranches, "destroying AsyncTee with branch still alive") {
      // Don't std::terminate().
      break;
    }
  }

  void addBranch(BranchId branch) {
    KJ_REQUIRE(branches[branch] == nullptr, "branch already exists");
    branches[branch] = Branch();
  }

  void removeBranch(BranchId branch) {
    auto& state = KJ_REQUIRE_NONNULL(branches[branch], "branch was already destroyed");
    KJ_REQUIRE(state.sink == nullptr,
        "destroying tee branch with operation still in-progress; probably going to segfault") {
      // Don't std::terminate().
      break;
    }

    branches[branch] = nullptr;
  }

  Promise<size_t> tryRead(BranchId branch, void* buffer, size_t minBytes, size_t maxBytes)  {
    auto& state = KJ_ASSERT_NONNULL(branches[branch]);
    KJ_ASSERT(state.sink == nullptr);

    // If there is excess data in the buffer for us, slurp that up.
    auto readBuffer = arrayPtr(reinterpret_cast<byte*>(buffer), maxBytes);
    auto readSoFar = state.buffer.consume(readBuffer, minBytes);

    if (minBytes == 0) {
      return readSoFar;
    }

    if (state.buffer.empty()) {
      KJ_IF_MAYBE(reason, stoppage) {
        // Prefer a short read to an exception. The exception prevents the pull loop from adding any
        // data to the buffer, so `readSoFar` will be zero the next time someone calls `tryRead()`,
        // and the caller will see the exception.
        if (reason->is<Eof>() || readSoFar > 0) {
          return readSoFar;
        }
        return cp(reason->get<Exception>());
      }
    }

    auto promise = newAdaptedPromise<size_t, ReadSink>(state.sink, readBuffer, minBytes, readSoFar);
    ensurePulling();
    return mv(promise);
  }

  Maybe<uint64_t> tryGetLength(BranchId branch)  {
    auto& state = KJ_ASSERT_NONNULL(branches[branch]);

    return length.map([&state](uint64_t amount) {
      return amount + state.buffer.size();
    });
  }

  Promise<uint64_t> pumpTo(BranchId branch, AsyncOutputStream& output, uint64_t amount)  {
    auto& state = KJ_ASSERT_NONNULL(branches[branch]);
    KJ_ASSERT(state.sink == nullptr);

    if (amount == 0) {
      return amount;
    }

    if (state.buffer.empty()) {
      KJ_IF_MAYBE(reason, stoppage) {
        if (reason->is<Eof>()) {
          return uint64_t(0);
        }
        return cp(reason->get<Exception>());
      }
    }

    auto promise = newAdaptedPromise<uint64_t, PumpSink>(state.sink, output, amount);
    ensurePulling();
    return mv(promise);
  }

private:
  struct Eof {};
  using Stoppage = OneOf<Eof, Exception>;

  class Buffer {
  public:
    uint64_t consume(ArrayPtr<byte>& readBuffer, size_t& minBytes);
    // Consume as many bytes as possible, copying them into `readBuffer`. Return the number of bytes
    // consumed.
    //
    // `readBuffer` and `minBytes` are both assigned appropriate new values, such that after any
    // call to `consume()`, `readBuffer` will point to the remaining slice of unwritten space, and
    // `minBytes` will have been decremented (clamped to zero) by the amount of bytes read. That is,
    // the read can be considered fulfilled if `minBytes` is zero after a call to `consume()`.

    Array<const ArrayPtr<const byte>> asArray(uint64_t minBytes, uint64_t& amount);
    // Consume the first `minBytes` of the buffer (or the entire buffer) and return it in an Array
    // of ArrayPtr<const byte>s, suitable for passing to AsyncOutputStream.write(). The outer Array
    // owns the underlying data.

    void produce(Array<byte> bytes);
    // Enqueue a byte array to the end of the buffer list.

    bool empty() const;
    uint64_t size() const;

  private:
    std::deque<Array<byte>> bufferList;
  };

  class Sink {
  public:
    struct Need {
      // We use uint64_t here because:
      // - pumpTo() accepts it as the `amount` parameter.
      // - all practical values of tryRead()'s `maxBytes` parameter (a size_t) should also fit into
      //   a uint64_t, unless we're on a machine with multiple exabytes of memory ...

      uint64_t minBytes = 0;

      uint64_t maxBytes = kj::maxValue;
    };

    virtual Promise<void> fill(Buffer& inBuffer, const Maybe<Stoppage>& stoppage) = 0;
    // Attempt to fill the sink with bytes andreturn a promise which must resolve before any inner
    // read may be attempted. If a sink requires backpressure to be respected, this is how it should
    // be communicated.
    //
    // If the sink is full, it must detach from the tee before the returned promise is resolved.
    //
    // The returned promise must not result in an exception.

    virtual Need need() = 0;

    virtual void reject(Exception&& exception) = 0;
    // Inform this sink of a catastrophic exception and detach it. Regular read exceptions should be
    // propagated through `fill()`'s stoppage parameter instead.
  };

  template <typename T>
  class SinkBase: public Sink {
    // Registers itself with the tee as a sink on construction, detaches from the tee on
    // fulfillment, rejection, or destruction.
    //
    // A bit of a Frankenstein, avert your eyes. For one thing, it's more of a mixin than a base...

  public:
    explicit SinkBase(PromiseFulfiller<T>& fulfiller, Maybe<Sink&>& sinkLink)
        : fulfiller(fulfiller), sinkLink(sinkLink) {
      KJ_ASSERT(sinkLink == nullptr, "sink initiated with sink already in flight");
      sinkLink = *this;
    }
    KJ_DISALLOW_COPY(SinkBase);
    ~SinkBase() noexcept(false) { detach(); }

    void reject(Exception&& exception) override {
      // The tee is allowed to reject this sink if it needs to, e.g. to propagate a non-inner read
      // exception from the pull loop. Only the derived class is allowed to fulfill() directly,
      // though -- the tee must keep calling fill().

      fulfiller.reject(mv(exception));
      detach();
    }

  protected:
    template <typename U>
    void fulfill(U value) {
      fulfiller.fulfill(fwd<U>(value));
      detach();
    }

  private:
    void detach() {
      KJ_IF_MAYBE(sink, sinkLink) {
        if (sink == this) {
          sinkLink = nullptr;
        }
      }
    }

    PromiseFulfiller<T>& fulfiller;
    Maybe<Sink&>& sinkLink;
  };

  struct Branch {
    Buffer buffer;
    Maybe<Sink&> sink;
  };

  class ReadSink final: public SinkBase<size_t> {
  public:
    explicit ReadSink(PromiseFulfiller<size_t>& fulfiller, Maybe<Sink&>& registration,
                      ArrayPtr<byte> buffer, size_t minBytes, size_t readSoFar)
        : SinkBase(fulfiller, registration), buffer(buffer),
          minBytes(minBytes), readSoFar(readSoFar) {}

    Promise<void> fill(Buffer& inBuffer, const Maybe<Stoppage>& stoppage) override {
      auto amount = inBuffer.consume(buffer, minBytes);
      readSoFar += amount;

      if (minBytes == 0) {
        // We satisfied the read request.
        fulfill(readSoFar);
        return READY_NOW;
      }

      if (amount == 0 && inBuffer.empty()) {
        // We made no progress on the read request and the buffer is tapped out.
        KJ_IF_MAYBE(reason, stoppage) {
          if (reason->is<Eof>() || readSoFar > 0) {
            // Prefer short read to exception.
            fulfill(readSoFar);
          } else {
            reject(cp(reason->get<Exception>()));
          }
          return READY_NOW;
        }
      }

      return READY_NOW;
    }

    Need need() override { return Need { minBytes, buffer.size() }; }

  private:
    ArrayPtr<byte> buffer;
    size_t minBytes;
    // Arguments to the outer tryRead() call, sliced/decremented after every buffer consumption.

    size_t readSoFar;
    // End result of the outer tryRead().
  };

  class PumpSink final: public SinkBase<uint64_t> {
  public:
    explicit PumpSink(PromiseFulfiller<uint64_t>& fulfiller, Maybe<Sink&>& registration,
                      AsyncOutputStream& output, uint64_t limit)
        : SinkBase(fulfiller, registration), output(output), limit(limit) {}

    ~PumpSink() noexcept(false) {
      canceler.cancel("This pump has been canceled.");
    }

    Promise<void> fill(Buffer& inBuffer, const Maybe<Stoppage>& stoppage) override {
      KJ_ASSERT(limit > 0);

      uint64_t amount = 0;

      // TODO(someday): This consumes data from the buffer, but we cannot know if the stream to
      //   which we're pumping will accept it until after the write() promise completes. If the
      //   write() promise rejects, we lose this data. We should consume the data from the buffer
      //   only after successful writes.
      auto writeBuffer = inBuffer.asArray(limit, amount);
      KJ_ASSERT(limit >= amount);
      if (amount > 0) {
        Promise<void> promise = nullptr;

        // TODO(soon): Replace try/catch with kj::evalNow() to work with -fno-exceptions.
        try {
          promise = output.write(writeBuffer).attach(mv(writeBuffer));
        } catch (const Exception& exception) {
          reject(cp(exception));
          return READY_NOW;
        }

        promise = promise.then([this, amount]() {
          limit -= amount;
          pumpedSoFar += amount;
          if (limit == 0) {
            fulfill(pumpedSoFar);
          }
        }).eagerlyEvaluate([this](Exception&& exception) {
          reject(mv(exception));
        });

        return canceler.wrap(mv(promise)).catch_([](kj::Exception&&) {});
      } else KJ_IF_MAYBE(reason, stoppage) {
        if (reason->is<Eof>()) {
          // Unlike in the read case, it makes more sense to immediately propagate exceptions to the
          // pump promise rather than show it a "short pump".
          fulfill(pumpedSoFar);
        } else {
          reject(cp(reason->get<Exception>()));
        }
      }

      return READY_NOW;
    }

    Need need() override { return Need { 1, limit }; }

  private:
    AsyncOutputStream& output;
    uint64_t limit;
    // Arguments to the outer pumpTo() call, decremented after every buffer consumption.
    //
    // Equal to zero once fulfiller has been fulfilled/rejected.

    uint64_t pumpedSoFar = 0;
    // End result of the outer pumpTo().

    Canceler canceler;
    // When the pump is canceled, we also need to cancel any write operations in flight.
  };

  // =====================================================================================

  Maybe<Sink::Need> analyzeSinks() {
    // Return nullptr if there are no sinks at all. Otherwise, return the largest `minBytes` and the
    // smallest `maxBytes` requested by any sink. The pull loop will use these values to calculate
    // the optimal buffer size for the next inner read, so that a minimum amount of data is buffered
    // at any given time.

    uint64_t minBytes = 0;
    uint64_t maxBytes = kj::maxValue;

    uint nBranches = 0;
    uint nSinks = 0;

    for (auto& state: branches) {
      KJ_IF_MAYBE(s, state) {
        ++nBranches;
        KJ_IF_MAYBE(sink, s->sink) {
          ++nSinks;
          auto need = sink->need();
          minBytes = kj::max(minBytes, need.minBytes);
          maxBytes = kj::min(maxBytes, need.maxBytes);
        }
      }
    }

    if (nSinks > 0) {
      KJ_ASSERT(minBytes > 0);
      KJ_ASSERT(maxBytes > 0, "sink was filled but did not detach");

      // Sinks may report non-overlapping needs.
      maxBytes = kj::max(minBytes, maxBytes);

      return Sink::Need { minBytes, maxBytes };
    }

    // No active sinks.
    return nullptr;
  }

  void ensurePulling() {
    if (!pulling) {
      pulling = true;
      UnwindDetector unwind;
      KJ_DEFER(if (unwind.isUnwinding()) pulling = false);
      pullPromise = pull();
    }
  }

  Promise<void> pull() {
    // Use evalLater() so that two pump sinks added on the same turn of the event loop will not
    // cause buffering.
    return evalLater([this] {
      // Attempt to fill any sinks that exist.

      Vector<Promise<void>> promises;

      for (auto& state: branches) {
        KJ_IF_MAYBE(s, state) {
          KJ_IF_MAYBE(sink, s->sink) {
            promises.add(sink->fill(s->buffer, stoppage));
          }
        }
      }

      // Respect the greatest of the sinks' backpressures.
      return joinPromises(promises.releaseAsArray());
    }).then([this]() -> Promise<void> {
      // Check to see whether we need to perform an inner read.

      auto need = analyzeSinks();

      if (need == nullptr) {
        // No more sinks, stop pulling.
        pulling = false;
        return READY_NOW;
      }

      if (stoppage != nullptr) {
        // We're eof or errored, don't read, but loop so we can fill the sink(s).
        return pull();
      }

      auto& n = KJ_ASSERT_NONNULL(need);

      KJ_ASSERT(n.minBytes > 0);

      // We must perform an inner read.

      // We'd prefer not to explode our buffer, if that's cool. We cap `maxBytes` to the buffer size
      // limit or our builtin MAX_BLOCK_SIZE, whichever is smaller. But, we make sure `maxBytes` is
      // still >= `minBytes`.
      n.maxBytes = kj::min(n.maxBytes, MAX_BLOCK_SIZE);
      n.maxBytes = kj::min(n.maxBytes, bufferSizeLimit);
      n.maxBytes = kj::max(n.minBytes, n.maxBytes);
      for (auto& state: branches) {
        KJ_IF_MAYBE(s, state) {
          // TODO(perf): buffer.size() is O(n) where n = # of individual heap-allocated byte arrays.
          if (s->buffer.size() + n.maxBytes > bufferSizeLimit) {
            stoppage = Stoppage(KJ_EXCEPTION(FAILED, "tee buffer size limit exceeded"));
            return pull();
          }
        }
      }
      auto heapBuffer = heapArray<byte>(n.maxBytes);

      // gcc 4.9 quirk: If I don't hoist this into a separate variable and instead call
      //
      //   inner->tryRead(heapBuffer.begin(), n.minBytes, heapBuffer.size())
      //
      // `heapBuffer` seems to get moved into the lambda capture before the arguments to `tryRead()`
      // are evaluated, meaning `inner` sees a nullptr destination. Bizarrely, `inner` sees the
      // correct value for `heapBuffer.size()`... I dunno, man.
      auto destination = heapBuffer.begin();

      try {
        return inner->tryRead(destination, n.minBytes, n.maxBytes)
            .then([this, heapBuffer = mv(heapBuffer), minBytes = n.minBytes](size_t amount) mutable
                -> Promise<void> {
          length = length.map([amount](uint64_t n) {
            KJ_ASSERT(n >= amount);
            return n - amount;
          });

          if (amount < heapBuffer.size()) {
            heapBuffer = heapBuffer.slice(0, amount).attach(mv(heapBuffer));
          }

          KJ_ASSERT(stoppage == nullptr);
          Maybe<ArrayPtr<byte>> bufferPtr = nullptr;
          for (auto& state: branches) {
            KJ_IF_MAYBE(s, state) {
              // Prefer to move the buffer into the receiving branch's deque, rather than memcpy.
              //
              // TODO(perf): For the 2-branch case, this is fine, since the majority of the time
              //   only one buffer will be in use. If we generalize to the n-branch case, this would
              //   become memcpy-heavy.
              KJ_IF_MAYBE(ptr, bufferPtr) {
                s->buffer.produce(heapArray(*ptr));
              } else {
                bufferPtr = ArrayPtr<byte>(heapBuffer);
                s->buffer.produce(mv(heapBuffer));
              }
            }
          }

          if (amount < minBytes) {
            // Short read, EOF.
            stoppage = Stoppage(Eof());
          }

          return pull();
        }, [this](Exception&& exception) {
          // Exception from the inner tryRead(). Propagate.
          stoppage = Stoppage(mv(exception));
          return pull();
        });
      } catch (const Exception& exception) {
        // Exception from the inner tryRead(). Propagate.
        stoppage = Stoppage(cp(exception));
        return pull();
      }
    }).eagerlyEvaluate([this](Exception&& exception) {
      // Exception from our loop, not from inner tryRead(). Something is broken; tell everybody!
      pulling = false;
      for (auto& state: branches) {
        KJ_IF_MAYBE(s, state) {
          KJ_IF_MAYBE(sink, s->sink) {
            sink->reject(KJ_EXCEPTION(FAILED, "Exception in tee loop", exception));
          }
        }
      }
    });
  }

  constexpr static size_t MAX_BLOCK_SIZE = 1 << 14;  // 16k

  Own<AsyncInputStream> inner;
  const uint64_t bufferSizeLimit = kj::maxValue;
  Maybe<uint64_t> length;
  Maybe<Branch> branches[2];
  Maybe<Stoppage> stoppage;
  Promise<void> pullPromise = READY_NOW;
  bool pulling = false;
};

constexpr size_t AsyncTee::MAX_BLOCK_SIZE;

uint64_t AsyncTee::Buffer::consume(ArrayPtr<byte>& readBuffer, size_t& minBytes) {
  uint64_t totalAmount = 0;

  while (readBuffer.size() > 0 && !bufferList.empty()) {
    auto& bytes = bufferList.front();
    auto amount = kj::min(bytes.size(), readBuffer.size());
    memcpy(readBuffer.begin(), bytes.begin(), amount);
    totalAmount += amount;

    readBuffer = readBuffer.slice(amount, readBuffer.size());
    minBytes -= kj::min(amount, minBytes);

    if (amount == bytes.size()) {
      bufferList.pop_front();
    } else {
      bytes = heapArray(bytes.slice(amount, bytes.size()));
      return totalAmount;
    }
  }

  return totalAmount;
}

void AsyncTee::Buffer::produce(Array<byte> bytes) {
  bufferList.push_back(mv(bytes));
}

Array<const ArrayPtr<const byte>> AsyncTee::Buffer::asArray(
    uint64_t maxBytes, uint64_t& amount) {
  amount = 0;

  Vector<ArrayPtr<const byte>> buffers;
  Vector<Array<byte>> ownBuffers;

  while (maxBytes > 0 && !bufferList.empty()) {
    auto& bytes = bufferList.front();

    if (bytes.size() <= maxBytes) {
      amount += bytes.size();
      maxBytes -= bytes.size();

      buffers.add(bytes);
      ownBuffers.add(mv(bytes));

      bufferList.pop_front();
    } else {
      auto ownBytes = heapArray(bytes.slice(0, maxBytes));
      buffers.add(ownBytes);
      ownBuffers.add(mv(ownBytes));

      bytes = heapArray(bytes.slice(maxBytes, bytes.size()));

      amount += maxBytes;
      maxBytes = 0;
    }
  }


  if (buffers.size() > 0) {
    return buffers.releaseAsArray().attach(mv(ownBuffers));
  }

  return {};
}

bool AsyncTee::Buffer::empty() const {
  return bufferList.empty();
}

uint64_t AsyncTee::Buffer::size() const {
  uint64_t result = 0;

  for (auto& bytes: bufferList) {
    result += bytes.size();
  }

  return result;
}

class TeeBranch final: public AsyncInputStream {
public:
  TeeBranch(Own<AsyncTee> tee, uint8_t branch): tee(mv(tee)), branch(branch) {
    this->tee->addBranch(branch);
  }
  ~TeeBranch() noexcept(false) {
    unwind.catchExceptionsIfUnwinding([&]() {
      tee->removeBranch(branch);
    });
  }

  Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
    return tee->tryRead(branch, buffer, minBytes, maxBytes);
  }

  Promise<uint64_t> pumpTo(AsyncOutputStream& output, uint64_t amount) override {
    return tee->pumpTo(branch, output, amount);
  }

  Maybe<uint64_t> tryGetLength() override {
    return tee->tryGetLength(branch);
  }

private:
  Own<AsyncTee> tee;
  const uint8_t branch;
  UnwindDetector unwind;
};

}  // namespace

Tee newTee(Own<AsyncInputStream> input, uint64_t limit) {
  auto impl = refcounted<AsyncTee>(mv(input), limit);
  Own<AsyncInputStream> branch1 = heap<TeeBranch>(addRef(*impl), 0);
  Own<AsyncInputStream> branch2 = heap<TeeBranch>(mv(impl), 1);
  return { { mv(branch1), mv(branch2) } };
}

namespace {

class PromisedAsyncIoStream final: public kj::AsyncIoStream, private kj::TaskSet::ErrorHandler {
  // An AsyncIoStream which waits for a promise to resolve then forwards all calls to the promised
  // stream.

public:
  PromisedAsyncIoStream(kj::Promise<kj::Own<AsyncIoStream>> promise)
      : promise(promise.then([this](kj::Own<AsyncIoStream> result) {
          stream = kj::mv(result);
        }).fork()),
        tasks(*this) {}

  kj::Promise<size_t> read(void* buffer, size_t minBytes, size_t maxBytes) override {
    KJ_IF_MAYBE(s, stream) {
      return s->get()->read(buffer, minBytes, maxBytes);
    } else {
      return promise.addBranch().then([this,buffer,minBytes,maxBytes]() {
        return KJ_ASSERT_NONNULL(stream)->read(buffer, minBytes, maxBytes);
      });
    }
  }
  kj::Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
    KJ_IF_MAYBE(s, stream) {
      return s->get()->tryRead(buffer, minBytes, maxBytes);
    } else {
      return promise.addBranch().then([this,buffer,minBytes,maxBytes]() {
        return KJ_ASSERT_NONNULL(stream)->tryRead(buffer, minBytes, maxBytes);
      });
    }
  }

  kj::Maybe<uint64_t> tryGetLength() override {
    KJ_IF_MAYBE(s, stream) {
      return s->get()->tryGetLength();
    } else {
      return nullptr;
    }
  }

  kj::Promise<uint64_t> pumpTo(kj::AsyncOutputStream& output, uint64_t amount) override {
    KJ_IF_MAYBE(s, stream) {
      return s->get()->pumpTo(output, amount);
    } else {
      return promise.addBranch().then([this,&output,amount]() {
        return KJ_ASSERT_NONNULL(stream)->pumpTo(output, amount);
      });
    }
  }

  kj::Promise<void> write(const void* buffer, size_t size) override {
    KJ_IF_MAYBE(s, stream) {
      return s->get()->write(buffer, size);
    } else {
      return promise.addBranch().then([this,buffer,size]() {
        return KJ_ASSERT_NONNULL(stream)->write(buffer, size);
      });
    }
  }
  kj::Promise<void> write(kj::ArrayPtr<const kj::ArrayPtr<const byte>> pieces) override {
    KJ_IF_MAYBE(s, stream) {
      return s->get()->write(pieces);
    } else {
      return promise.addBranch().then([this,pieces]() {
        return KJ_ASSERT_NONNULL(stream)->write(pieces);
      });
    }
  }

  kj::Maybe<kj::Promise<uint64_t>> tryPumpFrom(
      kj::AsyncInputStream& input, uint64_t amount = kj::maxValue) override {
    KJ_IF_MAYBE(s, stream) {
      // Call input.pumpTo() on the resolved stream instead, so that if it does some dynamic_casts
      // or whatnot to detect stream types it can retry those on the inner stream.
      return input.pumpTo(**s, amount);
    } else {
      return promise.addBranch().then([this,&input,amount]() {
        // Here we actually have no choice but to call input.pumpTo() because if we called
        // tryPumpFrom(input, amount) and it returned nullptr, what would we do? It's too late for
        // us to return nullptr. But the thing about dynamic_cast also applies.
        return input.pumpTo(*KJ_ASSERT_NONNULL(stream), amount);
      });
    }
  }

  Promise<void> whenWriteDisconnected() override {
    KJ_IF_MAYBE(s, stream) {
      return s->get()->whenWriteDisconnected();
    } else {
      return promise.addBranch().then([this]() {
        return KJ_ASSERT_NONNULL(stream)->whenWriteDisconnected();
      }, [](kj::Exception&& e) -> kj::Promise<void> {
        if (e.getType() == kj::Exception::Type::DISCONNECTED) {
          return kj::READY_NOW;
        } else {
          return kj::mv(e);
        }
      });
    }
  }

  void shutdownWrite() override {
    KJ_IF_MAYBE(s, stream) {
      return s->get()->shutdownWrite();
    } else {
      tasks.add(promise.addBranch().then([this]() {
        return KJ_ASSERT_NONNULL(stream)->shutdownWrite();
      }));
    }
  }

  void abortRead() override {
    KJ_IF_MAYBE(s, stream) {
      return s->get()->abortRead();
    } else {
      tasks.add(promise.addBranch().then([this]() {
        return KJ_ASSERT_NONNULL(stream)->abortRead();
      }));
    }
  }

private:
  kj::ForkedPromise<void> promise;
  kj::Maybe<kj::Own<AsyncIoStream>> stream;
  kj::TaskSet tasks;

  void taskFailed(kj::Exception&& exception) override {
    KJ_LOG(ERROR, exception);
  }
};

class PromisedAsyncOutputStream final: public kj::AsyncOutputStream {
  // An AsyncOutputStream which waits for a promise to resolve then forwards all calls to the
  // promised stream.
  //
  // TODO(cleanup): Can this share implementation with PromiseIoStream? Seems hard.

public:
  PromisedAsyncOutputStream(kj::Promise<kj::Own<AsyncOutputStream>> promise)
      : promise(promise.then([this](kj::Own<AsyncOutputStream> result) {
          stream = kj::mv(result);
        }).fork()) {}

  kj::Promise<void> write(const void* buffer, size_t size) override {
    KJ_IF_MAYBE(s, stream) {
      return s->get()->write(buffer, size);
    } else {
      return promise.addBranch().then([this,buffer,size]() {
        return KJ_ASSERT_NONNULL(stream)->write(buffer, size);
      });
    }
  }
  kj::Promise<void> write(kj::ArrayPtr<const kj::ArrayPtr<const byte>> pieces) override {
    KJ_IF_MAYBE(s, stream) {
      return s->get()->write(pieces);
    } else {
      return promise.addBranch().then([this,pieces]() {
        return KJ_ASSERT_NONNULL(stream)->write(pieces);
      });
    }
  }

  kj::Maybe<kj::Promise<uint64_t>> tryPumpFrom(
      kj::AsyncInputStream& input, uint64_t amount = kj::maxValue) override {
    KJ_IF_MAYBE(s, stream) {
      return s->get()->tryPumpFrom(input, amount);
    } else {
      return promise.addBranch().then([this,&input,amount]() {
        // Call input.pumpTo() on the resolved stream instead.
        return input.pumpTo(*KJ_ASSERT_NONNULL(stream), amount);
      });
    }
  }

  Promise<void> whenWriteDisconnected() override {
    KJ_IF_MAYBE(s, stream) {
      return s->get()->whenWriteDisconnected();
    } else {
      return promise.addBranch().then([this]() {
        return KJ_ASSERT_NONNULL(stream)->whenWriteDisconnected();
      }, [](kj::Exception&& e) -> kj::Promise<void> {
        if (e.getType() == kj::Exception::Type::DISCONNECTED) {
          return kj::READY_NOW;
        } else {
          return kj::mv(e);
        }
      });
    }
  }

private:
  kj::ForkedPromise<void> promise;
  kj::Maybe<kj::Own<AsyncOutputStream>> stream;
};

}  // namespace

Own<AsyncOutputStream> newPromisedStream(Promise<Own<AsyncOutputStream>> promise) {
  return heap<PromisedAsyncOutputStream>(kj::mv(promise));
}
Own<AsyncIoStream> newPromisedStream(Promise<Own<AsyncIoStream>> promise) {
  return heap<PromisedAsyncIoStream>(kj::mv(promise));
}

Promise<void> AsyncCapabilityStream::writeWithFds(
    ArrayPtr<const byte> data, ArrayPtr<const ArrayPtr<const byte>> moreData,
    ArrayPtr<const AutoCloseFd> fds) {
  // HACK: AutoCloseFd actually contains an `int` under the hood. We can reinterpret_cast to avoid
  //   unnecessary memory allocation.
  static_assert(sizeof(AutoCloseFd) == sizeof(int), "this optimization won't work");
  auto intArray = arrayPtr(reinterpret_cast<const int*>(fds.begin()), fds.size());

  // Be extra-paranoid about aliasing rules by injecting a compiler barrier here. Probably
  // not necessary but also probably doesn't hurt.
#if _MSC_VER
  _ReadWriteBarrier();
#else
  __asm__ __volatile__("": : :"memory");
#endif

  return writeWithFds(data, moreData, intArray);
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

kj::Promise<Maybe<Own<AsyncCapabilityStream>>> AsyncCapabilityStream::tryReceiveStream() {
  struct ResultHolder {
    byte b;
    Own<AsyncCapabilityStream> stream;
  };
  auto result = kj::heap<ResultHolder>();
  auto promise = tryReadWithStreams(&result->b, 1, 1, &result->stream, 1);
  return promise.then([result = kj::mv(result)](ReadResult actual) mutable
                      -> Maybe<Own<AsyncCapabilityStream>> {
    if (actual.byteCount == 0) {
      return nullptr;
    }

    KJ_REQUIRE(actual.capCount == 1,
        "expected to receive a capability (e.g. file descirptor via SCM_RIGHTS), but didn't") {
      return nullptr;
    }

    return kj::mv(result->stream);
  });
}

Promise<void> AsyncCapabilityStream::sendStream(Own<AsyncCapabilityStream> stream) {
  static constexpr byte b = 0;
  auto streams = kj::heapArray<Own<AsyncCapabilityStream>>(1);
  streams[0] = kj::mv(stream);
  return writeWithStreams(arrayPtr(&b, 1), nullptr, kj::mv(streams));
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

kj::Promise<kj::Maybe<AutoCloseFd>> AsyncCapabilityStream::tryReceiveFd() {
  struct ResultHolder {
    byte b;
    AutoCloseFd fd;
  };
  auto result = kj::heap<ResultHolder>();
  auto promise = tryReadWithFds(&result->b, 1, 1, &result->fd, 1);
  return promise.then([result = kj::mv(result)](ReadResult actual) mutable
                      -> Maybe<AutoCloseFd> {
    if (actual.byteCount == 0) {
      return nullptr;
    }

    KJ_REQUIRE(actual.capCount == 1,
        "expected to receive a file descriptor (e.g. via SCM_RIGHTS), but didn't") {
      return nullptr;
    }

    return kj::mv(result->fd);
  });
}

Promise<void> AsyncCapabilityStream::sendFd(int fd) {
  static constexpr byte b = 0;
  auto fds = kj::heapArray<int>(1);
  fds[0] = fd;
  auto promise = writeWithFds(arrayPtr(&b, 1), nullptr, fds);
  return promise.attach(kj::mv(fds));
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
void ConnectionReceiver::getsockname(struct sockaddr* addr, uint* length) {
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
