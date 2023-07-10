// Copyright (c) 2013-2014 Sandstorm Development Group, Inc. and contributors
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
// For Win32 implementation, see async-io-win32.c++.

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
// Request 64-bit off_t for sendfile(). (The code will still work if we get 32-bit off_t as long
// as actual files are under 4GB.)
#endif

#include "async-io.h"
#include "async-io-internal.h"
#include "async-unix.h"
#include "debug.h"
#include "thread.h"
#include "io.h"
#include "miniposix.h"
#include <unistd.h>
#include <sys/uio.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stddef.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <set>
#include <poll.h>
#include <limits.h>
#include <sys/ioctl.h>
#include <kj/filesystem.h>

#if __linux__
#include <sys/sendfile.h>
#endif

#if !defined(SO_PEERCRED) && defined(LOCAL_PEERCRED)
#include <sys/ucred.h>
#endif

#if !defined(SOL_LOCAL) && (__FreeBSD__ || __DragonflyBSD__ || __APPLE__)
// On DragonFly, FreeBSD < 12.2 and older Darwin you're supposed to use 0 for SOL_LOCAL.
#define SOL_LOCAL 0
#endif

namespace kj {

namespace {

void setNonblocking(int fd) {
#ifdef FIONBIO
  int opt = 1;
  KJ_SYSCALL(ioctl(fd, FIONBIO, &opt));
#else
  int flags;
  KJ_SYSCALL(flags = fcntl(fd, F_GETFL));
  if ((flags & O_NONBLOCK) == 0) {
    KJ_SYSCALL(fcntl(fd, F_SETFL, flags | O_NONBLOCK));
  }
#endif
}

void setCloseOnExec(int fd) {
#ifdef FIOCLEX
  KJ_SYSCALL(ioctl(fd, FIOCLEX));
#else
  int flags;
  KJ_SYSCALL(flags = fcntl(fd, F_GETFD));
  if ((flags & FD_CLOEXEC) == 0) {
    KJ_SYSCALL(fcntl(fd, F_SETFD, flags | FD_CLOEXEC));
  }
#endif
}

static constexpr uint NEW_FD_FLAGS =
#if __linux__ && !__BIONIC__
    LowLevelAsyncIoProvider::ALREADY_CLOEXEC | LowLevelAsyncIoProvider::ALREADY_NONBLOCK |
#endif
    LowLevelAsyncIoProvider::TAKE_OWNERSHIP;
// We always try to open FDs with CLOEXEC and NONBLOCK already set on Linux, but on other platforms
// this is not possible.

class OwnedFileDescriptor {
public:
  OwnedFileDescriptor(int fd, uint flags): fd(fd), flags(flags) {
    if (flags & LowLevelAsyncIoProvider::ALREADY_NONBLOCK) {
      KJ_DREQUIRE(fcntl(fd, F_GETFL) & O_NONBLOCK, "You claimed you set NONBLOCK, but you didn't.");
    } else {
      setNonblocking(fd);
    }

    if (flags & LowLevelAsyncIoProvider::TAKE_OWNERSHIP) {
      if (flags & LowLevelAsyncIoProvider::ALREADY_CLOEXEC) {
        KJ_DREQUIRE(fcntl(fd, F_GETFD) & FD_CLOEXEC,
                    "You claimed you set CLOEXEC, but you didn't.");
      } else {
        setCloseOnExec(fd);
      }
    }
  }

  ~OwnedFileDescriptor() noexcept(false) {
    // Don't use SYSCALL() here because close() should not be repeated on EINTR.
    if ((flags & LowLevelAsyncIoProvider::TAKE_OWNERSHIP) && close(fd) < 0) {
      KJ_FAIL_SYSCALL("close", errno, fd) {
        // Recoverable exceptions are safe in destructors.
        break;
      }
    }
  }

protected:
  const int fd;

private:
  uint flags;
};

// =======================================================================================

class AsyncStreamFd: public OwnedFileDescriptor, public AsyncCapabilityStream {
public:
  AsyncStreamFd(UnixEventPort& eventPort, int fd, uint flags, uint observerFlags)
      : OwnedFileDescriptor(fd, flags),
        eventPort(eventPort),
        observer(eventPort, fd, observerFlags) {}
  virtual ~AsyncStreamFd() noexcept(false) {}

  Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
    return tryReadInternal(buffer, minBytes, maxBytes, nullptr, 0, {0,0})
        .then([](ReadResult r) { return r.byteCount; });
  }

  Promise<ReadResult> tryReadWithFds(void* buffer, size_t minBytes, size_t maxBytes,
                                     AutoCloseFd* fdBuffer, size_t maxFds) override {
    return tryReadInternal(buffer, minBytes, maxBytes, fdBuffer, maxFds, {0,0});
  }

  Promise<ReadResult> tryReadWithStreams(
      void* buffer, size_t minBytes, size_t maxBytes,
      Own<AsyncCapabilityStream>* streamBuffer, size_t maxStreams) override {
    auto fdBuffer = kj::heapArray<AutoCloseFd>(maxStreams);
    auto promise = tryReadInternal(buffer, minBytes, maxBytes, fdBuffer.begin(), maxStreams, {0,0});

    return promise.then([this, fdBuffer = kj::mv(fdBuffer), streamBuffer]
                        (ReadResult result) mutable {
      for (auto i: kj::zeroTo(result.capCount)) {
        streamBuffer[i] = kj::heap<AsyncStreamFd>(eventPort, fdBuffer[i].release(),
            LowLevelAsyncIoProvider::TAKE_OWNERSHIP | LowLevelAsyncIoProvider::ALREADY_CLOEXEC,
            UnixEventPort::FdObserver::OBSERVE_READ_WRITE);
      }
      return result;
    });
  }

