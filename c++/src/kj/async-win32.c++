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

#if _WIN32

// Request Vista-level APIs.
#include <kj/win32-api-version.h>

#include "async-win32.h"
#include "debug.h"
#include <atomic>
#include "time.h"
#include "refcount.h"
#include <ntsecapi.h>  // NTSTATUS
#include <ntstatus.h>  // STATUS_SUCCESS

#undef ERROR  // dammit windows.h

namespace kj {

Win32IocpEventPort::Win32IocpEventPort()
    : clock(systemPreciseMonotonicClock()),
      iocp(newIocpHandle()), thread(openCurrentThread()), timerImpl(clock.now()) {}

Win32IocpEventPort::~Win32IocpEventPort() noexcept(false) {}

class Win32IocpEventPort::IoPromiseAdapter final: public OVERLAPPED {
public:
  IoPromiseAdapter(PromiseFulfiller<IoResult>& fulfiller, Win32IocpEventPort& port,
                   uint64_t offset, IoPromiseAdapter** selfPtr)
      : fulfiller(fulfiller), port(port) {
    *selfPtr = this;

    memset(implicitCast<OVERLAPPED*>(this), 0, sizeof(OVERLAPPED));
    this->Offset = offset & 0x00000000FFFFFFFFull;
    this->OffsetHigh = offset >> 32;
  }

  ~IoPromiseAdapter() {
    if (handle != INVALID_HANDLE_VALUE) {
      // Need to cancel the I/O.
      //
      // Note: Even if HasOverlappedIoCompleted(this) is true, CancelIoEx() still seems needed to
      //   force the completion event.
      if (!CancelIoEx(handle, this)) {
        DWORD error = GetLastError();

        // ERROR_NOT_FOUND probably means the operation already completed and is enqueued on the
        // IOCP.
        //
        // ERROR_INVALID_HANDLE probably means that, amid a mass of destructors, the HANDLE was
        // closed before all of the I/O promises were destroyed. We tolerate this so long as the
        // I/O promises are also destroyed before returning to the event loop, hence the I/O
        // tasks won't actually continue on a dead handle.
        //
        // TODO(cleanup): ERROR_INVALID_HANDLE really shouldn't be allowed. Unfortunately, the
        //   refcounted nature of capabilities and the RPC system seems to mean that objects
        //   are unwound in the wrong order in several of Cap'n Proto's tests. So we live with this
        //   for now. Note that even if a new handle is opened with the same numeric value, it
        //   should be hardless to call CancelIoEx() on it because it couldn't possibly be using
        //   the same OVERLAPPED structure.
        if (error != ERROR_NOT_FOUND && error != ERROR_INVALID_HANDLE) {
          KJ_FAIL_WIN32("CancelIoEx()", error, handle);
        }
      }

      // We have to wait for the IOCP to poop out the event, so that we can safely destroy the
      // OVERLAPPED.
      while (handle != INVALID_HANDLE_VALUE) {
        port.waitIocp(INFINITE);
      }
    }
  }

  void start(HANDLE handle) {
    KJ_ASSERT(this->handle == INVALID_HANDLE_VALUE);
    this->handle = handle;
  }

  void done(IoResult result) {
    KJ_ASSERT(handle != INVALID_HANDLE_VALUE);
    handle = INVALID_HANDLE_VALUE;
    fulfiller.fulfill(kj::mv(result));
  }

private:
  PromiseFulfiller<IoResult>& fulfiller;
  Win32IocpEventPort& port;

  HANDLE handle = INVALID_HANDLE_VALUE;
  // If an I/O operation is currently enqueued, the handle on which it is enqueued.
};

class Win32IocpEventPort::IoOperationImpl final: public Win32EventPort::IoOperation {
public:
  explicit IoOperationImpl(Win32IocpEventPort& port, HANDLE handle, uint64_t offset)
      : handle(handle),
        promise(newAdaptedPromise<IoResult, IoPromiseAdapter>(port, offset, &promiseAdapter)) {}

  LPOVERLAPPED getOverlapped() override {
    KJ_REQUIRE(promiseAdapter != nullptr, "already called onComplete()");
    return promiseAdapter;
  }

  Promise<IoResult> onComplete() override {
    KJ_REQUIRE(promiseAdapter != nullptr, "can only call onComplete() once");
    promiseAdapter->start(handle);
    promiseAdapter = nullptr;
    return kj::mv(promise);
  }

private:
  HANDLE handle;
  IoPromiseAdapter* promiseAdapter;
  Promise<IoResult> promise;
};

class Win32IocpEventPort::IoObserverImpl final: public Win32EventPort::IoObserver {
public:
  IoObserverImpl(Win32IocpEventPort& port, HANDLE handle)
      : port(port), handle(handle) {
    KJ_WIN32(CreateIoCompletionPort(handle, port.iocp, 0, 1), handle, port.iocp.get());
  }

