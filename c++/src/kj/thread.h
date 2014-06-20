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

#ifndef KJ_THREAD_H_
#define KJ_THREAD_H_

#include "common.h"
#include "function.h"
#include "exception.h"

namespace kj {

class Thread {
  // A thread!  Pass a lambda to the constructor, and it runs in the thread.  The destructor joins
  // the thread.  If the function throws an exception, it is rethrown from the thread's destructor
  // (if not unwinding from another exception).

public:
  explicit Thread(Function<void()> func);

  ~Thread() noexcept(false);

  void sendSignal(int signo);
  // Send a Unix signal to the given thread, using pthread_kill or an equivalent.

  void detach();
  // Don't join the thread in ~Thread().

private:
  Function<void()> func;
  unsigned long long threadId;  // actually pthread_t
  kj::Maybe<kj::Exception> exception;
  bool detached = false;

  static void* runThread(void* ptr);
};

}  // namespace kj

#endif  // KJ_THREAD_H_
