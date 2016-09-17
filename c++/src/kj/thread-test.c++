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

#include "thread.h"
#include "test.h"
#include <atomic>

#if _WIN32
#define NOGDI
#include <windows.h>
#undef NOGDI
#else
#include <unistd.h>
#endif

namespace kj {
namespace {

#if _WIN32
inline void delay() { Sleep(10); }
#else
inline void delay() { usleep(10000); }
#endif

KJ_TEST("detaching thread doesn't delete function") {
  struct Functor {
    // Functor that sets *b = true on destruction, not counting moves.

    std::atomic<bool>* destroyed;
    const std::atomic<bool>* canExit;

    Functor(std::atomic<bool>* destroyed, const std::atomic<bool>* canExit)
        : destroyed(destroyed), canExit(canExit) {}

    ~Functor() {
      if (destroyed != nullptr) *destroyed = true;
    }
    KJ_DISALLOW_COPY(Functor);
    Functor(Functor&& other): destroyed(other.destroyed), canExit(other.canExit) {
      other.destroyed = nullptr;
    }
    Functor& operator=(Functor&& other) = delete;

    void operator()() {
      while (!*canExit) delay();
    }
  };

  std::atomic<bool> destroyed(false);
  std::atomic<bool> canExit(false);
  Functor f(&destroyed, &canExit);

  kj::Thread(kj::mv(f)).detach();

  // detach() should not have destroyed the function.
  KJ_ASSERT(!destroyed);

  // Delay a bit to make sure the thread has had time to start up, and then make sure the function
  // still isn't destroyed.
  delay();
  delay();
  KJ_ASSERT(!destroyed);

  // Notify the thread that it's safe to exit.
  canExit = true;
  while (!destroyed) {
    delay();
  }
}

}  // namespace
}  // namespace kj