  Own<IoOperation> newOperation(uint64_t offset) {
    return heap<IoOperationImpl>(port, handle, offset);
  }

private:
  Win32IocpEventPort& port;
  HANDLE handle;
};

Own<Win32EventPort::IoObserver> Win32IocpEventPort::observeIo(HANDLE handle) {
  return heap<IoObserverImpl>(*this, handle);
}

Own<Win32EventPort::SignalObserver> Win32IocpEventPort::observeSignalState(HANDLE handle) {
  return waitThreads.observeSignalState(handle);
}

bool Win32IocpEventPort::wait() {
  // It's possible that a wake event was received and discarded during ~IoPromiseAdapter. We
  // need to check for that now. Otherwise, calling waitIocp may cause it to hang forever.
  if (receivedWake()) {
    return true;
  }
  waitIocp(timerImpl.timeoutToNextEvent(clock.now(), MILLISECONDS, INFINITE - 1)
      .map([](uint64_t t) -> DWORD { return t; })
      .orDefault(INFINITE));

  timerImpl.advanceTo(clock.now());

  return receivedWake();
}

bool Win32IocpEventPort::poll() {
  waitIocp(0);

  return receivedWake();
}

void Win32IocpEventPort::wake() const {
  if (!sentWake.load(std::memory_order_acquire)) {
    sentWake.store(true, std::memory_order_release);
    KJ_WIN32(PostQueuedCompletionStatus(iocp, 0, 0, nullptr));
  }
}

void Win32IocpEventPort::waitIocp(DWORD timeoutMs) {
  if (isAllowApc) {
    ULONG countReceived = 0;
    OVERLAPPED_ENTRY entry;
    memset(&entry, 0, sizeof(entry));

    if (GetQueuedCompletionStatusEx(iocp, &entry, 1, &countReceived, timeoutMs, TRUE)) {
      KJ_ASSERT(countReceived == 1);

      if (entry.lpOverlapped == nullptr) {
        // wake() called in another thread, or APC queued.
      } else {
        DWORD error = ERROR_SUCCESS;
        if (entry.lpOverlapped->Internal != STATUS_SUCCESS) {
          error = LsaNtStatusToWinError(entry.lpOverlapped->Internal);
        }
        static_cast<IoPromiseAdapter*>(entry.lpOverlapped)
            ->done(IoResult { error, entry.dwNumberOfBytesTransferred });
      }
    } else {
      // Call failed.
      DWORD error = GetLastError();
      if (error == WAIT_TIMEOUT || error == WAIT_IO_COMPLETION) {
        // WAIT_TIMEOUT = timed out (dunno why this isn't ERROR_TIMEOUT??)
        // WAIT_IO_COMPLETION = APC queued
        // Either way, nothing to do.
        return;
      } else {
        KJ_FAIL_WIN32("GetQueuedCompletionStatusEx()", error, error, entry.lpOverlapped);
      }
    }
  } else {
    DWORD bytesTransferred;
    ULONG_PTR completionKey;
    LPOVERLAPPED overlapped = nullptr;

    BOOL success = GetQueuedCompletionStatus(
        iocp, &bytesTransferred, &completionKey, &overlapped, timeoutMs);

    if (overlapped == nullptr) {
      if (success) {
        // wake() called in another thread.
      } else {
        DWORD error = GetLastError();
        if (error == WAIT_TIMEOUT) {
          // Great, nothing to do. (Why this is WAIT_TIMEOUT and not ERROR_TIMEOUT I'm not sure.)
        } else {
          KJ_FAIL_WIN32("GetQueuedCompletionStatus()", error, error, overlapped);
        }
      }
    } else {
      DWORD error = success ? ERROR_SUCCESS : GetLastError();
      static_cast<IoPromiseAdapter*>(overlapped)->done(IoResult { error, bytesTransferred });
    }
  }
}

bool Win32IocpEventPort::receivedWake() {
  if (sentWake.load(std::memory_order_acquire)) {
    sentWake.store(false, std::memory_order_release);
    return true;
  } else {
    return false;
  }
}

AutoCloseHandle Win32IocpEventPort::newIocpHandle() {
  HANDLE h;
  KJ_WIN32(h = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1));
  return AutoCloseHandle(h);
}

AutoCloseHandle Win32IocpEventPort::openCurrentThread() {
  HANDLE process = GetCurrentProcess();
  HANDLE result;
  KJ_WIN32(DuplicateHandle(process, GetCurrentThread(), process, &result,
                           0, FALSE, DUPLICATE_SAME_ACCESS));
  return AutoCloseHandle(result);
}

// =======================================================================================

Win32WaitObjectThreadPool::Win32WaitObjectThreadPool(uint mainThreadCount) {}

Own<Win32EventPort::SignalObserver> Win32WaitObjectThreadPool::observeSignalState(HANDLE handle) {
  KJ_UNIMPLEMENTED("wait for win32 handles");
}

uint Win32WaitObjectThreadPool::prepareMainThreadWait(HANDLE* handles[]) {
  KJ_UNIMPLEMENTED("wait for win32 handles");
}

bool Win32WaitObjectThreadPool::finishedMainThreadWait(DWORD returnCode) {
  KJ_UNIMPLEMENTED("wait for win32 handles");
}

} // namespace kj

#endif  // _WIN32
