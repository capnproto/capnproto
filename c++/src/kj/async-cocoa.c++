// Copyright (c) 2014 Jason Choy <jjwchoy@gmail.com>
// Copyright (c) 2015 Google Inc. (Jason Choy <jjwchoy@google.com>)
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

#ifdef __APPLE__

#include "async.h"
#include "async-cocoa.h"
#include "debug.h"

#include <fcntl.h>
#include <float.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

extern "C" {
  static void kjCocoaFdCallback(CFFileDescriptorRef fdRef, CFOptionFlags callbackTypes, void* info);
  static void kjCocoaScheduleCallback(void* info);
}

namespace kj {
namespace {

void setNonblocking(int fd) {
  int flags;
  KJ_SYSCALL(flags = fcntl(fd, F_GETFL));
  if ((flags & O_NONBLOCK) == 0) {
    KJ_SYSCALL(fcntl(fd, F_SETFL, flags | O_NONBLOCK));
  }
}

void setCloseOnExec(int fd) {
  int flags;
  KJ_SYSCALL(flags = fcntl(fd, F_GETFL));
  if ((flags & O_CLOEXEC) == 0) {
    KJ_SYSCALL(fcntl(fd, F_SETFL, flags | O_CLOEXEC));
  }
}

class CocoaEventPort: public EventPort {
public:
  CocoaEventPort(CFRunLoopRef runLoop, CFStringRef runLoopMode) : runLoop(runLoop), runLoopMode(runLoopMode), kjLoop(*this), wasAwoken(false) {
    CFRetain(runLoop);
    CFRetain(runLoopMode);
    CFRunLoopSourceContext context = { 0, this, NULL, NULL, NULL, NULL, NULL, NULL, NULL, kjCocoaScheduleCallback };
    scheduleSource = CFRunLoopSourceCreate(kCFAllocatorDefault, 0, &context);
    CFRunLoopAddSource(runLoop, scheduleSource, runLoopMode);
  }

  ~CocoaEventPort() noexcept(false) {
    CFRunLoopSourceInvalidate(scheduleSource);

    CFRelease(runLoop);
    CFRelease(runLoopMode);
    CFRelease(scheduleSource);
  }

  EventLoop& getKjLoop() { return kjLoop; }
  CFRunLoopRef getRunLoop() { return runLoop; }
  CFStringRef getRunLoopMode() { return runLoopMode; }

  bool wait() override {
    CFRunLoopRunInMode(runLoopMode, DBL_MAX, true);
    return checkAwoken();
  }

  bool poll() override {
    CFRunLoopRunInMode(runLoopMode, 0, true);
    return checkAwoken();
  }

  void wake() const override {
    CFRunLoopPerformBlock(runLoop, kCFRunLoopCommonModes, ^{
      wasAwoken = true;
    });
    CFRunLoopWakeUp(runLoop);
  }
    
  void setRunnable(bool runnable) override {
    if (runnable != this->runnable) {
      this->runnable = runnable;
      if (runnable && !scheduled) {
        schedule();
      }
    }
  }
private:
  CFRunLoopRef runLoop;
  CFStringRef runLoopMode;
  bool runnable = false;
  bool scheduled = false;
  CFRunLoopSourceRef scheduleSource;
  mutable bool wasAwoken;

  EventLoop kjLoop;

  void schedule() {
    CFRunLoopSourceSignal(scheduleSource);
    scheduled = true;
  }

  void run() {
    KJ_ASSERT(scheduled);
    if (runnable) {
      kjLoop.run();
    }
    scheduled = false;
    if (runnable) {
      schedule();
    }
  }
    
  bool checkAwoken() {
    bool res = wasAwoken;
    wasAwoken = false;
    return res;
  }