  Promise<void> write(const void* buffer, size_t size) override {
    ssize_t n;
    KJ_NONBLOCKING_SYSCALL(n = ::write(fd, buffer, size)) {
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

    if (n < 0) {
      // EAGAIN -- need to wait for writability and try again.
      return observer.whenBecomesWritable().then([=]() {
        return write(buffer, size);
      });
    } else if (n == size) {
      // All done.
      return READY_NOW;
    } else {
      // Fewer than `size` bytes were written, but we CANNOT assume we're out of buffer space, as
      // Linux is known to return partial reads/writes when interrupted by a signal -- yes, even
      // for non-blocking operations. So, we'll need to write() again now, even though it will
      // almost certainly fail with EAGAIN. See comments in the read path for more info.
      buffer = reinterpret_cast<const byte*>(buffer) + n;
      size -= n;
      return write(buffer, size);
    }
  }

  Promise<void> write(ArrayPtr<const ArrayPtr<const byte>> pieces) override {
    if (pieces.size() == 0) {
      return writeInternal(nullptr, nullptr, nullptr);
    } else {
      return writeInternal(pieces[0], pieces.slice(1, pieces.size()), nullptr);
    }
  }

  Promise<void> writeWithFds(ArrayPtr<const byte> data,
                             ArrayPtr<const ArrayPtr<const byte>> moreData,
                             ArrayPtr<const int> fds) override {
    return writeInternal(data, moreData, fds);
  }

  Promise<void> writeWithStreams(ArrayPtr<const byte> data,
                                 ArrayPtr<const ArrayPtr<const byte>> moreData,
                                 Array<Own<AsyncCapabilityStream>> streams) override {
    auto fds = KJ_MAP(stream, streams) {
      return downcast<AsyncStreamFd>(*stream).fd;
    };
    auto promise = writeInternal(data, moreData, fds);
    return promise.attach(kj::mv(fds), kj::mv(streams));
  }

  Maybe<Promise<uint64_t>> tryPumpFrom(
      AsyncInputStream& input, uint64_t amount = kj::maxValue) override {
#if __linux__ && !__ANDROID__
    KJ_IF_MAYBE(sock, kj::dynamicDowncastIfAvailable<AsyncStreamFd>(input)) {
      return pumpFromOther(*sock, amount);
    }
#endif

#if __linux__
    KJ_IF_MAYBE(file, kj::dynamicDowncastIfAvailable<FileInputStream>(input)) {
      KJ_IF_MAYBE(fd, file->getUnderlyingFile().getFd()) {
        return pumpFromFile(*file, *fd, amount, 0);
      }
    }
#endif

    return nullptr;
  }

#if __linux__
  // TODO(someday): Support sendfile on other OS's... unfortunately, it works differently on
  //   different systems.

private:
  Promise<uint64_t> pumpFromFile(FileInputStream& input, int fileFd,
                                 uint64_t amount, uint64_t soFar) {
    while (soFar < amount) {
      off_t offset = input.getOffset();
      ssize_t n;

      // Although sendfile()'s last argument has type size_t, on Linux it seems to cause EINVAL
      // if we pass an amount that is greater than UINT32_MAX, so make sure to clamp to that. In
      // practice, of course, we'll be limited to the socket buffer size.
      size_t requested = kj::min(amount - soFar, (uint32_t)kj::maxValue);

      KJ_SYSCALL_HANDLE_ERRORS(n = sendfile(fd, fileFd, &offset, requested)) {
        case EINVAL:
        case ENOSYS:
          // Fall back to regular pump
          return unoptimizedPumpTo(input, *this, amount, soFar);

        case EAGAIN:
          return observer.whenBecomesWritable()
              .then([this, &input, fileFd, amount, soFar]() {
            return pumpFromFile(input, fileFd, amount, soFar);
          });

        default:
          KJ_FAIL_SYSCALL("sendfile", error);
      }

      if (n == 0) break;

      input.seek(offset);  // NOTE: sendfile() updated `offset` in-place.
      soFar += n;
    }

    return soFar;
  }

public:
#endif  // __linux__

#if __linux__ && !__ANDROID__
// Linux's splice() syscall lets us optimize pumping of bytes between file descriptors.
//
// TODO(someday): splice()-based pumping hangs in unit tests on Android for some reason. We should
//   figure out why, but for now I'm just disabling it...

private:
  Maybe<Promise<uint64_t>> pumpFromOther(AsyncStreamFd& input, uint64_t amount) {
    // The input is another AsyncStreamFd, so perhaps we can do an optimized pump with splice().

    // Before we resort to a bunch of syscalls, let's try to see if the pump is small and able to
    // be fully satisfied immediately. This optimizes for the case of small streams, e.g. a short
    // HTTP body.

    byte buffer[4096];
    size_t pos = 0;
    size_t initialAmount = kj::min(sizeof(buffer), amount);

    bool eof = false;

    // Read into the buffer until it's full or there are no bytes available. Note that we'd expect
    // one call to read() will pull as much data out of the socket as possible (up to our buffer
    // size), so you might think the loop is unnecessary. The reason we want to do a second read(),
    // though, is to find out if we're at EOF or merely waiting for more data. In the EOF case,
    // we can end the pump early without splicing.
    while (pos < initialAmount) {
      ssize_t n;
      KJ_NONBLOCKING_SYSCALL(n = ::read(input.fd, buffer + pos, initialAmount - pos));
      if (n <= 0) {
        eof = n == 0;
        break;
      }
      pos += n;
    }

    // Write the bytes that we just read back out to the output.
    {
      ssize_t n;
      KJ_NONBLOCKING_SYSCALL(n = ::write(fd, buffer, pos));
      if (n < 0) n = 0;  // treat EAGAIN as "zero bytes written"
      if (size_t(n) < pos) {
        // Oh crap, the output buffer is full. This should be rare. But, now we're going to have
        // to copy the remaining bytes into the heap to do an async write.
        auto leftover = kj::heapArray<byte>(buffer + n, pos - n);
        auto promise = write(leftover.begin(), leftover.size());
        promise = promise.attach(kj::mv(leftover));
        if (eof || pos == amount) {
          return promise.then([pos]() -> uint64_t { return pos; });
        } else {
          return promise.then([&input, this, pos, amount]() {
            return splicePumpFrom(input, pos, amount);
          });
        }
      }
    }

    if (eof || pos == amount) {
      // We finished the pump in one go, so don't splice.
      return Promise<uint64_t>(uint64_t(pos));
    } else {
      // Use splice for the rest of the pump.
      return splicePumpFrom(input, pos, amount);
    }
  }

  static constexpr size_t MAX_SPLICE_LEN = 1 << 20;
  // Maximum value we'll pass for the `len` argument of `splice()`. Linux does not like it when we
  // use `kj::maxValue` here so we clamp it. Note that the actual value of this constant is
  // irrelevanta as long as it is more than the pipe buffer size (typically 64k) and less than
  // whatever value makes Linux unhappy. All actual operations will be clamped to the buffer size.
  // (And if the buffer size is for some reason larger than this, that's OK too, we just won't
  // end up using the whole buffer.)

  Promise<uint64_t> splicePumpFrom(AsyncStreamFd& input, uint64_t readSoFar, uint64_t limit) {
    // splice() requires that either its input or its output is a pipe. But chances are neither
    // `input.fd` nor `this->fd` is a pipe -- in most use cases they are sockets. In order to take
    // advantage of splice(), then, we need to allocate a pipe to act as the middleman, so we can
    // splice() from the input to the pipe, and then from the pipe to the output.
    //
    // You might wonder why this pipe middleman is required. Why can't splice() go directly from
    // a socket to a socket? Linus Torvalds attempts to explain here:
    //     https://yarchive.net/comp/linux/splice.html
    //
    // The short version is that the pipe itself is equivalent to an in-memory buffer. In a naive
    // pump implementation, we allocate a buffer, read() into it and write() out. With splice(),
    // we allocate a kernelspace buffer by allocating a pipe, then we splice() into the pipe and
    // splice() back out.

    // Linux normally allocates pipe buffers of 64k (16 pages of 4k each). However, when
    // /proc/sys/fs/pipe-user-pages-soft is hit, then Linux will start allocating 4k (1 page)
    // buffers instead, and will give an error if we try to increase it.
    //
    // The soft limit defaults to 16384 pages, which we'd hit after 1024 pipes -- totally possible
    // in a big server. 64k is a nice buffer size, but even 4k is better than not using splice, so
    // we'll live with whatever buffer size the kernel gives us.
    //
    // There is a second, "hard" limit, /proc/sys/fs/pipe-user-pages-hard, at which point Linux
    // will start refusing to allocate pipes at all. In this case we fall back to an unoptimized
    // pump. However, this limit defaults to unlimited, so this won't ever happen unless someone
    // has manually changed the limit. That's probably dangerous since if the app allocates pipes
    // anywhere else in its codebase, it probably doesn't have any fallbacks in those places, so
    // things will break anyway... to avoid that we'd need to self-regulate the number of pipes
    // we allocate here to avoid coming close to the hard limit, but that's a lot of effort so I'm
    // not going to bother!

    int pipeFds[2];
    KJ_SYSCALL_HANDLE_ERRORS(pipe2(pipeFds, O_NONBLOCK | O_CLOEXEC)) {
      case ENFILE:
        // Probably hit the limit on pipe buffers, fall back to unoptimized pump.
        return unoptimizedPumpTo(input, *this, limit, readSoFar);
      default:
        KJ_FAIL_SYSCALL("pipe2()", error);
    }

    AutoCloseFd pipeIn(pipeFds[0]), pipeOut(pipeFds[1]);

    return splicePumpLoop(input, pipeFds[0], pipeFds[1], readSoFar, limit, 0)
        .attach(kj::mv(pipeIn), kj::mv(pipeOut));
  }

  Promise<uint64_t> splicePumpLoop(AsyncStreamFd& input, int pipeIn, int pipeOut,
                                   uint64_t readSoFar, uint64_t limit, size_t bufferedAmount) {
    for (;;) {
      while (bufferedAmount > 0) {
        // First flush out whatever is in the pipe buffer.
        ssize_t n;
        KJ_NONBLOCKING_SYSCALL(n = splice(pipeIn, nullptr, fd, nullptr,
            MAX_SPLICE_LEN, SPLICE_F_MOVE | SPLICE_F_NONBLOCK));
        if (n > 0) {
          KJ_ASSERT(n <= bufferedAmount, "splice pipe larger than bufferedAmount?");
          bufferedAmount -= n;
        } else {
          KJ_ASSERT(n < 0, "splice pipe empty before bufferedAmount reached?", bufferedAmount);
          return observer.whenBecomesWritable()
              .then([this, &input, pipeIn, pipeOut, readSoFar, limit, bufferedAmount]() {
            return splicePumpLoop(input, pipeIn, pipeOut, readSoFar, limit, bufferedAmount);
          });
        }
      }

      // Now the pipe buffer is empty, so we can try to read some more.
      {
        if (readSoFar >= limit) {
          // Hit the limit, we're done.
          KJ_ASSERT(readSoFar == limit);
          return readSoFar;
        }

        ssize_t n;
        KJ_NONBLOCKING_SYSCALL(n = splice(input.fd, nullptr, pipeOut, nullptr,
            kj::min(limit - readSoFar, MAX_SPLICE_LEN), SPLICE_F_MOVE | SPLICE_F_NONBLOCK));
        if (n == 0) {
          // EOF.
          return readSoFar;
        } else if (n < 0) {
          // No data available, wait.
          return input.observer.whenBecomesReadable()
              .then([this, &input, pipeIn, pipeOut, readSoFar, limit]() {
            return splicePumpLoop(input, pipeIn, pipeOut, readSoFar, limit, 0);
          });
        }

        readSoFar += n;
        bufferedAmount = n;
      }
    }
  }

public:
#endif  // __linux__ && !__ANDROID__

  Promise<void> whenWriteDisconnected() override {
    KJ_IF_MAYBE(p, writeDisconnectedPromise) {
      return p->addBranch();
    } else {
      auto fork = observer.whenWriteDisconnected().fork();
      auto result = fork.addBranch();
      writeDisconnectedPromise = kj::mv(fork);
      return kj::mv(result);
    }
  }

  void shutdownWrite() override {
    // There's no legitimate way to get an AsyncStreamFd that isn't a socket through the
    // UnixAsyncIoProvider interface.
    KJ_SYSCALL(shutdown(fd, SHUT_WR));
  }

  void abortRead() override {
    // There's no legitimate way to get an AsyncStreamFd that isn't a socket through the
    // UnixAsyncIoProvider interface.
    KJ_SYSCALL(shutdown(fd, SHUT_RD));
  }

  void getsockopt(int level, int option, void* value, uint* length) override {
    socklen_t socklen = *length;
    KJ_SYSCALL(::getsockopt(fd, level, option, value, &socklen));
    *length = socklen;
  }

  void setsockopt(int level, int option, const void* value, uint length) override {
    KJ_SYSCALL(::setsockopt(fd, level, option, value, length));
  }

  void getsockname(struct sockaddr* addr, uint* length) override {
    socklen_t socklen = *length;
    KJ_SYSCALL(::getsockname(fd, addr, &socklen));
    *length = socklen;
  }

  void getpeername(struct sockaddr* addr, uint* length) override {
    socklen_t socklen = *length;
    KJ_SYSCALL(::getpeername(fd, addr, &socklen));
    *length = socklen;
  }

  kj::Maybe<int> getFd() const override {
    return fd;
  }

  void registerAncillaryMessageHandler(
      kj::Function<void(kj::ArrayPtr<AncillaryMessage>)> fn) override {
    ancillaryMsgCallback = kj::mv(fn);
  }

  Promise<void> waitConnected() {
    // Wait until initial connection has completed. This actually just waits until it is writable.

    // Can't just go directly to writeObserver.whenBecomesWritable() because of edge triggering. We
    // need to explicitly check if the socket is already connected.

    struct pollfd pollfd;
    memset(&pollfd, 0, sizeof(pollfd));
    pollfd.fd = fd;
    pollfd.events = POLLOUT;

    int pollResult;
    KJ_SYSCALL(pollResult = poll(&pollfd, 1, 0));

    if (pollResult == 0) {
      // Not ready yet. We can safely use the edge-triggered observer.
      return observer.whenBecomesWritable();
    } else {
      // Ready now.
      return kj::READY_NOW;
    }
  }

private:
  UnixEventPort& eventPort;
  UnixEventPort::FdObserver observer;
  Maybe<ForkedPromise<void>> writeDisconnectedPromise;
  Maybe<Function<void(ArrayPtr<AncillaryMessage>)>> ancillaryMsgCallback;

  Promise<ReadResult> tryReadInternal(void* buffer, size_t minBytes, size_t maxBytes,
                                      AutoCloseFd* fdBuffer, size_t maxFds,
                                      ReadResult alreadyRead) {
    // `alreadyRead` is the number of bytes we have already received via previous reads -- minBytes,
    // maxBytes, and buffer have already been adjusted to account for them, but this count must
    // be included in the final return value.

    ssize_t n;
    if (maxFds == 0 && ancillaryMsgCallback == nullptr) {
      KJ_NONBLOCKING_SYSCALL(n = ::read(fd, buffer, maxBytes)) {
        // Error.

        // We can't "return kj::READY_NOW;" inside this block because it causes a memory leak due to
        // a bug that exists in both Clang and GCC:
        //   http://gcc.gnu.org/bugzilla/show_bug.cgi?id=33799
        //   http://llvm.org/bugs/show_bug.cgi?id=12286
        goto error;
      }
    } else {
      struct msghdr msg;
      memset(&msg, 0, sizeof(msg));

      struct iovec iov;
      memset(&iov, 0, sizeof(iov));
      iov.iov_base = buffer;
      iov.iov_len = maxBytes;
      msg.msg_iov = &iov;
      msg.msg_iovlen = 1;

      // Allocate space to receive a cmsg.
      size_t msgBytes;
      if (ancillaryMsgCallback == nullptr) {
#if __APPLE__ || __FreeBSD__
        // Until very recently (late 2018 / early 2019), FreeBSD suffered from a bug in which when
        // an SCM_RIGHTS message was truncated on delivery, it would not close the FDs that weren't
        // delivered -- they would simply leak: https://bugs.freebsd.org/131876
        //
        // My testing indicates that MacOS has this same bug as of today (April 2019). I don't know
        // if they plan to fix it or are even aware of it.
        //
        // To handle both cases, we will always provide space to receive 512 FDs. Hopefully, this is
        // greater than the maximum number of FDs that these kernels will transmit in one message
        // PLUS enough space for any other ancillary messages that could be sent before the
        // SCM_RIGHTS message to push it back in the buffer. I couldn't find any firm documentation
        // on these limits, though -- I only know that Linux is limited to 253, and I saw a hint in
        // a comment in someone else's application that suggested FreeBSD is the same. Hopefully,
        // then, this is sufficient to prevent attacks. But if not, there's nothing more we can do;
        // it's really up to the kernel to fix this.
        msgBytes = CMSG_SPACE(sizeof(int) * 512);
#else
        msgBytes = CMSG_SPACE(sizeof(int) * maxFds);
#endif
      } else {
        // If we want room for ancillary messages instead of or in addition to FDs, just use the
        // same amount of cushion as in the MacOS/FreeBSD case above.
        // Someday we may want to allow customization here, but there's no immediate use for it.
        msgBytes = CMSG_SPACE(sizeof(int) * 512);
      }

      // On Linux, CMSG_SPACE will align to a word-size boundary, but on Mac it always aligns to a
      // 32-bit boundary. I guess aligning to 32 bits helps avoid the problem where you
      // surprisingly end up with space for two file descriptors when you only wanted one. However,
      // cmsghdr's preferred alignment is word-size (it contains a size_t). If we stack-allocate
      // the buffer, we need to make sure it is aligned properly (maybe not on x64, but maybe on
      // other platforms), so we want to allocate an array of words (we use void*). So... we use
      // CMSG_SPACE() and then additionally round up to deal with Mac.
      size_t msgWords = (msgBytes + sizeof(void*) - 1) / sizeof(void*);
      KJ_STACK_ARRAY(void*, cmsgSpace, msgWords, 16, 256);
      auto cmsgBytes = cmsgSpace.asBytes();
      memset(cmsgBytes.begin(), 0, cmsgBytes.size());
      msg.msg_control = cmsgBytes.begin();
      msg.msg_controllen = msgBytes;

#ifdef MSG_CMSG_CLOEXEC
      static constexpr int RECVMSG_FLAGS = MSG_CMSG_CLOEXEC;
#else
      static constexpr int RECVMSG_FLAGS = 0;
#endif

      KJ_NONBLOCKING_SYSCALL(n = ::recvmsg(fd, &msg, RECVMSG_FLAGS)) {
        // Error.

        // We can't "return kj::READY_NOW;" inside this block because it causes a memory leak due to
        // a bug that exists in both Clang and GCC:
        //   http://gcc.gnu.org/bugzilla/show_bug.cgi?id=33799
        //   http://llvm.org/bugs/show_bug.cgi?id=12286
        goto error;
      }

      if (n >= 0) {
        // Process all messages.
        //
        // WARNING DANGER: We have to be VERY careful not to miss a file descriptor here, because
        // if we do, then that FD will never be closed, and a malicious peer could exploit this to
        // fill up our FD table, creating a DoS attack. Some things to keep in mind:
        // - CMSG_SPACE() could have rounded up the space for alignment purposes, and this could
        //   mean we permitted the kernel to deliver more file descriptors than `maxFds`. We need
        //   to close the extras.
        // - We can receive multiple ancillary messages at once. In particular, there is also
        //   SCM_CREDENTIALS. The sender decides what to send. They could send SCM_CREDENTIALS
        //   first followed by SCM_RIGHTS. We need to make sure we see both.
        size_t nfds = 0;
        size_t spaceLeft = msg.msg_controllen;
        Vector<AncillaryMessage> ancillaryMessages;
        for (struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
            cmsg != nullptr; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
          if (spaceLeft >= CMSG_LEN(0) &&
              cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
            // Some operating systems (like MacOS) do not adjust csmg_len when the message is
            // truncated. We must do so ourselves or risk overrunning the buffer.
            auto len = kj::min(cmsg->cmsg_len, spaceLeft);
            auto data = arrayPtr(reinterpret_cast<int*>(CMSG_DATA(cmsg)),
                                 (len - CMSG_LEN(0)) / sizeof(int));
            kj::Vector<kj::AutoCloseFd> trashFds;
            for (auto fd: data) {
              kj::AutoCloseFd ownFd(fd);
              if (nfds < maxFds) {
                fdBuffer[nfds++] = kj::mv(ownFd);
              } else {
                trashFds.add(kj::mv(ownFd));
              }
            }
          } else if (spaceLeft >= CMSG_LEN(0) && ancillaryMsgCallback != nullptr) {
            auto len = kj::min(cmsg->cmsg_len, spaceLeft);
            auto data = ArrayPtr<const byte>(CMSG_DATA(cmsg), len - CMSG_LEN(0));
            ancillaryMessages.add(cmsg->cmsg_level, cmsg->cmsg_type, data);
          }

          if (spaceLeft >= CMSG_LEN(0) && spaceLeft >= cmsg->cmsg_len) {
            spaceLeft -= cmsg->cmsg_len;
          } else {
            spaceLeft = 0;
          }
        }

#ifndef MSG_CMSG_CLOEXEC
        for (size_t i = 0; i < nfds; i++) {
          setCloseOnExec(fdBuffer[i]);
        }
#endif

        if (ancillaryMessages.size() > 0) {
          KJ_IF_MAYBE(fn, ancillaryMsgCallback) {
            (*fn)(ancillaryMessages.asPtr());
          }
        }

        alreadyRead.capCount += nfds;
        fdBuffer += nfds;
        maxFds -= nfds;
      }
    }

    if (false) {
    error:
      return alreadyRead;
    }

    if (n < 0) {
      // Read would block.
      return observer.whenBecomesReadable().then([=]() {
        return tryReadInternal(buffer, minBytes, maxBytes, fdBuffer, maxFds, alreadyRead);
      });
    } else if (n == 0) {
      // EOF -OR- maxBytes == 0.
      return alreadyRead;
    } else if (implicitCast<size_t>(n) >= minBytes) {
      // We read enough to stop here.
      alreadyRead.byteCount += n;
      return alreadyRead;
    } else {
      // The kernel returned fewer bytes than we asked for (and fewer than we need).

      buffer = reinterpret_cast<byte*>(buffer) + n;
      minBytes -= n;
      maxBytes -= n;
      alreadyRead.byteCount += n;

      // According to David Klempner, who works on Stubby at Google, we sadly CANNOT assume that
      // we've consumed the whole read buffer here. If a signal is delivered in the middle of a
      // read() -- yes, even a non-blocking read -- it can cause the kernel to return a partial
      // result, with data still in the buffer.
      //     https://bugzilla.kernel.org/show_bug.cgi?id=199131
      //     https://twitter.com/CaptainSegfault/status/1112622245531144194
      //
      // Unfortunately, we have no choice but to issue more read()s until it either tells us EOF
      // or EAGAIN. We used to have an optimization here using observer.atEndHint() (when it is
      // non-null) to avoid a redundant call to read(). Alas...
      return tryReadInternal(buffer, minBytes, maxBytes, fdBuffer, maxFds, alreadyRead);
    }
  }

  Promise<void> writeInternal(ArrayPtr<const byte> firstPiece,
                              ArrayPtr<const ArrayPtr<const byte>> morePieces,
                              ArrayPtr<const int> fds) {
    const size_t iovmax = kj::miniposix::iovMax();
    // If there are more than IOV_MAX pieces, we'll only write the first IOV_MAX for now, and
    // then we'll loop later.
    KJ_STACK_ARRAY(struct iovec, iov, kj::min(1 + morePieces.size(), iovmax), 16, 128);
    size_t iovTotal = 0;

    // writev() interface is not const-correct.  :(
    iov[0].iov_base = const_cast<byte*>(firstPiece.begin());
    iov[0].iov_len = firstPiece.size();
    iovTotal += iov[0].iov_len;
    for (uint i = 1; i < iov.size(); i++) {
      iov[i].iov_base = const_cast<byte*>(morePieces[i - 1].begin());
      iov[i].iov_len = morePieces[i - 1].size();
      iovTotal += iov[i].iov_len;
    }

    if (iovTotal == 0) {
      KJ_REQUIRE(fds.size() == 0, "can't write FDs without bytes");
      return kj::READY_NOW;
    }

    ssize_t n;
    if (fds.size() == 0) {
      KJ_NONBLOCKING_SYSCALL(n = ::writev(fd, iov.begin(), iov.size()), iovTotal, iov.size()) {
        // Error.

        // We can't "return kj::READY_NOW;" inside this block because it causes a memory leak due to
        // a bug that exists in both Clang and GCC:
        //   http://gcc.gnu.org/bugzilla/show_bug.cgi?id=33799
        //   http://llvm.org/bugs/show_bug.cgi?id=12286
        goto error;
      }
    } else {
      struct msghdr msg;
      memset(&msg, 0, sizeof(msg));
      msg.msg_iov = iov.begin();
      msg.msg_iovlen = iov.size();

      // Allocate space to send a cmsg.
      size_t msgBytes = CMSG_SPACE(sizeof(int) * fds.size());
      // On Linux, CMSG_SPACE will align to a word-size boundary, but on Mac it always aligns to a
      // 32-bit boundary. I guess aligning to 32 bits helps avoid the problem where you
      // surprisingly end up with space for two file descriptors when you only wanted one. However,
      // cmsghdr's preferred alignment is word-size (it contains a size_t). If we stack-allocate
      // the buffer, we need to make sure it is aligned properly (maybe not on x64, but maybe on
      // other platforms), so we want to allocate an array of words (we use void*). So... we use
      // CMSG_SPACE() and then additionally round up to deal with Mac.
      size_t msgWords = (msgBytes + sizeof(void*) - 1) / sizeof(void*);
      KJ_STACK_ARRAY(void*, cmsgSpace, msgWords, 16, 256);
      auto cmsgBytes = cmsgSpace.asBytes();
      memset(cmsgBytes.begin(), 0, cmsgBytes.size());
      msg.msg_control = cmsgBytes.begin();
      msg.msg_controllen = msgBytes;

      struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
      cmsg->cmsg_level = SOL_SOCKET;
      cmsg->cmsg_type = SCM_RIGHTS;
      cmsg->cmsg_len = CMSG_LEN(sizeof(int) * fds.size());
      memcpy(CMSG_DATA(cmsg), fds.begin(), fds.asBytes().size());

      KJ_NONBLOCKING_SYSCALL(n = ::sendmsg(fd, &msg, 0)) {
        // Error.

        // We can't "return kj::READY_NOW;" inside this block because it causes a memory leak due to
        // a bug that exists in both Clang and GCC:
        //   http://gcc.gnu.org/bugzilla/show_bug.cgi?id=33799
        //   http://llvm.org/bugs/show_bug.cgi?id=12286
        goto error;
      }
    }

    if (false) {
    error:
      return kj::READY_NOW;
    }

    if (n < 0) {
      // Got EAGAIN. Nothing was written.
      return observer.whenBecomesWritable().then([=]() {
        return writeInternal(firstPiece, morePieces, fds);
      });
    } else if (n == 0) {
      // Why would a sendmsg() with a non-empty message ever return 0 when writing to a stream
      // socket? If there's no room in the send buffer, it should fail with EAGAIN. If the
      // connection is closed, it should fail with EPIPE. Various documents and forum posts around
      // the internet claim this can happen but no one seems to know when. My guess is it can only
      // happen if we try to send an empty message -- which we didn't. So I think this is
      // impossible. If it is possible, we need to figure out how to correctly handle it, which
      // depends on what caused it.
      //
      // Note in particular that if 0 is a valid return here, and we sent an SCM_RIGHTS message,
      // we need to know whether the message was sent or not, in order to decide whether to retry
      // sending it!
      KJ_FAIL_ASSERT("non-empty sendmsg() returned 0");
    }

    // Non-zero bytes were written. This also implies that *all* FDs were written.

    // Discard all data that was written, then issue a new write for what's left (if any).
    for (;;) {
      if (n < firstPiece.size()) {
        // Only part of the first piece was consumed.  Wait for buffer space and then write again.
        firstPiece = firstPiece.slice(n, firstPiece.size());
        iovTotal -= n;

        if (iovTotal == 0) {
          // Oops, what actually happened is that we hit the IOV_MAX limit. Don't wait.
          return writeInternal(firstPiece, morePieces, nullptr);
        }

        // As with read(), we cannot assume that a short write() really means the write buffer is
        // full (see comments in the read path above). We have to write again.
        return writeInternal(firstPiece, morePieces, nullptr);
      } else if (morePieces.size() == 0) {
        // First piece was fully-consumed and there are no more pieces, so we're done.
        KJ_DASSERT(n == firstPiece.size(), n);
        return READY_NOW;
      } else {
        // First piece was fully consumed, so move on to the next piece.
        n -= firstPiece.size();
        iovTotal -= firstPiece.size();
        firstPiece = morePieces[0];
        morePieces = morePieces.slice(1, morePieces.size());
      }
    }
  }
};

#if __linux__ && !__ANDROID__
constexpr size_t AsyncStreamFd::MAX_SPLICE_LEN;
#endif  // __linux__ && !__ANDROID__

// =======================================================================================

class SocketAddress {
public:
  SocketAddress(const void* sockaddr, uint len): addrlen(len) {
    KJ_REQUIRE(len <= sizeof(addr), "Sorry, your sockaddr is too big for me.");
    memcpy(&addr.generic, sockaddr, len);
  }

