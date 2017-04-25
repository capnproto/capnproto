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

#if _WIN32

#include "async-win32.h"
#include "thread.h"
#include "test.h"

namespace kj {
namespace {

KJ_TEST("Win32IocpEventPort I/O operations") {
  Win32IocpEventPort port;
  EventLoop loop(port);
  WaitScope waitScope(loop);

  auto pipeName = kj::str("\\\\.\\Pipe\\kj-async-win32-test.", GetCurrentProcessId());

  HANDLE readEnd_, writeEnd_;
  KJ_WIN32(readEnd_ = CreateNamedPipeA(pipeName.cStr(),
      PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
      PIPE_TYPE_BYTE | PIPE_WAIT,
      1, 0, 0, 0, NULL));
  AutoCloseHandle readEnd(readEnd_);

  KJ_WIN32(writeEnd_ = CreateFileA(pipeName.cStr(), GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                                   FILE_ATTRIBUTE_NORMAL, NULL));
  AutoCloseHandle writeEnd(writeEnd_);

  auto observer = port.observeIo(readEnd);
  auto op = observer->newOperation(0);

  byte buffer[256];

  KJ_ASSERT(!ReadFile(readEnd, buffer, sizeof(buffer), NULL, op->getOverlapped()));
  DWORD error = GetLastError();
  if (error != ERROR_IO_PENDING) {
    KJ_FAIL_WIN32("ReadFile()", error);
  }

  bool done = false;
  auto promise = op->onComplete().then([&](Win32EventPort::IoResult result) {
    done = true;
    return result;
  }).eagerlyEvaluate(nullptr);

  KJ_EXPECT(!done);

  evalLater([]() {}).wait(waitScope);
  evalLater([]() {}).wait(waitScope);
  evalLater([]() {}).wait(waitScope);
  evalLater([]() {}).wait(waitScope);
  evalLater([]() {}).wait(waitScope);

  KJ_EXPECT(!done);

  DWORD bytesWritten;
  KJ_WIN32(WriteFile(writeEnd, "foo", 3, &bytesWritten, NULL));
  KJ_EXPECT(bytesWritten == 3);

  auto result = promise.wait(waitScope);
  KJ_EXPECT(result.errorCode == ERROR_SUCCESS);
  KJ_EXPECT(result.bytesTransferred == 3);

  KJ_EXPECT(kj::str(kj::arrayPtr(buffer, 3).asChars()) == "foo");
}

KJ_TEST("Win32IocpEventPort::wake()") {
  Win32IocpEventPort port;

  Thread thread([&]() {
    Sleep(10);
    port.wake();
  });

  KJ_EXPECT(port.wait());
}

KJ_TEST("Win32IocpEventPort::wake() on poll()") {
  Win32IocpEventPort port;
  volatile bool woken = false;

  Thread thread([&]() {
    Sleep(10);
    port.wake();
    woken = true;
  });

  KJ_EXPECT(!port.poll());
  while (!woken) Sleep(10);
  KJ_EXPECT(port.poll());
}

KJ_TEST("Win32IocpEventPort timer") {
  Win32IocpEventPort port;
  EventLoop loop(port);
  WaitScope waitScope(loop);

  auto start = port.getTimer().now();

  bool done = false;
  auto promise = port.getTimer().afterDelay(10 * MILLISECONDS).then([&]() {
    done = true;
  }).eagerlyEvaluate(nullptr);

  KJ_EXPECT(!done);

  evalLater([]() {}).wait(waitScope);
  evalLater([]() {}).wait(waitScope);
  evalLater([]() {}).wait(waitScope);
  evalLater([]() {}).wait(waitScope);
  evalLater([]() {}).wait(waitScope);

  KJ_EXPECT(!done);

  promise.wait(waitScope);
  KJ_EXPECT(done);
  KJ_EXPECT(port.getTimer().now() - start >= 10 * MILLISECONDS);
}

VOID CALLBACK testApcProc(ULONG_PTR dwParam) {
  reinterpret_cast<kj::PromiseFulfiller<void>*>(dwParam)->fulfill();
}

KJ_TEST("Win32IocpEventPort APC") {
  if (GetProcAddress(GetModuleHandle("ntdll.dll"), "wine_get_version") != nullptr) {
    // TODO(cleanup): Periodically check if Wine supports this yet.
    KJ_LOG(WARNING, "detected that we're running under wine and this test won't work; skipping");
    return;
  }

  Win32IocpEventPort port;
  EventLoop loop(port);
  WaitScope waitScope(loop);

  port.allowApc();

  auto paf = kj::newPromiseAndFulfiller<void>();

  KJ_WIN32(QueueUserAPC(&testApcProc, GetCurrentThread(),
      reinterpret_cast<ULONG_PTR>(paf.fulfiller.get())));

  paf.promise.wait(waitScope);
}

}  // namespace
}  // namespace kj

#endif  // _WIN32