  friend void ::kjCocoaScheduleCallback(void*);
};

class CocoaOwnedFileDescriptor {
public:
  CocoaOwnedFileDescriptor(CFRunLoopRef runLoop, CFStringRef runLoopMode, int fd, uint flags) : fd(fd), flags(flags) {
    if (flags & kj::LowLevelAsyncIoProvider::ALREADY_NONBLOCK) {
      KJ_DREQUIRE(fcntl(fd, F_GETFL) & O_NONBLOCK, "You claimed you already set NONBLOCK, but you didn't.");
    } else {
      setNonblocking(fd);
    }

    if (flags & kj::LowLevelAsyncIoProvider::TAKE_OWNERSHIP) {
      if (flags & kj::LowLevelAsyncIoProvider::ALREADY_CLOEXEC) {
        KJ_DREQUIRE(fcntl(fd, F_GETFD) & FD_CLOEXEC,
                    "You claimed you set CLOEXEC, but you didn't.");
      } else {
        setCloseOnExec(fd);
      }
    }

    CFFileDescriptorContext context = { 0, this, NULL, NULL, NULL };
    fdRef = CFFileDescriptorCreate(kCFAllocatorDefault, fd, false, kjCocoaFdCallback, &context);
    CFRunLoopSourceRef fdRunLoopSource = CFFileDescriptorCreateRunLoopSource(kCFAllocatorDefault, fdRef, 0);
    CFRunLoopAddSource(runLoop, fdRunLoopSource, runLoopMode);
    CFRelease(fdRunLoopSource);
  }

  ~CocoaOwnedFileDescriptor() {
    CFFileDescriptorInvalidate(fdRef);
    CFRelease(fdRef);

    // Don't use KJ_SYSCALL() here because close() should not be repeated on EINTR.
    if ((flags & kj::LowLevelAsyncIoProvider::TAKE_OWNERSHIP) && close(fd) < 0) {
      KJ_FAIL_SYSCALL("close", errno, fd) {
        // Recoverable exceptions are safe in destructors.
        break;
      }
    }
  }

  kj::Promise<void> onReadable() {
    KJ_REQUIRE(readable == nullptr, "Must wait for previous event to complete.");

    auto paf = kj::newPromiseAndFulfiller<void>();
    readable = kj::mv(paf.fulfiller);

    CFFileDescriptorEnableCallBacks(fdRef, kCFFileDescriptorReadCallBack);
    return kj::mv(paf.promise);
  }

  kj::Promise<void> onWritable() {
    KJ_REQUIRE(writable == nullptr, "Must wait for previouse event to complete.");

    auto paf = kj::newPromiseAndFulfiller<void>();
    writable = kj::mv(paf.fulfiller);

    CFFileDescriptorEnableCallBacks(fdRef, kCFFileDescriptorWriteCallBack);
    return kj::mv(paf.promise);
  }

protected:
  const int fd;
private:
  uint flags;
  Maybe<Own<PromiseFulfiller<void>>> readable;
  Maybe<Own<PromiseFulfiller<void>>> writable;
  CFFileDescriptorRef fdRef;

  void fdEvent(CFOptionFlags callbackTypes) {
    // CFFileDescriptor callbacks are one-shot therefore are automatically
    // disabled. No need to manually disable the appropriate callback here
    if (callbackTypes & kCFFileDescriptorReadCallBack) {
      KJ_ASSERT_NONNULL(readable)->fulfill();
      readable = nullptr;
    }
    if (callbackTypes & kCFFileDescriptorWriteCallBack) {
      KJ_ASSERT_NONNULL(writable)->fulfill();
      writable = nullptr;
    }
  }

  friend void ::kjCocoaFdCallback(CFFileDescriptorRef, CFOptionFlags, void*);
};

class CocoaIoStream: public CocoaOwnedFileDescriptor, public kj::AsyncIoStream {
public:
  CocoaIoStream(CFRunLoopRef runLoop, CFStringRef runLoopMode, int fd, uint flags) : CocoaOwnedFileDescriptor(runLoop, runLoopMode, fd, flags) {}
  virtual ~CocoaIoStream() noexcept(false) {}

  kj::Promise<size_t> read(void* buffer, size_t minBytes, size_t maxBytes) override {
    return tryReadInternal(buffer, minBytes, maxBytes, 0).then([=](size_t result) {
      KJ_REQUIRE(result >= minBytes, "Premature EOF") {
        memset(reinterpret_cast<byte*>(buffer) + result, 0, minBytes - result);
        return minBytes;
      }
      return result;
    });
  }