  bool operator<(const SocketAddress& other) const {
    // So we can use std::set<SocketAddress>...  see DNS lookup code.

    if (wildcard < other.wildcard) return true;
    if (wildcard > other.wildcard) return false;

    if (addrlen < other.addrlen) return true;
    if (addrlen > other.addrlen) return false;

    return memcmp(&addr.generic, &other.addr.generic, addrlen) < 0;
  }

  const struct sockaddr* getRaw() const { return &addr.generic; }
  socklen_t getRawSize() const { return addrlen; }

  int socket(int type) const {
    bool isStream = type == SOCK_STREAM;

    int result;
#if __linux__ && !__BIONIC__
    type |= SOCK_NONBLOCK | SOCK_CLOEXEC;
#endif
    KJ_SYSCALL(result = ::socket(addr.generic.sa_family, type, 0));

    if (isStream && (addr.generic.sa_family == AF_INET ||
                     addr.generic.sa_family == AF_INET6)) {
      // TODO(perf):  As a hack for the 0.4 release we are always setting
      //   TCP_NODELAY because Nagle's algorithm pretty much kills Cap'n Proto's
      //   RPC protocol.  Later, we should extend the interface to provide more
      //   control over this.  Perhaps write() should have a flag which
      //   specifies whether to pass MSG_MORE.
      int one = 1;
      KJ_SYSCALL(setsockopt(
          result, IPPROTO_TCP, TCP_NODELAY, (char*)&one, sizeof(one)));
    }

    return result;
  }

