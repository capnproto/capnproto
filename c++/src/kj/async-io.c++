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
Own<DatagramPort> LowLevelAsyncIoProvider::wrapDatagramSocketFd(int fd, uint flags) {
  KJ_UNIMPLEMENTED("Datagram sockets not implemented.");
}

}  // namespace kj