  kj::Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
    return tryReadInternal(buffer, minBytes, maxBytes, 0);
  }

  kj::Promise<void> write(const void* buffer, size_t size) override {
    ssize_t writeResult;
    KJ_NONBLOCKING_SYSCALL(writeResult = ::write(fd, buffer, size)) {
      return kj::READY_NOW;
    }

    // A negative result means EAGAIN, which we can treat the same as having written zero bytes.
    size_t n = writeResult < 0 ? 0 : writeResult;

    if (n == size) {
      return kj::READY_NOW;
    } else {
      buffer = reinterpret_cast<const byte*>(buffer) + n;
      size -= n;
    }

    return onWritable().then([=]() {
      return write(buffer, size);
    });
  }

  kj::Promise<void> write(kj::ArrayPtr<const kj::ArrayPtr<const byte>> pieces) override {
    if (pieces.size() == 0) {
      return writeInternal(nullptr, nullptr);
    } else {
      return writeInternal(pieces[0], pieces.slice(1, pieces.size()));
    }
  }

  void shutdownWrite() override {
    // There's no legitimate way to get an AsyncStreamFd that isn't a socket through the
    // UnixAsyncIoProvider interface.
    KJ_SYSCALL(shutdown(fd, SHUT_WR));
  }

private:
  kj::Promise<size_t> tryReadInternal(void* buffer, size_t minBytes, size_t maxBytes,
                                      size_t alreadyRead) {
    // `alreadyRead` is the number of bytes we have already received via previous reads -- minBytes,
    // maxBytes, and buffer have already been adjusted to account for them, but this count must
    // be included in the final return value.

    ssize_t n;
    KJ_NONBLOCKING_SYSCALL(n = ::read(fd, buffer, maxBytes)) {
      return alreadyRead;
    }

    if (n < 0) {
      // Read would block.
      return onReadable().then([=]() {
        return tryReadInternal(buffer, minBytes, maxBytes, alreadyRead);
      });
    } else if (n == 0) {
      // EOF -OR- maxBytes == 0.
      return alreadyRead;
    } else if (kj::implicitCast<size_t>(n) < minBytes) {
      // The kernel returned fewer bytes than we asked for (and fewer than we need).  This indicates
      // that we're out of data.  It could also mean we're at EOF.  We could check for EOF by doing
      // another read just to see if it returns zero, but that would mean making a redundant syscall
      // every time we receive a message on a long-lived connection.  So, instead, we optimistically
      // asume we are not at EOF and return to the event loop.
      buffer = reinterpret_cast<byte*>(buffer) + n;
      minBytes -= n;
      maxBytes -= n;
      alreadyRead += n;
      return onReadable().then([=]() {
        return tryReadInternal(buffer, minBytes, maxBytes, alreadyRead);
      });
    } else {
      // We read enough to stop here.
      return alreadyRead + n;
    }
  }