  void bind(int sockfd) const {
#if !defined(__OpenBSD__)
    if (wildcard) {
      // Disable IPV6_V6ONLY because we want to handle both ipv4 and ipv6 on this socket.  (The
      // default value of this option varies across platforms.)
      int value = 0;
      KJ_SYSCALL(setsockopt(sockfd, IPPROTO_IPV6, IPV6_V6ONLY, &value, sizeof(value)));
    }
#endif

    KJ_SYSCALL(::bind(sockfd, &addr.generic, addrlen), toString());
  }

  uint getPort() const {
    switch (addr.generic.sa_family) {
      case AF_INET: return ntohs(addr.inet4.sin_port);
      case AF_INET6: return ntohs(addr.inet6.sin6_port);
      default: return 0;
    }
  }

  String toString() const {
    if (wildcard) {
      return str("*:", getPort());
    }

    switch (addr.generic.sa_family) {
      case AF_INET: {
        char buffer[INET6_ADDRSTRLEN];
        if (inet_ntop(addr.inet4.sin_family, &addr.inet4.sin_addr,
                      buffer, sizeof(buffer)) == nullptr) {
          KJ_FAIL_SYSCALL("inet_ntop", errno) { break; }
          return heapString("(inet_ntop error)");
        }
        return str(buffer, ':', ntohs(addr.inet4.sin_port));
      }
      case AF_INET6: {
        char buffer[INET6_ADDRSTRLEN];
        if (inet_ntop(addr.inet6.sin6_family, &addr.inet6.sin6_addr,
                      buffer, sizeof(buffer)) == nullptr) {
          KJ_FAIL_SYSCALL("inet_ntop", errno) { break; }
          return heapString("(inet_ntop error)");
        }
        return str('[', buffer, "]:", ntohs(addr.inet6.sin6_port));
      }
      case AF_UNIX: {
        auto path = _::safeUnixPath(&addr.unixDomain, addrlen);
        if (path.size() > 0 && path[0] == '\0') {
          return str("unix-abstract:", path.slice(1, path.size()));
        } else {
          return str("unix:", path);
        }
      }
      default:
        return str("(unknown address family ", addr.generic.sa_family, ")");
    }
  }

  static Promise<Array<SocketAddress>> lookupHost(
      LowLevelAsyncIoProvider& lowLevel, kj::String host, kj::String service, uint portHint,
      _::NetworkFilter& filter);
  // Perform a DNS lookup.

