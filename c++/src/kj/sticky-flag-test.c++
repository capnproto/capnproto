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

#include "sticky-flag.h"

#include <kj/test.h>

namespace kj {
namespace {

KJ_TEST("StickyFlag resolves pending and future observers") {
  EventLoop loop;
  WaitScope waitScope(loop);
  StickyFlag event;

  auto first = event.whenSignaled();
  auto second = event.whenSignaled();
  KJ_EXPECT(!first.poll(waitScope));
  KJ_EXPECT(!second.poll(waitScope));

  KJ_EXPECT(event.signal());
  KJ_EXPECT(!event.signal());
  KJ_EXPECT(!event.reject(KJ_EXCEPTION(FAILED, "ignored")));
  first.wait(waitScope);
  second.wait(waitScope);
  event.whenSignaled().wait(waitScope);
}

KJ_TEST("StickyFlag retains its first rejection") {
  EventLoop loop;
  WaitScope waitScope(loop);
  StickyFlag event;
  auto first = event.whenSignaled();
  auto second = event.whenSignaled();

  auto exception = KJ_EXCEPTION(DISCONNECTED, "signal failed");
  constexpr Exception::DetailTypeId detailId = 0x1234;
  exception.setDetail(detailId, heapArray<byte>({1, 2, 3}));
  KJ_EXPECT(event.reject(kj::mv(exception)));
  KJ_EXPECT(!event.reject(KJ_EXCEPTION(FAILED, "ignored")));
  KJ_EXPECT(!event.signal());

  auto expectRejection = [&](Promise<void> promise) {
    auto caught = runCatchingExceptions([&]() { promise.wait(waitScope); });
    auto& error = KJ_ASSERT_NONNULL(caught);
    KJ_EXPECT(error.getType() == Exception::Type::DISCONNECTED);
    KJ_EXPECT(error.getDescription() == "signal failed");
    auto details = error.getDetails();
    KJ_ASSERT(details.size() == 1);
    KJ_EXPECT(details[0].id == detailId);
    auto expectedDetail = arr<byte>(1, 2, 3);
    KJ_EXPECT(details[0].value == expectedDetail);
  };

  expectRejection(kj::mv(first));
  expectRejection(kj::mv(second));
  expectRejection(event.whenSignaled());
}

KJ_TEST("StickyFlag tolerates canceled observers") {
  EventLoop loop;
  WaitScope waitScope(loop);
  StickyFlag event;

  {
    auto canceled = event.whenSignaled();
    KJ_EXPECT(!canceled.poll(waitScope));
  }

  KJ_EXPECT(event.signal());
  event.whenSignaled().wait(waitScope);
}

KJ_TEST("StickyFlag destruction rejects unresolved observers") {
  EventLoop loop;
  WaitScope waitScope(loop);

  auto promise = [] {
    StickyFlag event;
    return event.whenSignaled();
  }();
  KJ_EXPECT_THROW_MESSAGE(
      "StickyFlag destroyed before it was signaled or rejected", promise.wait(waitScope));
}

KJ_TEST("StickyFlag destruction during unwind reports the unresolved event") {
  EventLoop loop;
  WaitScope waitScope(loop);
  Promise<void> promise = READY_NOW;

  auto caught = runCatchingExceptions([&]() {
    StickyFlag event;
    promise = event.whenSignaled();
    KJ_FAIL_ASSERT("outer failure");
  });
  KJ_EXPECT(KJ_ASSERT_NONNULL(caught).getDescription() == "outer failure");
  KJ_EXPECT_THROW_MESSAGE(
      "StickyFlag destroyed before it was signaled or rejected", promise.wait(waitScope));
}

Promise<void> waitWithUnresolvedEvent(Promise<void>& eventPromise) {
  StickyFlag event;
  eventPromise = event.whenSignaled();
  co_await Promise<void>(NEVER_DONE);
}

KJ_TEST("StickyFlag destruction in a canceled coroutine reports the unresolved event") {
  EventLoop loop;
  WaitScope waitScope(loop);
  Promise<void> eventPromise = READY_NOW;

  auto caught = runCatchingExceptions([&]() {
    auto promise = waitWithUnresolvedEvent(eventPromise);
    KJ_EXPECT(!promise.poll(waitScope));
    KJ_FAIL_ASSERT("outer failure");
  });
  KJ_EXPECT(KJ_ASSERT_NONNULL(caught).getDescription() == "outer failure");
  KJ_EXPECT_THROW_MESSAGE(
      "StickyFlag destroyed before it was signaled or rejected", eventPromise.wait(waitScope));
}

}  // namespace
}  // namespace kj
