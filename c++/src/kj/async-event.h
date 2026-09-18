// Copyright (c) 2026 Cloudflare, Inc. and contributors
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

#pragma once

#include "async.h"
#include <kj/refcount.h>

KJ_BEGIN_HEADER

namespace kj {

class AsyncEvent {
  // A same-thread event that retains its first outcome for all current and
  // future observers.

 public:
  AsyncEvent();
  KJ_DISALLOW_COPY_AND_MOVE(AsyncEvent);
  ~AsyncEvent() noexcept(false);

  Promise<void> whenSignaled();
  // Resolves successfully when signal() is called. Resolves with an exception
  // either when reject() is called or when the AsyncEvent is destructed
  // without a resolution.

  bool signal();
  // Returns true if this call signaled the event, or false if it was already
  // settled.

  bool reject(Exception exception);
  // Returns true if this call rejected the event, or false if it was already
  // settled.

 private:
  class BaseWaiter;
  class Waiter;
  struct State;

  Rc<State> state;
};

}  // namespace kj

KJ_END_HEADER