  static Promise<Array<SocketAddress>> parse(
      LowLevelAsyncIoProvider& lowLevel, StringPtr str, uint portHint, _::NetworkFilter& filter) {
    // TODO(someday):  Allow commas in `str`.

    SocketAddress result;

    if (str.startsWith("unix:")) {
      StringPtr path = str.slice(strlen("unix:"));
      KJ_REQUIRE(path.size() < sizeof(addr.unixDomain.sun_path),
                 "Unix domain socket address is too long.", str);
      KJ_REQUIRE(path.size() == strlen(path.cStr()),
                 "Unix domain socket address contains NULL. Use"
                 " 'unix-abstract:' for the abstract namespace.");
      result.addr.unixDomain.sun_family = AF_UNIX;
      strcpy(result.addr.unixDomain.sun_path, path.cStr());
      result.addrlen = offsetof(struct sockaddr_un, sun_path) + path.size() + 1;

      if (!result.parseAllowedBy(filter)) {
        KJ_FAIL_REQUIRE("unix sockets blocked by restrictPeers()");
        return Array<SocketAddress>();
      }

      auto array = kj::heapArrayBuilder<SocketAddress>(1);
      array.add(result);
      return array.finish();
    }

    if (str.startsWith("unix-abstract:")) {
      StringPtr path = str.slice(strlen("unix-abstract:"));
      KJ_REQUIRE(path.size() + 1 < sizeof(addr.unixDomain.sun_path),
                 "Unix domain socket address is too long.", str);
      result.addr.unixDomain.sun_family = AF_UNIX;
      result.addr.unixDomain.sun_path[0] = '\0';
      // although not strictly required by Linux, also copy the trailing
      // NULL terminator so that we can safely read it back in toString
      memcpy(result.addr.unixDomain.sun_path + 1, path.cStr(), path.size() + 1);
      result.addrlen = offsetof(struct sockaddr_un, sun_path) + path.size() + 1;

      if (!result.parseAllowedBy(filter)) {
        KJ_FAIL_REQUIRE("abstract unix sockets blocked by restrictPeers()");
        return Array<SocketAddress>();
      }

      auto array = kj::heapArrayBuilder<SocketAddress>(1);
      array.add(result);
      return array.finish();
    }

    // Try to separate the address and port.
    ArrayPtr<const char> addrPart;
    Maybe<StringPtr> portPart;

    int af;

    if (str.startsWith("[")) {
      // Address starts with a bracket, which is a common way to write an ip6 address with a port,
      // since without brackets around the address part, the port looks like another segment of
      // the address.
      af = AF_INET6;
      size_t closeBracket = KJ_ASSERT_NONNULL(str.findLast(']'),
          "Unclosed '[' in address string.", str);

      addrPart = str.slice(1, closeBracket);
      if (str.size() > closeBracket + 1) {
        KJ_REQUIRE(str.slice(closeBracket + 1).startsWith(":"),
                   "Expected port suffix after ']'.", str);
        portPart = str.slice(closeBracket + 2);
      }
    } else {
      KJ_IF_MAYBE(colon, str.findFirst(':')) {
        if (str.slice(*colon + 1).findFirst(':') == nullptr) {
          // There is exactly one colon and no brackets, so it must be an ip4 address with port.
          af = AF_INET;
          addrPart = str.slice(0, *colon);
          portPart = str.slice(*colon + 1);
        } else {
          // There are two or more colons and no brackets, so the whole thing must be an ip6
          // address with no port.
          af = AF_INET6;
          addrPart = str;
        }
      } else {
        // No colons, so it must be an ip4 address without port.
        af = AF_INET;
        addrPart = str;
      }
    }

    // Parse the port.
    unsigned long port;
    KJ_IF_MAYBE(portText, portPart) {
      char* endptr;
      port = strtoul(portText->cStr(), &endptr, 0);
      if (portText->size() == 0 || *endptr != '\0') {
        // Not a number.  Maybe it's a service name.  Fall back to DNS.
        return lookupHost(lowLevel, kj::heapString(addrPart), kj::heapString(*portText), portHint,
                          filter);
      }
      KJ_REQUIRE(port < 65536, "Port number too large.");
    } else {
      port = portHint;
    }

    // Check for wildcard.
    if (addrPart.size() == 1 && addrPart[0] == '*') {
      result.wildcard = true;
#if defined(__OpenBSD__)
      // On OpenBSD, all sockets are either v4-only or v6-only, so use v4 as a
      // temporary workaround for wildcards.
      result.addrlen = sizeof(addr.inet4);
      result.addr.inet4.sin_family = AF_INET;
      result.addr.inet4.sin_port = htons(port);
#else
      // Create an ip6 socket and set IPV6_V6ONLY to 0 later.
      result.addrlen = sizeof(addr.inet6);
      result.addr.inet6.sin6_family = AF_INET6;
      result.addr.inet6.sin6_port = htons(port);
#endif

      auto array = kj::heapArrayBuilder<SocketAddress>(1);
      array.add(result);
      return array.finish();
    }

    void* addrTarget;
    if (af == AF_INET6) {
      result.addrlen = sizeof(addr.inet6);
      result.addr.inet6.sin6_family = AF_INET6;
      result.addr.inet6.sin6_port = htons(port);
      addrTarget = &result.addr.inet6.sin6_addr;
    } else {
      result.addrlen = sizeof(addr.inet4);
      result.addr.inet4.sin_family = AF_INET;
      result.addr.inet4.sin_port = htons(port);
      addrTarget = &result.addr.inet4.sin_addr;
    }

    if (addrPart.size() < INET6_ADDRSTRLEN - 1) {
      // addrPart is not necessarily NUL-terminated so we have to make a copy.  :(
      char buffer[INET6_ADDRSTRLEN];
      memcpy(buffer, addrPart.begin(), addrPart.size());
      buffer[addrPart.size()] = '\0';

      // OK, parse it!
      switch (inet_pton(af, buffer, addrTarget)) {
        case 1: {
          // success.
          if (!result.parseAllowedBy(filter)) {
            KJ_FAIL_REQUIRE("address family blocked by restrictPeers()");
            return Array<SocketAddress>();
          }

          auto array = kj::heapArrayBuilder<SocketAddress>(1);
          array.add(result);
          return array.finish();
        }
        case 0:
          // It's apparently not a simple address...  fall back to DNS.
          break;
        default:
          KJ_FAIL_SYSCALL("inet_pton", errno, af, addrPart);
      }
    }

    return lookupHost(lowLevel, kj::heapString(addrPart), nullptr, port, filter);
  }

  static SocketAddress getLocalAddress(int sockfd) {
    SocketAddress result;
    result.addrlen = sizeof(addr);
    KJ_SYSCALL(getsockname(sockfd, &result.addr.generic, &result.addrlen));
    return result;
  }

  bool allowedBy(LowLevelAsyncIoProvider::NetworkFilter& filter) {
    return filter.shouldAllow(&addr.generic, addrlen);
  }

  bool parseAllowedBy(_::NetworkFilter& filter) {
    return filter.shouldAllowParse(&addr.generic, addrlen);
  }

  kj::Own<PeerIdentity> getIdentity(LowLevelAsyncIoProvider& llaiop,
                                    LowLevelAsyncIoProvider::NetworkFilter& filter,
                                    AsyncIoStream& stream) const;

private:
  SocketAddress() {
    // We need to memset the whole object 0 otherwise Valgrind gets unhappy when we write it to a
    // pipe, due to the padding bytes being uninitialized.
    memset(this, 0, sizeof(*this));
  }

  socklen_t addrlen;
  bool wildcard = false;
  union {
    struct sockaddr generic;
    struct sockaddr_in inet4;
    struct sockaddr_in6 inet6;
    struct sockaddr_un unixDomain;
    struct sockaddr_storage storage;
  } addr;

  struct LookupParams;
};

struct SocketAddress::LookupParams {
  kj::String host;
  kj::String service;
};

Promise<Array<SocketAddress>> SocketAddress::lookupHost(
    LowLevelAsyncIoProvider& lowLevel, kj::String host, kj::String service, uint portHint,
    _::NetworkFilter& filter) {
  // This shitty function spawns a thread to run getaddrinfo().  Unfortunately, getaddrinfo() is
  // the only cross-platform DNS API and it is blocking.
  //
  // TODO(perf):  Use a thread pool?  Maybe kj::Thread should use a thread pool automatically?
  //   Maybe use the various platform-specific asynchronous DNS libraries?  Please do not implement
  //   a custom DNS resolver...

  auto paf = newPromiseAndCrossThreadFulfiller<Array<SocketAddress>>();
  LookupParams params = { kj::mv(host), kj::mv(service) };

  auto thread = heap<Thread>(
      [fulfiller=kj::mv(paf.fulfiller),params=kj::mv(params),portHint]() mutable {
    // getaddrinfo() can return multiple copies of the same address for several reasons.
    // A major one is that we don't give it a socket type (SOCK_STREAM vs. SOCK_DGRAM), so
    // it may return two copies of the same address, one for each type, unless it explicitly
    // knows that the service name given is specific to one type.  But we can't tell it a type,
    // because we don't actually know which one the user wants, and if we specify SOCK_STREAM
    // while the user specified a UDP service name then they'll get a resolution error which
    // is lame.  (At least, I think that's how it works.)
    //
    // So we instead resort to de-duping results.
    std::set<SocketAddress> result;

    KJ_IF_MAYBE(exception, kj::runCatchingExceptions([&]() {
      struct addrinfo hints;
      memset(&hints, 0, sizeof(hints));
      hints.ai_family = AF_UNSPEC;
#if __BIONIC__
      // AI_V4MAPPED causes getaddrinfo() to fail on Bionic libc (Android).
      hints.ai_flags = AI_ADDRCONFIG;
#else
      hints.ai_flags = AI_V4MAPPED | AI_ADDRCONFIG;
#endif
      struct addrinfo* list;
      int status = getaddrinfo(
          params.host == "*" ? nullptr : params.host.cStr(),
          params.service == nullptr ? nullptr : params.service.cStr(),
          &hints, &list);
      if (status == 0) {
        KJ_DEFER(freeaddrinfo(list));

        struct addrinfo* cur = list;
        while (cur != nullptr) {
          if (params.service == nullptr) {
            switch (cur->ai_addr->sa_family) {
              case AF_INET:
                ((struct sockaddr_in*)cur->ai_addr)->sin_port = htons(portHint);
                break;
              case AF_INET6:
                ((struct sockaddr_in6*)cur->ai_addr)->sin6_port = htons(portHint);
                break;
              default:
                break;
            }
          }

          SocketAddress addr;
          if (params.host == "*") {
            // Set up a wildcard SocketAddress.  Only use the port number returned by getaddrinfo().
            addr.wildcard = true;
            addr.addrlen = sizeof(addr.addr.inet6);
            addr.addr.inet6.sin6_family = AF_INET6;
            switch (cur->ai_addr->sa_family) {
              case AF_INET:
                addr.addr.inet6.sin6_port = ((struct sockaddr_in*)cur->ai_addr)->sin_port;
                break;
              case AF_INET6:
                addr.addr.inet6.sin6_port = ((struct sockaddr_in6*)cur->ai_addr)->sin6_port;
                break;
              default:
                addr.addr.inet6.sin6_port = portHint;
                break;
            }
          } else {
            addr.addrlen = cur->ai_addrlen;
            memcpy(&addr.addr.generic, cur->ai_addr, cur->ai_addrlen);
          }
          result.insert(addr);
          cur = cur->ai_next;
        }
      } else if (status == EAI_SYSTEM) {
        KJ_FAIL_SYSCALL("getaddrinfo", errno, params.host, params.service) {
          return;
        }
      } else {
        KJ_FAIL_REQUIRE("DNS lookup failed.",
                        params.host, params.service, gai_strerror(status)) {
          return;
        }
      }
    })) {
      fulfiller->reject(kj::mv(*exception));
    } else {
      fulfiller->fulfill(KJ_MAP(addr, result) { return addr; });
    }
  });

  return kj::mv(paf.promise);
}

// =======================================================================================

class FdConnectionReceiver final: public ConnectionReceiver, public OwnedFileDescriptor {
public:
  FdConnectionReceiver(LowLevelAsyncIoProvider& lowLevel,
                       UnixEventPort& eventPort, int fd,
                       LowLevelAsyncIoProvider::NetworkFilter& filter, uint flags)
      : OwnedFileDescriptor(fd, flags), lowLevel(lowLevel), eventPort(eventPort), filter(filter),
        observer(eventPort, fd, UnixEventPort::FdObserver::OBSERVE_READ) {}