  kj::Promise<void> writeInternal(kj::ArrayPtr<const byte> firstPiece,
                                  kj::ArrayPtr<const kj::ArrayPtr<const byte>> morePieces) {
    KJ_STACK_ARRAY(struct iovec, iov, 1 + morePieces.size(), 16, 128);

    // writev() interface is not const-correct.  :(
    iov[0].iov_base = const_cast<byte*>(firstPiece.begin());
    iov[0].iov_len = firstPiece.size();
    for (uint i = 0; i < morePieces.size(); i++) {
      iov[i + 1].iov_base = const_cast<byte*>(morePieces[i].begin());
      iov[i + 1].iov_len = morePieces[i].size();
    }

    ssize_t writeResult;
    KJ_NONBLOCKING_SYSCALL(writeResult = ::writev(fd, iov.begin(), iov.size())) {
      // Error.

      // We can't "return kj::READY_NOW;" inside this block because it causes a memory leak due to
      // a bug that exists in both Clang and GCC:
      //   http://gcc.gnu.org/bugzilla/show_bug.cgi?id=33799
      //   http://llvm.org/bugs/show_bug.cgi?id=12286
      goto error;
    }
    if (false) {
    error:
      return kj::READY_NOW;
    }

    // A negative result means EAGAIN, which we can treat the same as having written zero bytes.
    size_t n = writeResult < 0 ? 0 : writeResult;

    // Discard all data that was written, then issue a new write for what's left (if any).
    for (;;) {
      if (n < firstPiece.size()) {
        // Only part of the first piece was consumed.  Wait for POLLOUT and then write again.
        firstPiece = firstPiece.slice(n, firstPiece.size());
        return onWritable().then([=]() {
          return writeInternal(firstPiece, morePieces);
        });
      } else if (morePieces.size() == 0) {
        // First piece was fully-consumed and there are no more pieces, so we're done.
        KJ_DASSERT(n == firstPiece.size(), n);
        return kj::READY_NOW;
      } else {
        // First piece was fully consumed, so move on to the next piece.
        n -= firstPiece.size();
        firstPiece = morePieces[0];
        morePieces = morePieces.slice(1, morePieces.size());
      }
    }
  }
};

class CocoaConnectionReceiver final: public ConnectionReceiver, public CocoaOwnedFileDescriptor {
  // Like CocoaIoStream but for ConnectionReceiver.  This is also largely copied from kj/async-io.c++.

public:
  CocoaConnectionReceiver(CFRunLoopRef runLoop, CFStringRef runLoopMode, int fd, uint flags)
      : CocoaOwnedFileDescriptor(runLoop, runLoopMode, fd, flags), runLoop(runLoop), runLoopMode(runLoopMode) {
    CFRetain(runLoop);
    CFRetain(runLoopMode);
  }
  ~CocoaConnectionReceiver() {
    CFRelease(runLoop);
    CFRelease(runLoopMode);
  }

  kj::Promise<kj::Own<kj::AsyncIoStream>> accept() override {
    int newFd;

  retry:
    newFd = ::accept(fd, nullptr, nullptr);

    if (newFd >= 0) {
      static constexpr uint NEW_FD_FLAGS = kj::LowLevelAsyncIoProvider::TAKE_OWNERSHIP;
      return kj::Own<kj::AsyncIoStream>(kj::heap<CocoaIoStream>(runLoop, runLoopMode, newFd, NEW_FD_FLAGS));
    } else {
      int error = errno;

      switch (error) {
        case EAGAIN:
#if EAGAIN != EWOULDBLOCK
        case EWOULDBLOCK:
#endif
          // Not ready yet.
          return onReadable().then([this]() {
            return accept();
          });

        case EINTR:
        case ENETDOWN:
        case EPROTO:
        case EHOSTDOWN:
        case EHOSTUNREACH:
        case ENETUNREACH:
        case ECONNABORTED:
        case ETIMEDOUT:
          // According to the Linux man page, accept() may report an error if the accepted
          // connection is already broken.  In this case, we really ought to just ignore it and
          // keep waiting.  But it's hard to say exactly what errors are such network errors and
          // which ones are permanent errors.  We've made a guess here.
          goto retry;

        default:
          KJ_FAIL_SYSCALL("accept", error);
      }

    }
  }

  uint getPort() override {
    socklen_t addrlen;
    union {
      struct sockaddr generic;
      struct sockaddr_in inet4;
      struct sockaddr_in6 inet6;
    } addr;
    addrlen = sizeof(addr);
    KJ_SYSCALL(getsockname(fd, &addr.generic, &addrlen));
    switch (addr.generic.sa_family) {
      case AF_INET: return ntohs(addr.inet4.sin_port);
      case AF_INET6: return ntohs(addr.inet6.sin6_port);
      default: return 0;
    }
  }
private:
  CFRunLoopRef runLoop;
  CFStringRef runLoopMode;
};

/*
class CocoaDatagramPort final: public DatagramPort, public CocoaOwnedFileDescriptor {
public:
  Promise<size_t> send(const void* buffer, size_t size, NetworkAddress& destination) {
    // TODO
  }
    
  Promise<size_t> send(ArrayPtr<const ArrayPtr<const byte>> pieces,
                       NetworkAddress& destination) {
    // TODO
  }

  Own<DatagramReceiver> makeReceiver(DatagramReceiver::Capacity capacity) override;

  uint getPort() override {
    // TODO
  }

  void getsockopt(int level, int option, void* value, uint* length) override {
    socklen_t socklen = *length;
    KJ_SYSCALL(::getsockopt(fd, level, option, value, &socklen));
    *length = socklen;
  }

  void setsockopt(int level, int option, const void* value, uint length) override {
    KJ_SYSCALL(::setsockopt(fd, level, option, value, length));
  }
private:
  LowLevelAsyncIoProvider& lowLevel;
  EventPort& eventPort;
};
*/

class CocoaLowLevelAsyncIoProvider final: public LowLevelAsyncIoProvider {
public:
  CocoaLowLevelAsyncIoProvider(CFRunLoopRef runLoop, CFStringRef runLoopMode) : eventPort(runLoop, runLoopMode), waitScope(eventPort.getKjLoop()) {}

