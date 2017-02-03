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

#include "async-io.h"
#include "debug.h"
#include "vector.h"

namespace kj {

Promise<void> AsyncInputStream::read(void* buffer, size_t bytes) {
  return read(buffer, bytes, bytes).then([](size_t) {});
}

Promise<size_t> AsyncInputStream::read(void* buffer, size_t minBytes, size_t maxBytes) {
  return tryRead(buffer, minBytes, maxBytes).then([=](size_t result) {
    KJ_REQUIRE(result >= minBytes, "Premature EOF") {
      // Pretend we read zeros from the input.
      memset(reinterpret_cast<byte*>(buffer) + result, 0, minBytes - result);
      return minBytes;
    }
    return result;
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

    return input.tryRead(buffer, 1, sizeof(buffer))
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

  Promise<Array<byte>> readAllBytes() {
    return loop().then([this](uint64_t size) {
      auto out = heapArray<byte>(size);
      copyInto(out);
      return out;
    });
  }

  Promise<String> readAllText() {
    return loop().then([this](uint64_t size) {
      auto out = heapArray<char>(size + 1);
      copyInto(out.slice(0, out.size() - 1).asBytes());
      out.back() = '\0';
      return String(kj::mv(out));
    });
  }

private:
  AsyncInputStream& input;
  Vector<Array<byte>> parts;

  Promise<uint64_t> loop(uint64_t total = 0) {
    auto part = heapArray<byte>(4096);
    auto partPtr = part.asPtr();
    parts.add(kj::mv(part));
    return input.tryRead(partPtr.begin(), partPtr.size(), partPtr.size())
        .then([this,KJ_CPCAP(partPtr),total](size_t amount) -> Promise<uint64_t> {
      uint64_t newTotal = total + amount;
      if (amount < partPtr.size()) {
        return newTotal;
      } else {
        return loop(newTotal);
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

Promise<Array<byte>> AsyncInputStream::readAllBytes() {
  auto reader = kj::heap<AllReader>(*this);
  auto promise = reader->readAllBytes();
  return promise.attach(kj::mv(reader));
}

Promise<String> AsyncInputStream::readAllText() {
  auto reader = kj::heap<AllReader>(*this);
  auto promise = reader->readAllText();
  return promise.attach(kj::mv(reader));
}

Maybe<Promise<uint64_t>> AsyncOutputStream::tryPumpFrom(
    AsyncInputStream& input, uint64_t amount) {
  return nullptr;
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
Own<DatagramPort> LowLevelAsyncIoProvider::wrapDatagramSocketFd(Fd fd, uint flags) {
  KJ_UNIMPLEMENTED("Datagram sockets not implemented.");
}

}  // namespace kj