  Promise<Own<AsyncIoStream>> accept() override {
    return acceptImpl(false).then([](AuthenticatedStream&& a) { return kj::mv(a.stream); });
  }

  Promise<AuthenticatedStream> acceptAuthenticated() override {
    return acceptImpl(true);
  }

  Promise<AuthenticatedStream> acceptImpl(bool authenticated) {
    int newFd;

    struct sockaddr_storage addr;
    socklen_t addrlen = sizeof(addr);

  retry:
#if __linux__ && !__BIONIC__
    newFd = ::accept4(fd, reinterpret_cast<struct sockaddr*>(&addr), &addrlen,
                      SOCK_NONBLOCK | SOCK_CLOEXEC);
#else
    newFd = ::accept(fd, reinterpret_cast<struct sockaddr*>(&addr), &addrlen);
#endif

    if (newFd >= 0) {
      kj::AutoCloseFd ownFd(newFd);
      if (!filter.shouldAllow(reinterpret_cast<struct sockaddr*>(&addr), addrlen)) {
        // Ignore disallowed address.
        return acceptImpl(authenticated);
      } else {
        // TODO(perf):  As a hack for the 0.4 release we are always setting
        //   TCP_NODELAY because Nagle's algorithm pretty much kills Cap'n Proto's
        //   RPC protocol.  Later, we should extend the interface to provide more
        //   control over this.  Perhaps write() should have a flag which
        //   specifies whether to pass MSG_MORE.
        int one = 1;
        KJ_SYSCALL_HANDLE_ERRORS(::setsockopt(
              ownFd.get(), IPPROTO_TCP, TCP_NODELAY, (char*)&one, sizeof(one))) {
          case EOPNOTSUPP:
          case ENOPROTOOPT: // (returned for AF_UNIX in cygwin)
#if __FreeBSD__
          case EINVAL: // (returned for AF_UNIX in FreeBSD)
#endif
            break;
          default:
            KJ_FAIL_SYSCALL("setsocketopt(IPPROTO_TCP, TCP_NODELAY)", error);
        }

        AuthenticatedStream result;
        result.stream = heap<AsyncStreamFd>(eventPort, ownFd.release(), NEW_FD_FLAGS,
                                            UnixEventPort::FdObserver::OBSERVE_READ_WRITE);
        if (authenticated) {
          result.peerIdentity = SocketAddress(reinterpret_cast<struct sockaddr*>(&addr), addrlen)
              .getIdentity(lowLevel, filter, *result.stream);
        }
        return kj::mv(result);
      }
    } else {
      int error = errno;

      switch (error) {
        case EAGAIN:
#if EAGAIN != EWOULDBLOCK
        case EWOULDBLOCK:
#endif
          // Not ready yet.
          return observer.whenBecomesReadable().then([this,authenticated]() {
            return acceptImpl(authenticated);
          });

        case EINTR:
        case ENETDOWN:
#ifdef EPROTO
        // EPROTO is not defined on OpenBSD.
        case EPROTO:
#endif
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
    return SocketAddress::getLocalAddress(fd).getPort();
  }

  void getsockopt(int level, int option, void* value, uint* length) override {
    socklen_t socklen = *length;
    KJ_SYSCALL(::getsockopt(fd, level, option, value, &socklen));
    *length = socklen;
  }
  void setsockopt(int level, int option, const void* value, uint length) override {
    KJ_SYSCALL(::setsockopt(fd, level, option, value, length));
  }
  void getsockname(struct sockaddr* addr, uint* length) override {
    socklen_t socklen = *length;
    KJ_SYSCALL(::getsockname(fd, addr, &socklen));
    *length = socklen;
  }

public:
  LowLevelAsyncIoProvider& lowLevel;
  UnixEventPort& eventPort;
  LowLevelAsyncIoProvider::NetworkFilter& filter;
  UnixEventPort::FdObserver observer;
};

class DatagramPortImpl final: public DatagramPort, public OwnedFileDescriptor {
public:
  DatagramPortImpl(LowLevelAsyncIoProvider& lowLevel, UnixEventPort& eventPort, int fd,
                   LowLevelAsyncIoProvider::NetworkFilter& filter, uint flags)
      : OwnedFileDescriptor(fd, flags), lowLevel(lowLevel), eventPort(eventPort), filter(filter),
        observer(eventPort, fd, UnixEventPort::FdObserver::OBSERVE_READ |
                                UnixEventPort::FdObserver::OBSERVE_WRITE) {}

  Promise<size_t> send(const void* buffer, size_t size, NetworkAddress& destination) override;
  Promise<size_t> send(
      ArrayPtr<const ArrayPtr<const byte>> pieces, NetworkAddress& destination) override;

  class ReceiverImpl;

  Own<DatagramReceiver> makeReceiver(DatagramReceiver::Capacity capacity) override;

  uint getPort() override {
    return SocketAddress::getLocalAddress(fd).getPort();
  }

  void getsockopt(int level, int option, void* value, uint* length) override {
    socklen_t socklen = *length;
    KJ_SYSCALL(::getsockopt(fd, level, option, value, &socklen));
    *length = socklen;
  }
  void setsockopt(int level, int option, const void* value, uint length) override {
    KJ_SYSCALL(::setsockopt(fd, level, option, value, length));
  }

public:
  LowLevelAsyncIoProvider& lowLevel;
  UnixEventPort& eventPort;
  LowLevelAsyncIoProvider::NetworkFilter& filter;
  UnixEventPort::FdObserver observer;
};

class LowLevelAsyncIoProviderImpl final: public LowLevelAsyncIoProvider {
public:
  LowLevelAsyncIoProviderImpl()
      : eventPort(), eventLoop(eventPort), waitScope(eventLoop) {}

  inline WaitScope& getWaitScope() { return waitScope; }

  Own<AsyncInputStream> wrapInputFd(int fd, uint flags = 0) override {
    return heap<AsyncStreamFd>(eventPort, fd, flags, UnixEventPort::FdObserver::OBSERVE_READ);
  }
  Own<AsyncOutputStream> wrapOutputFd(int fd, uint flags = 0) override {
    return heap<AsyncStreamFd>(eventPort, fd, flags, UnixEventPort::FdObserver::OBSERVE_WRITE);
  }
  Own<AsyncIoStream> wrapSocketFd(int fd, uint flags = 0) override {
    return heap<AsyncStreamFd>(eventPort, fd, flags, UnixEventPort::FdObserver::OBSERVE_READ_WRITE);
  }
  Own<AsyncCapabilityStream> wrapUnixSocketFd(Fd fd, uint flags = 0) override {
    return heap<AsyncStreamFd>(eventPort, fd, flags, UnixEventPort::FdObserver::OBSERVE_READ_WRITE);
  }
  Promise<Own<AsyncIoStream>> wrapConnectingSocketFd(
      int fd, const struct sockaddr* addr, uint addrlen, uint flags = 0) override {
    // It's important that we construct the AsyncStreamFd first, so that `flags` are honored,
    // especially setting nonblocking mode and taking ownership.
    auto result = heap<AsyncStreamFd>(eventPort, fd, flags,
                                      UnixEventPort::FdObserver::OBSERVE_READ_WRITE);

    // Unfortunately connect() doesn't fit the mold of KJ_NONBLOCKING_SYSCALL, since it indicates
    // non-blocking using EINPROGRESS.
    for (;;) {
      if (::connect(fd, addr, addrlen) < 0) {
        int error = errno;
        if (error == EINPROGRESS) {
          // Fine.
          break;
        } else if (error != EINTR) {
          auto address = SocketAddress(addr, addrlen).toString();
          KJ_FAIL_SYSCALL("connect()", error, address) { break; }
          return Own<AsyncIoStream>();
        }
      } else {
        // no error
        break;
      }
    }

    auto connected = result->waitConnected();
    return connected.then([fd,stream=kj::mv(result)]() mutable -> Own<AsyncIoStream> {
      int err;
      socklen_t errlen = sizeof(err);
      KJ_SYSCALL(getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &errlen));
      if (err != 0) {
        KJ_FAIL_SYSCALL("connect()", err) { break; }
      }
      return kj::mv(stream);
    });
  }
  Own<ConnectionReceiver> wrapListenSocketFd(
      int fd, NetworkFilter& filter, uint flags = 0) override {
    return heap<FdConnectionReceiver>(*this, eventPort, fd, filter, flags);
  }
  Own<DatagramPort> wrapDatagramSocketFd(
      int fd, NetworkFilter& filter, uint flags = 0) override {
    return heap<DatagramPortImpl>(*this, eventPort, fd, filter, flags);
  }

  Timer& getTimer() override { return eventPort.getTimer(); }

  UnixEventPort& getEventPort() { return eventPort; }

private:
  UnixEventPort eventPort;
  EventLoop eventLoop;
  WaitScope waitScope;
};

// =======================================================================================

class NetworkAddressImpl final: public NetworkAddress {
public:
  NetworkAddressImpl(LowLevelAsyncIoProvider& lowLevel,
                     LowLevelAsyncIoProvider::NetworkFilter& filter,
                     Array<SocketAddress> addrs)
      : lowLevel(lowLevel), filter(filter), addrs(kj::mv(addrs)) {}