  WaitScope& getWaitScope() { return waitScope; }
  CocoaEventPort& getEventPort() { return eventPort; }

  Own<AsyncInputStream> wrapInputFd(int fd, uint flags = 0) override {
    return kj::heap<CocoaIoStream>(eventPort.getRunLoop(), eventPort.getRunLoopMode(), fd, flags);
  }

  Own<AsyncOutputStream> wrapOutputFd(int fd, uint flags = 0) override {
    return kj::heap<CocoaIoStream>(eventPort.getRunLoop(), eventPort.getRunLoopMode(), fd, flags);
  }

  Own<AsyncIoStream> wrapSocketFd(int fd, uint flags = 0) override {
    return kj::heap<CocoaIoStream>(eventPort.getRunLoop(), eventPort.getRunLoopMode(), fd, flags);
  }

  Promise<Own<AsyncIoStream>> wrapConnectingSocketFd(int fd, uint flags = 0) override {
    auto result = kj::heap<CocoaIoStream>(eventPort.getRunLoop(), eventPort.getRunLoopMode(), fd, flags);
    auto connected = result->onWritable();
    return connected.then(kj::mvCapture(result,
        [fd](Own<AsyncIoStream>&& stream) {
          int err;
          socklen_t errlen = sizeof(err);
          KJ_SYSCALL(getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &errlen));
          if (err != 0) {
            KJ_FAIL_SYSCALL("connect()", err) { break; }
          }
          return kj::mv(stream);
        }));
  }

  Own<ConnectionReceiver> wrapListenSocketFd(int fd, uint flags = 0) override {
    return kj::heap<CocoaConnectionReceiver>(eventPort.getRunLoop(), eventPort.getRunLoopMode(), fd, flags);
  }

/*
  Own<DatagramPort> wrapDatagramSocketFd(int fd, uint flags = 0) {
      return kj::heap<CocoaDatagramPort>(eventPort.getRunLoop(), eventPort.getRunLoopMode(), fd, flags);
  }
*/
 
  Timer& getTimer() override {
    KJ_FAIL_ASSERT("Timers not implemented.");
  }
private:
  CocoaEventPort eventPort;
  WaitScope waitScope;
};

} // end anonymous ns

AsyncCocoaIoContext setupCocoaAsyncIo(CFRunLoopRef runLoop, CFStringRef runLoopMode) {
  auto lowLevel = kj::heap<CocoaLowLevelAsyncIoProvider>(runLoop ? runLoop : CFRunLoopGetCurrent(), runLoopMode);
  auto ioProvider = kj::newAsyncIoProvider(*lowLevel);
  auto& waitScope = lowLevel->getWaitScope();
  return { kj::mv(lowLevel), kj::mv(ioProvider), waitScope };
}

}

void kjCocoaFdCallback(CFFileDescriptorRef fdRef, CFOptionFlags callbackTypes, void* info) {
  reinterpret_cast<kj::CocoaOwnedFileDescriptor*>(info)->fdEvent(callbackTypes);
}

void kjCocoaScheduleCallback(void* info) {
  reinterpret_cast<kj::CocoaEventPort*>(info)->run();
}

#endif