  Promise<Own<AsyncIoStream>> connect() override {
    auto addrsCopy = heapArray(addrs.asPtr());
    auto promise = connectImpl(lowLevel, filter, addrsCopy, false);
    return promise.attach(kj::mv(addrsCopy))
        .then([](AuthenticatedStream&& a) { return kj::mv(a.stream); });
  }

  Promise<AuthenticatedStream> connectAuthenticated() override {
    auto addrsCopy = heapArray(addrs.asPtr());
    auto promise = connectImpl(lowLevel, filter, addrsCopy, true);
    return promise.attach(kj::mv(addrsCopy));
  }

  Own<ConnectionReceiver> listen() override {
    auto makeReceiver = [&](SocketAddress& addr) {
      int fd = addr.socket(SOCK_STREAM);

      {
        KJ_ON_SCOPE_FAILURE(close(fd));

        // We always enable SO_REUSEADDR because having to take your server down for five minutes
        // before it can restart really sucks.
        int optval = 1;
        KJ_SYSCALL(setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)));

        addr.bind(fd);

        // TODO(someday):  Let queue size be specified explicitly in string addresses.
        KJ_SYSCALL(::listen(fd, SOMAXCONN));
      }

      return lowLevel.wrapListenSocketFd(fd, filter, NEW_FD_FLAGS);
    };

    if (addrs.size() == 1) {
      return makeReceiver(addrs[0]);
    } else {
      return newAggregateConnectionReceiver(KJ_MAP(addr, addrs) { return makeReceiver(addr); });
    }
  }

  Own<DatagramPort> bindDatagramPort() override {
    if (addrs.size() > 1) {
      KJ_LOG(WARNING, "Bind address resolved to multiple addresses.  Only the first address will "
          "be used.  If this is incorrect, specify the address numerically.  This may be fixed "
          "in the future.", addrs[0].toString());
    }

    int fd = addrs[0].socket(SOCK_DGRAM);

    {
      KJ_ON_SCOPE_FAILURE(close(fd));

      // We always enable SO_REUSEADDR because having to take your server down for five minutes
      // before it can restart really sucks.
      int optval = 1;
      KJ_SYSCALL(setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)));

      addrs[0].bind(fd);
    }

    return lowLevel.wrapDatagramSocketFd(fd, filter, NEW_FD_FLAGS);
  }

  Own<NetworkAddress> clone() override {
    return kj::heap<NetworkAddressImpl>(lowLevel, filter, kj::heapArray(addrs.asPtr()));
  }

  String toString() override {
    return strArray(KJ_MAP(addr, addrs) { return addr.toString(); }, ",");
  }

  const SocketAddress& chooseOneAddress() {
    KJ_REQUIRE(addrs.size() > 0, "No addresses available.");
    return addrs[counter++ % addrs.size()];
  }

private:
  LowLevelAsyncIoProvider& lowLevel;
  LowLevelAsyncIoProvider::NetworkFilter& filter;
  Array<SocketAddress> addrs;
  uint counter = 0;

  static Promise<AuthenticatedStream> connectImpl(
      LowLevelAsyncIoProvider& lowLevel,
      LowLevelAsyncIoProvider::NetworkFilter& filter,
      ArrayPtr<SocketAddress> addrs,
      bool authenticated) {
    KJ_ASSERT(addrs.size() > 0);

    return kj::evalNow([&]() -> Promise<Own<AsyncIoStream>> {
      if (!addrs[0].allowedBy(filter)) {
        return KJ_EXCEPTION(FAILED, "connect() blocked by restrictPeers()");
      } else {
        int fd = addrs[0].socket(SOCK_STREAM);
        return lowLevel.wrapConnectingSocketFd(
            fd, addrs[0].getRaw(), addrs[0].getRawSize(), NEW_FD_FLAGS);
      }
    }).then([&lowLevel,&filter,addrs,authenticated](Own<AsyncIoStream>&& stream)
        -> Promise<AuthenticatedStream> {
      // Success, pass along.
      AuthenticatedStream result;
      result.stream = kj::mv(stream);
      if (authenticated) {
        result.peerIdentity = addrs[0].getIdentity(lowLevel, filter, *result.stream);
      }
      return kj::mv(result);
    }, [&lowLevel,&filter,addrs,authenticated](Exception&& exception) mutable
        -> Promise<AuthenticatedStream> {
      // Connect failed.
      if (addrs.size() > 1) {
        // Try the next address instead.
        return connectImpl(lowLevel, filter, addrs.slice(1, addrs.size()), authenticated);
      } else {
        // No more addresses to try, so propagate the exception.
        return kj::mv(exception);
      }
    });
  }
};

kj::Own<PeerIdentity> SocketAddress::getIdentity(kj::LowLevelAsyncIoProvider& llaiop,
                                                 LowLevelAsyncIoProvider::NetworkFilter& filter,
                                                 AsyncIoStream& stream) const {
  switch (addr.generic.sa_family) {
    case AF_INET:
    case AF_INET6: {
      auto builder = kj::heapArrayBuilder<SocketAddress>(1);
      builder.add(*this);
      return NetworkPeerIdentity::newInstance(
          kj::heap<NetworkAddressImpl>(llaiop, filter, builder.finish()));
    }
    case AF_UNIX: {
      LocalPeerIdentity::Credentials result;

      // There is little documentation on what happens when the uid/pid can't be obtained, but I've
      // seen vague references on the internet saying that a PID of 0 and a UID of uid_t(-1) are used
      // as invalid values.

// OpenBSD defines SO_PEERCRED but uses a different interface for it
// hence we're falling back to LOCAL_PEERCRED
#if defined(SO_PEERCRED) && !__OpenBSD__
      struct ucred creds;
      uint length = sizeof(creds);
      stream.getsockopt(SOL_SOCKET, SO_PEERCRED, &creds, &length);
      if (creds.pid > 0) {
        result.pid = creds.pid;
      }
      if (creds.uid != static_cast<uid_t>(-1)) {
        result.uid = creds.uid;
      }

#elif defined(LOCAL_PEERCRED)
      // MacOS / FreeBSD / OpenBSD
      struct xucred creds;
      uint length = sizeof(creds);
      stream.getsockopt(SOL_LOCAL, LOCAL_PEERCRED, &creds, &length);
      KJ_ASSERT(length == sizeof(creds));
      if (creds.cr_uid != static_cast<uid_t>(-1)) {
        result.uid = creds.cr_uid;
      }

#if defined(LOCAL_PEERPID)
      // MacOS only?
      pid_t pid;
      length = sizeof(pid);
      stream.getsockopt(SOL_LOCAL, LOCAL_PEERPID, &pid, &length);
      KJ_ASSERT(length == sizeof(pid));
      if (pid > 0) {
        result.pid = pid;
      }
#endif
#endif

      return LocalPeerIdentity::newInstance(result);
    }
    default:
      return UnknownPeerIdentity::newInstance();
  }
}

class SocketNetwork final: public Network {
public:
  explicit SocketNetwork(LowLevelAsyncIoProvider& lowLevel): lowLevel(lowLevel) {}
  explicit SocketNetwork(SocketNetwork& parent,
                         kj::ArrayPtr<const kj::StringPtr> allow,
                         kj::ArrayPtr<const kj::StringPtr> deny)
      : lowLevel(parent.lowLevel), filter(allow, deny, parent.filter) {}

  Promise<Own<NetworkAddress>> parseAddress(StringPtr addr, uint portHint = 0) override {
    return evalNow([&]() {
      return SocketAddress::parse(lowLevel, addr, portHint, filter);
    }).then([this](Array<SocketAddress> addresses) -> Own<NetworkAddress> {
      return heap<NetworkAddressImpl>(lowLevel, filter, kj::mv(addresses));
    });
  }

  Own<NetworkAddress> getSockaddr(const void* sockaddr, uint len) override {
    auto array = kj::heapArrayBuilder<SocketAddress>(1);
    array.add(SocketAddress(sockaddr, len));
    KJ_REQUIRE(array[0].allowedBy(filter), "address blocked by restrictPeers()") { break; }
    return Own<NetworkAddress>(heap<NetworkAddressImpl>(lowLevel, filter, array.finish()));
  }

  Own<Network> restrictPeers(
      kj::ArrayPtr<const kj::StringPtr> allow,
      kj::ArrayPtr<const kj::StringPtr> deny = nullptr) override {
    return heap<SocketNetwork>(*this, allow, deny);
  }

private:
  LowLevelAsyncIoProvider& lowLevel;
  _::NetworkFilter filter;
};

// =======================================================================================

Promise<size_t> DatagramPortImpl::send(
    const void* buffer, size_t size, NetworkAddress& destination) {
  auto& addr = downcast<NetworkAddressImpl>(destination).chooseOneAddress();

  ssize_t n;
  KJ_NONBLOCKING_SYSCALL(n = sendto(fd, buffer, size, 0, addr.getRaw(), addr.getRawSize()));
  if (n < 0) {
    // Write buffer full.
    return observer.whenBecomesWritable().then([this, buffer, size, &destination]() {
      return send(buffer, size, destination);
    });
  } else {
    // If less than the whole message was sent, then it got truncated, and there's nothing we can
    // do about it.
    return n;
  }
}

Promise<size_t> DatagramPortImpl::send(
    ArrayPtr<const ArrayPtr<const byte>> pieces, NetworkAddress& destination) {
  struct msghdr msg;
  memset(&msg, 0, sizeof(msg));

  auto& addr = downcast<NetworkAddressImpl>(destination).chooseOneAddress();
  msg.msg_name = const_cast<void*>(implicitCast<const void*>(addr.getRaw()));
  msg.msg_namelen = addr.getRawSize();

  const size_t iovmax = kj::miniposix::iovMax();
  KJ_STACK_ARRAY(struct iovec, iov, kj::min(pieces.size(), iovmax), 16, 64);

  for (size_t i: kj::indices(pieces)) {
    iov[i].iov_base = const_cast<void*>(implicitCast<const void*>(pieces[i].begin()));
    iov[i].iov_len = pieces[i].size();
  }

  Array<byte> extra;
  if (pieces.size() > iovmax) {
    // Too many pieces, but we can't use multiple syscalls because they'd send separate
    // datagrams. We'll have to copy the trailing pieces into a temporary array.
    //
    // TODO(perf): On Linux we could use multiple syscalls via MSG_MORE or sendmsg/sendmmsg.
    size_t extraSize = 0;
    for (size_t i = iovmax - 1; i < pieces.size(); i++) {
      extraSize += pieces[i].size();
    }
    extra = kj::heapArray<byte>(extraSize);
    extraSize = 0;
    for (size_t i = iovmax - 1; i < pieces.size(); i++) {
      memcpy(extra.begin() + extraSize, pieces[i].begin(), pieces[i].size());
      extraSize += pieces[i].size();
    }
    iov.back().iov_base = extra.begin();
    iov.back().iov_len = extra.size();
  }

  msg.msg_iov = iov.begin();
  msg.msg_iovlen = iov.size();

  ssize_t n;
  KJ_NONBLOCKING_SYSCALL(n = sendmsg(fd, &msg, 0));
  if (n < 0) {
    // Write buffer full.
    return observer.whenBecomesWritable().then([this, pieces, &destination]() {
      return send(pieces, destination);
    });
  } else {
    // If less than the whole message was sent, then it was truncated, and there's nothing we can
    // do about that now.
    return n;
  }
}

class DatagramPortImpl::ReceiverImpl final: public DatagramReceiver {
public:
  explicit ReceiverImpl(DatagramPortImpl& port, Capacity capacity)
      : port(port),
        contentBuffer(heapArray<byte>(capacity.content)),
        ancillaryBuffer(capacity.ancillary > 0 ? heapArray<byte>(capacity.ancillary)
                                               : Array<byte>(nullptr)) {}

  Promise<void> receive() override {
    struct msghdr msg;
    memset(&msg, 0, sizeof(msg));

    struct sockaddr_storage addr;
    memset(&addr, 0, sizeof(addr));
    msg.msg_name = &addr;
    msg.msg_namelen = sizeof(addr);

    struct iovec iov;
    iov.iov_base = contentBuffer.begin();
    iov.iov_len = contentBuffer.size();
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = ancillaryBuffer.begin();
    msg.msg_controllen = ancillaryBuffer.size();

    ssize_t n;
    KJ_NONBLOCKING_SYSCALL(n = recvmsg(port.fd, &msg, 0));

    if (n < 0) {
      // No data available. Wait.
      return port.observer.whenBecomesReadable().then([this]() {
        return receive();
      });
    } else {
      if (!port.filter.shouldAllow(reinterpret_cast<const struct sockaddr*>(msg.msg_name),
                                   msg.msg_namelen)) {
        // Ignore message from disallowed source.
        return receive();
      }

      receivedSize = n;
      contentTruncated = msg.msg_flags & MSG_TRUNC;

      source.emplace(port.lowLevel, port.filter, msg.msg_name, msg.msg_namelen);

      ancillaryList.resize(0);
      ancillaryTruncated = msg.msg_flags & MSG_CTRUNC;

      for (struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg); cmsg != nullptr;
           cmsg = CMSG_NXTHDR(&msg, cmsg)) {
        // On some platforms (OSX), a cmsghdr's length may cross the end of the ancillary buffer
        // when truncated. On other platforms (Linux) the length in cmsghdr will itself be
        // truncated to fit within the buffer.

#if __APPLE__
// On MacOS, `CMSG_SPACE(0)` triggers a bogus warning.
#pragma GCC diagnostic ignored "-Wnull-pointer-arithmetic"
#endif
        const byte* pos = reinterpret_cast<const byte*>(cmsg);
        size_t available = ancillaryBuffer.end() - pos;
        if (available < CMSG_SPACE(0)) {
          // The buffer ends in the middle of the header. We can't use this message.
          // (On Linux, this never happens, because the message is not included if there isn't
          // space for a header. I'm not sure how other systems behave, though, so let's be safe.)
          break;
        }

        // OK, we know the cmsghdr is valid, at least.

        // Find the start of the message payload.
        const byte* begin = (const byte *)CMSG_DATA(cmsg);

        // Cap the message length to the available space.
        const byte* end = pos + kj::min(available, cmsg->cmsg_len);

        ancillaryList.add(AncillaryMessage(
            cmsg->cmsg_level, cmsg->cmsg_type, arrayPtr(begin, end)));
      }

      return READY_NOW;
    }
  }

  MaybeTruncated<ArrayPtr<const byte>> getContent() override {
    return { contentBuffer.slice(0, receivedSize), contentTruncated };
  }

  MaybeTruncated<ArrayPtr<const AncillaryMessage>> getAncillary() override {
    return { ancillaryList.asPtr(), ancillaryTruncated };
  }

  NetworkAddress& getSource() override {
    return KJ_REQUIRE_NONNULL(source, "Haven't sent a message yet.").abstract;
  }

private:
  DatagramPortImpl& port;
  Array<byte> contentBuffer;
  Array<byte> ancillaryBuffer;
  Vector<AncillaryMessage> ancillaryList;
  size_t receivedSize = 0;
  bool contentTruncated = false;
  bool ancillaryTruncated = false;

  struct StoredAddress {
    StoredAddress(LowLevelAsyncIoProvider& lowLevel, LowLevelAsyncIoProvider::NetworkFilter& filter,
                  const void* sockaddr, uint length)
        : raw(sockaddr, length),
          abstract(lowLevel, filter, Array<SocketAddress>(&raw, 1, NullArrayDisposer::instance)) {}

    SocketAddress raw;
    NetworkAddressImpl abstract;
  };

  kj::Maybe<StoredAddress> source;
};

Own<DatagramReceiver> DatagramPortImpl::makeReceiver(DatagramReceiver::Capacity capacity) {
  return kj::heap<ReceiverImpl>(*this, capacity);
}

// =======================================================================================

class AsyncIoProviderImpl final: public AsyncIoProvider {
public:
  AsyncIoProviderImpl(LowLevelAsyncIoProvider& lowLevel)
      : lowLevel(lowLevel), network(lowLevel) {}

  OneWayPipe newOneWayPipe() override {
    int fds[2];
#if __linux__ && !__BIONIC__
    KJ_SYSCALL(pipe2(fds, O_NONBLOCK | O_CLOEXEC));
#else
    KJ_SYSCALL(pipe(fds));
#endif
    return OneWayPipe {
      lowLevel.wrapInputFd(fds[0], NEW_FD_FLAGS),
      lowLevel.wrapOutputFd(fds[1], NEW_FD_FLAGS)
    };
  }

  TwoWayPipe newTwoWayPipe() override {
    int fds[2];
    int type = SOCK_STREAM;
#if __linux__ && !__BIONIC__
    type |= SOCK_NONBLOCK | SOCK_CLOEXEC;
#endif
    KJ_SYSCALL(socketpair(AF_UNIX, type, 0, fds));
    return TwoWayPipe { {
      lowLevel.wrapSocketFd(fds[0], NEW_FD_FLAGS),
      lowLevel.wrapSocketFd(fds[1], NEW_FD_FLAGS)
    } };
  }

  CapabilityPipe newCapabilityPipe() override {
    int fds[2];
    int type = SOCK_STREAM;
#if __linux__ && !__BIONIC__
    type |= SOCK_NONBLOCK | SOCK_CLOEXEC;
#endif
    KJ_SYSCALL(socketpair(AF_UNIX, type, 0, fds));
    return CapabilityPipe { {
      lowLevel.wrapUnixSocketFd(fds[0], NEW_FD_FLAGS),
      lowLevel.wrapUnixSocketFd(fds[1], NEW_FD_FLAGS)
    } };
  }

  Network& getNetwork() override {
    return network;
  }

  PipeThread newPipeThread(
      Function<void(AsyncIoProvider&, AsyncIoStream&, WaitScope&)> startFunc) override {
    int fds[2];
    int type = SOCK_STREAM;
#if __linux__ && !__BIONIC__
    type |= SOCK_NONBLOCK | SOCK_CLOEXEC;
#endif
    KJ_SYSCALL(socketpair(AF_UNIX, type, 0, fds));

    int threadFd = fds[1];
    KJ_ON_SCOPE_FAILURE(close(threadFd));

    auto pipe = lowLevel.wrapSocketFd(fds[0], NEW_FD_FLAGS);

    auto thread = heap<Thread>([threadFd,startFunc=kj::mv(startFunc)]() mutable {
      LowLevelAsyncIoProviderImpl lowLevel;
      auto stream = lowLevel.wrapSocketFd(threadFd, NEW_FD_FLAGS);
      AsyncIoProviderImpl ioProvider(lowLevel);
      startFunc(ioProvider, *stream, lowLevel.getWaitScope());
    });

    return { kj::mv(thread), kj::mv(pipe) };
  }

  Timer& getTimer() override { return lowLevel.getTimer(); }

private:
  LowLevelAsyncIoProvider& lowLevel;
  SocketNetwork network;
};

}  // namespace

Own<AsyncIoProvider> newAsyncIoProvider(LowLevelAsyncIoProvider& lowLevel) {
  return kj::heap<AsyncIoProviderImpl>(lowLevel);
}

AsyncIoContext setupAsyncIo() {
  auto lowLevel = heap<LowLevelAsyncIoProviderImpl>();
  auto ioProvider = kj::heap<AsyncIoProviderImpl>(*lowLevel);
  auto& waitScope = lowLevel->getWaitScope();
  auto& eventPort = lowLevel->getEventPort();
  return { kj::mv(lowLevel), kj::mv(ioProvider), waitScope, eventPort };
}

}  // namespace kj

#endif  // !_WIN32
