// Copyright (c) 2019 Cloudflare, Inc. and contributors
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

#include "byte-stream.h"
#include <kj/one-of.h>
#include <kj/debug.h>

namespace capnp {

class ByteStreamFactory::StreamServerBase: public capnp::ByteStream::Server {
public:
  virtual void returnStream(uint64_t written) = 0;
  // Called after the StreamServerBase's internal kj::AsyncOutputStream has been borrowed, to
  // indicate that the borrower is done.
  //
  // A stream becomes borrowed either when getShortestPath() returns a BorrowedStream, or when
  // a SubstreamImpl is constructed wrapping an existing stream.

  struct BorrowedStream {
    // Represents permission to use the StreamServerBase's inner AsyncOutputStream directly, up
    // to some limit of bytes written.

    StreamServerBase& lender;
    kj::AsyncOutputStream& stream;
    uint64_t limit;
  };

  typedef kj::OneOf<kj::Promise<void>, capnp::ByteStream::Client*, BorrowedStream> ShortestPath;

  virtual ShortestPath getShortestPath() = 0;
  // Called by KjToCapnpStreamAdapter when it has determined that its inner ByteStream::Client
  // actually points back to a StreamServerBase in the same process created by the same
  // ByteStreamFactory. Returns the best shortened path to use, or a promise that resolves when the
  // shortest path is known.

  virtual void directEnd() = 0;
  // Called by KjToCapnpStreamAdapter's destructor when it has determined that its inner
  // ByteStream::Client actually points back to a StreamServerBase in the same process created by
  // the same ByteStreamFactory. Since destruction of a KJ stream signals EOF, we need to propagate
  // that by destroying our underlying stream.
  // TODO(cleanup): When KJ streams evolve an end() method, this can go away.
};

class ByteStreamFactory::SubstreamImpl final: public StreamServerBase {
public:
  SubstreamImpl(ByteStreamFactory& factory,
                StreamServerBase& parent,
                capnp::ByteStream::Client ownParent,
                kj::AsyncOutputStream& stream,
                capnp::ByteStream::SubstreamCallback::Client callback,
                uint64_t limit,
                kj::PromiseFulfillerPair<void> paf = kj::newPromiseAndFulfiller<void>())
      : factory(factory),
        state(Streaming {parent, kj::mv(ownParent), stream, kj::mv(callback)}),
        limit(limit),
        resolveFulfiller(kj::mv(paf.fulfiller)),
        resolvePromise(paf.promise.fork()) {}

  // ---------------------------------------------------------------------------
  // implements StreamServerBase

  void returnStream(uint64_t written) override {
    completed += written;
    KJ_ASSERT(completed <= limit);
    auto borrowed = kj::mv(state.get<Borrowed>());
    state = kj::mv(borrowed.originalState);

    if (completed == limit) {
      limitReached();
    }
  }

  ShortestPath getShortestPath() override {
    KJ_SWITCH_ONEOF(state) {
      KJ_CASE_ONEOF(redirected, Redirected) {
        return &redirected.replacement;
      }
      KJ_CASE_ONEOF(e, Ended) {
        KJ_FAIL_REQUIRE("already called end()");
      }
      KJ_CASE_ONEOF(b, Borrowed) {
        KJ_FAIL_REQUIRE("can't call other methods while substream is active");
      }
      KJ_CASE_ONEOF(streaming, Streaming) {
        auto& stream = streaming.stream;
        auto oldState = kj::mv(streaming);
        state = Borrowed { kj::mv(oldState) };
        return BorrowedStream { *this, stream, limit - completed };
      }
    }
    KJ_UNREACHABLE;
  }

  void directEnd() override {
    KJ_SWITCH_ONEOF(state) {
      KJ_CASE_ONEOF(redirected, Redirected) {
        // Ugh I guess we need to send a real end() request here.
        redirected.replacement.endRequest(MessageSize {2, 0}).send().detach([](kj::Exception&&){});
      }
      KJ_CASE_ONEOF(e, Ended) {
        // whatever
      }
      KJ_CASE_ONEOF(b, Borrowed) {
        // ... whatever.
      }
      KJ_CASE_ONEOF(streaming, Streaming) {
        auto req = streaming.callback.endedRequest(MessageSize {4, 0});
        req.setByteCount(completed);
        req.send().detach([](kj::Exception&&){});
        streaming.parent.returnStream(completed);
        state = Ended();
      }
    }
  }

  // ---------------------------------------------------------------------------
  // implements ByteStream::Server RPC interface

  kj::Maybe<kj::Promise<Capability::Client>> shortenPath() override {
    return resolvePromise.addBranch()
        .then([this]() -> Capability::Client {
      return state.get<Redirected>().replacement;
    });
  }

  kj::Promise<void> write(WriteContext context) override {
    auto params = context.getParams();
    auto data = params.getBytes();

    KJ_SWITCH_ONEOF(state) {
      KJ_CASE_ONEOF(redirected, Redirected) {
        auto req = redirected.replacement.writeRequest(params.totalSize());
        req.setBytes(data);
        return req.send();
      }
      KJ_CASE_ONEOF(e, Ended) {
        KJ_FAIL_REQUIRE("already called end()");
      }
      KJ_CASE_ONEOF(b, Borrowed) {
        KJ_FAIL_REQUIRE("can't call other methods while stream is borrowed");
      }
      KJ_CASE_ONEOF(streaming, Streaming) {
        if (completed + data.size() < limit) {
          completed += data.size();
          return streaming.stream.write(data.begin(), data.size());
        } else {
          // This write passes the limit.
          uint64_t remainder = limit - completed;
          auto leftover = data.slice(remainder, data.size());
          return streaming.stream.write(data.begin(), remainder)
              .then([this, leftover]() -> kj::Promise<void> {
            completed = limit;
            limitReached();

            if (leftover.size() > 0) {
              // Need to forward the leftover bytes to the next stream.
              auto req = state.get<Redirected>().replacement.writeRequest(
                  MessageSize { 4 + leftover.size() / sizeof(capnp::word), 0 });
              req.setBytes(leftover);
              return req.send();
            } else {
              return kj::READY_NOW;
            }
          });
        }
      }
    }
    KJ_UNREACHABLE;
  }

  kj::Promise<void> end(EndContext context) override {
    KJ_SWITCH_ONEOF(state) {
      KJ_CASE_ONEOF(redirected, Redirected) {
        return context.tailCall(redirected.replacement.endRequest(MessageSize {2,0}));
      }
      KJ_CASE_ONEOF(e, Ended) {
        KJ_FAIL_REQUIRE("already called end()");
      }
      KJ_CASE_ONEOF(b, Borrowed) {
        KJ_FAIL_REQUIRE("can't call other methods while stream is borrowed");
      }
      KJ_CASE_ONEOF(streaming, Streaming) {
        auto req = streaming.callback.endedRequest(MessageSize {4, 0});
        req.setByteCount(completed);
        auto result = req.send().ignoreResult();
        streaming.parent.returnStream(completed);
        state = Ended();
        return result;
      }
    }
    KJ_UNREACHABLE;
  }

  kj::Promise<void> getSubstream(GetSubstreamContext context) override {
    KJ_SWITCH_ONEOF(state) {
      KJ_CASE_ONEOF(redirected, Redirected) {
        auto params = context.getParams();
        auto req = redirected.replacement.getSubstreamRequest(params.totalSize());
        req.setCallback(params.getCallback());
        req.setLimit(params.getLimit());
        return context.tailCall(kj::mv(req));
      }
      KJ_CASE_ONEOF(e, Ended) {
        KJ_FAIL_REQUIRE("already called end()");
      }
      KJ_CASE_ONEOF(b, Borrowed) {
        KJ_FAIL_REQUIRE("can't call other methods while stream is borrowed");
      }
      KJ_CASE_ONEOF(streaming, Streaming) {
        auto params = context.getParams();
        auto callback = params.getCallback();
        auto limit = params.getLimit();
        context.releaseParams();
        auto results = context.getResults(MessageSize { 2, 1 });
        results.setSubstream(factory.streamSet.add(kj::heap<SubstreamImpl>(
            factory, *this, thisCap(), streaming.stream, kj::mv(callback), kj::mv(limit))));
        state = Borrowed { kj::mv(streaming) };
        return kj::READY_NOW;
      }
    }
    KJ_UNREACHABLE;
  }

private:
  ByteStreamFactory& factory;

  struct Streaming {
    StreamServerBase& parent;
    capnp::ByteStream::Client ownParent;
    kj::AsyncOutputStream& stream;
    capnp::ByteStream::SubstreamCallback::Client callback;
  };
  struct Borrowed {
    Streaming originalState;
  };
  struct Redirected {
    capnp::ByteStream::Client replacement;
  };
  struct Ended {};

  kj::OneOf<Streaming, Borrowed, Redirected, Ended> state;

  uint64_t limit;
  uint64_t completed = 0;

  kj::Own<kj::PromiseFulfiller<void>> resolveFulfiller;
  kj::ForkedPromise<void> resolvePromise;

  void limitReached() {
    auto& streaming = state.get<Streaming>();
    auto next = streaming.callback.reachedLimitRequest(capnp::MessageSize {2,0})
        .send().getNext();

    // Set the next stream as our replacement.
    streaming.parent.returnStream(limit);
    state = Redirected { kj::mv(next) };
    resolveFulfiller->fulfill();
  }
};

// =======================================================================================

class ByteStreamFactory::CapnpToKjStreamAdapter final: public StreamServerBase {
  // Implements Cap'n Proto ByteStream as a wrapper around a KJ stream.

  class SubstreamCallbackImpl;

public:
  class PathProber;

  CapnpToKjStreamAdapter(ByteStreamFactory& factory,
                         kj::Own<kj::AsyncOutputStream> inner)
      : factory(factory),
        state(kj::heap<PathProber>(*this, kj::mv(inner))) {
    state.get<kj::Own<PathProber>>()->startProbing();
  }

  CapnpToKjStreamAdapter(ByteStreamFactory& factory,
                         kj::Own<PathProber> pathProber)
      : factory(factory),
        state(kj::mv(pathProber)) {
    state.get<kj::Own<PathProber>>()->setNewParent(*this);
  }

  // ---------------------------------------------------------------------------
  // implements StreamServerBase

  void returnStream(uint64_t written) override {
    auto stream = kj::mv(state.get<Borrowed>().stream);
    state = kj::mv(stream);
  }

  ShortestPath getShortestPath() override {
    // Called by KjToCapnpStreamAdapter when it has determined that its inner ByteStream::Client
    // actually points back to a CapnpToKjStreamAdapter in the same process. Returns the best
    // shortened path to use, or a promise that resolves when the shortest path is known.

    KJ_SWITCH_ONEOF(state) {
      KJ_CASE_ONEOF(prober, kj::Own<PathProber>) {
        return prober->whenReady();
      }
      KJ_CASE_ONEOF(kjStream, kj::Own<kj::AsyncOutputStream>) {
        auto& streamRef = *kjStream;
        state = Borrowed { kj::mv(kjStream) };
        return StreamServerBase::BorrowedStream { *this, streamRef, kj::maxValue };
      }
      KJ_CASE_ONEOF(capnpStream, capnp::ByteStream::Client) {
        return &capnpStream;
      }
      KJ_CASE_ONEOF(b, Borrowed) {
        KJ_FAIL_REQUIRE("concurrent streaming calls disallowed") { break; }
        return kj::Promise<void>(kj::READY_NOW);
      }
      KJ_CASE_ONEOF(e, Ended) {
        KJ_FAIL_REQUIRE("already ended") { break; }
        return kj::Promise<void>(kj::READY_NOW);
      }
    }
    KJ_UNREACHABLE;
  }

  void directEnd() override {
    KJ_SWITCH_ONEOF(state) {
      KJ_CASE_ONEOF(prober, kj::Own<PathProber>) {
        state = Ended();
      }
      KJ_CASE_ONEOF(kjStream, kj::Own<kj::AsyncOutputStream>) {
        state = Ended();
      }
      KJ_CASE_ONEOF(capnpStream, capnp::ByteStream::Client) {
        // Ugh I guess we need to send a real end() request here.
        capnpStream.endRequest(MessageSize {2, 0}).send().detach([](kj::Exception&&){});
      }
      KJ_CASE_ONEOF(b, Borrowed) {
        // Fine, ignore.
      }
      KJ_CASE_ONEOF(e, Ended) {
        // Fine, ignore.
      }
    }
  }

  // ---------------------------------------------------------------------------
  // PathProber

  class PathProber final: public kj::AsyncInputStream {
  public:
    PathProber(CapnpToKjStreamAdapter& parent, kj::Own<kj::AsyncOutputStream> inner,
               kj::PromiseFulfillerPair<void> paf = kj::newPromiseAndFulfiller<void>())
        : parent(parent), inner(kj::mv(inner)),
          readyPromise(paf.promise.fork()),
          readyFulfiller(kj::mv(paf.fulfiller)),
          task(nullptr) {}

    void startProbing() {
      task = probeForShorterPath();
    }

    void setNewParent(CapnpToKjStreamAdapter& newParent) {
      KJ_ASSERT(parent == nullptr);
      parent = newParent;
      auto paf = kj::newPromiseAndFulfiller<void>();
      readyPromise = paf.promise.fork();
      readyFulfiller = kj::mv(paf.fulfiller);
    }

    kj::Promise<void> whenReady() {
      return readyPromise.addBranch();
    }

    kj::Promise<uint64_t> pumpToShorterPath(capnp::ByteStream::Client target, uint64_t limit) {
      // If our probe succeeds in finding a KjToCapnpStreamAdapter somewhere down the stack, that
      // will call this method to provide the shortened path.

      KJ_IF_MAYBE(currentParent, parent) {
        parent = nullptr;

        auto self = kj::mv(currentParent->state.get<kj::Own<PathProber>>());
        currentParent->state = Ended();  // temporary, we'll set this properly below
        KJ_ASSERT(self.get() == this);

        // Open a substream on the target stream.
        auto req = target.getSubstreamRequest();
        req.setLimit(limit);
        auto paf = kj::newPromiseAndFulfiller<uint64_t>();
        req.setCallback(kj::heap<SubstreamCallbackImpl>(currentParent->factory,
            kj::mv(self), kj::mv(paf.fulfiller), limit));

        // Now we hook up the incoming stream adapter to point directly to this substream, yay.
        currentParent->state = req.send().getSubstream();

        // Let the original CapnpToKjStreamAdapter know that it's safe to handle incoming requests.
        readyFulfiller->fulfill();

        // It's now up to the SubstreamCallbackImpl to signal when the pump is done.
        return kj::mv(paf.promise);
      } else {
        // We already completed a path-shortening. Probably SubstreamCallbackImpl::ended() was
        // eventually called, meaning the substream was ended without redirecting back to us. So,
        // we're at EOF.
        return uint64_t(0);
      }
    }

    kj::Promise<size_t> tryRead(void* buffer, size_t minBytes, size_t maxBytes) override {
      // If this is called, it means the tryPumpFrom() in probeForShorterPath() eventually invoked
      // code that tries to read manually from the source. We don't know what this code is doing
      // exactly, but we do know for sure that the endpoint is not a KjToCapnpStreamAdapter, so
      // we can't optimize. Instead, we pretend that we immediately hit EOF, ending the pump. This
      // works because pumps do not propagate EOF -- the destination can still receive further
      // writes and pumps. Basically our probing pump becomes a no-op, and then we revert to having
      // each write() RPC directly call write() on the inner stream.
      return size_t(0);
    }

    kj::Promise<uint64_t> pumpTo(kj::AsyncOutputStream& output, uint64_t amount) override {
      // Call the stream's `tryPumpFrom()` as a way to discover where the data will eventually go,
      // in hopes that we find we can shorten the path.
      KJ_IF_MAYBE(promise, output.tryPumpFrom(*this, amount)) {
        // tryPumpFrom() returned non-null. Either it called `tryRead()` or `pumpTo()` (see
        // below), or it plans to do so in the future.
        return kj::mv(*promise);
      } else {
        // There is no shorter path. As with tryRead(), we pretend we get immediate EOF.
        return uint64_t(0);
      }
    }

  private:
    kj::Maybe<CapnpToKjStreamAdapter&> parent;
    kj::Own<kj::AsyncOutputStream> inner;
    kj::ForkedPromise<void> readyPromise;
    kj::Own<kj::PromiseFulfiller<void>> readyFulfiller;
    kj::Promise<void> task;

    friend class SubstreamCallbackImpl;

    kj::Promise<void> probeForShorterPath() {
      return kj::evalNow([&]() -> kj::Promise<uint64_t> {
        return pumpTo(*inner, kj::maxValue);
      }).then([this](uint64_t actual) {
        KJ_IF_MAYBE(currentParent, parent) {
          KJ_IF_MAYBE(prober, currentParent->state.tryGet<kj::Own<PathProber>>()) {
            // Either we didn't find any shorter path at all during probing and faked an EOF
            // to get out of the probe (see comments in tryRead(), or we DID find a shorter path,
            // completed a pumpTo() using a substream, and that substream redirected back to us,
            // and THEN we couldn't find any further shorter paths for subsequent pumps.

            // HACK: If we overwrite the Probing state now, we'll delete ourselves and delete
            //   this task promise, which is an error... let the event loop do it later by
            //   detaching.
            task.attach(kj::mv(*prober)).detach([](kj::Exception&&){});
            parent = nullptr;

            // OK, now we can change the parent state and signal it to proceed.
            currentParent->state = kj::mv(inner);
            readyFulfiller->fulfill();
          }
        }
      }).eagerlyEvaluate([this](kj::Exception&& exception) mutable {
        // Something threw, so propagate the exception to break the parent.
        readyFulfiller->reject(kj::mv(exception));
      });
    }
  };

protected:
  // ---------------------------------------------------------------------------
  // implements ByteStream::Server RPC interface

  kj::Maybe<kj::Promise<Capability::Client>> shortenPath() override {
    return shortenPathImpl();
  }
  kj::Promise<Capability::Client> shortenPathImpl() {
    // Called by RPC implementation to find out if a shorter path presents itself.
    KJ_SWITCH_ONEOF(state) {
      KJ_CASE_ONEOF(prober, kj::Own<PathProber>) {
        return prober->whenReady().then([this]() {
          KJ_ASSERT(!state.is<kj::Own<PathProber>>());
          return shortenPathImpl();
        });
      }
      KJ_CASE_ONEOF(kjStream, kj::Own<kj::AsyncOutputStream>) {
        // No shortening possible. Pretend we never resolve so that calls continue to be routed
        // to us forever.
        return kj::NEVER_DONE;
      }
      KJ_CASE_ONEOF(capnpStream, capnp::ByteStream::Client) {
        return Capability::Client(capnpStream);
      }
      KJ_CASE_ONEOF(b, Borrowed) {
        KJ_FAIL_REQUIRE("concurrent streaming calls disallowed") { break; }
        return kj::NEVER_DONE;
      }
      KJ_CASE_ONEOF(e, Ended) {
        // No shortening possible. Pretend we never resolve so that calls continue to be routed
        // to us forever.
        return kj::NEVER_DONE;
      }
    }
    KJ_UNREACHABLE;
  }

  kj::Promise<void> write(WriteContext context) override {
    KJ_SWITCH_ONEOF(state) {
      KJ_CASE_ONEOF(prober, kj::Own<PathProber>) {
        return prober->whenReady().then([this, context]() mutable {
          KJ_ASSERT(!state.is<kj::Own<PathProber>>());
          return write(context);
        });
      }
      KJ_CASE_ONEOF(kjStream, kj::Own<kj::AsyncOutputStream>) {
        auto data = context.getParams().getBytes();
        return kjStream->write(data.begin(), data.size());
      }
      KJ_CASE_ONEOF(capnpStream, capnp::ByteStream::Client) {
        auto params = context.getParams();
        auto req = capnpStream.writeRequest(params.totalSize());
        req.setBytes(params.getBytes());
        return req.send();
      }
      KJ_CASE_ONEOF(b, Borrowed) {
        KJ_FAIL_REQUIRE("concurrent streaming calls disallowed") { break; }
        return kj::READY_NOW;
      }
      KJ_CASE_ONEOF(e, Ended) {
        KJ_FAIL_REQUIRE("already called end()") { break; }
        return kj::READY_NOW;
      }
    }
    KJ_UNREACHABLE;
  }

  kj::Promise<void> end(EndContext context) override {
    KJ_SWITCH_ONEOF(state) {
      KJ_CASE_ONEOF(prober, kj::Own<PathProber>) {
        return prober->whenReady().then([this, context]() mutable {
          KJ_ASSERT(!state.is<kj::Own<PathProber>>());
          return end(context);
        });
      }
      KJ_CASE_ONEOF(kjStream, kj::Own<kj::AsyncOutputStream>) {
        // TODO(someday): When KJ adds a proper .end() call, use it here. For now, we must
        //   drop the stream to close it.
        state = Ended();
        return kj::READY_NOW;
      }
      KJ_CASE_ONEOF(capnpStream, capnp::ByteStream::Client) {
        auto params = context.getParams();
        auto req = capnpStream.endRequest(params.totalSize());
        return context.tailCall(kj::mv(req));
      }
      KJ_CASE_ONEOF(b, Borrowed) {
        KJ_FAIL_REQUIRE("concurrent streaming calls disallowed") { break; }
        return kj::READY_NOW;
      }
      KJ_CASE_ONEOF(e, Ended) {
        KJ_FAIL_REQUIRE("already called end()") { break; }
        return kj::READY_NOW;
      }
    }
    KJ_UNREACHABLE;
  }

  kj::Promise<void> getSubstream(GetSubstreamContext context) override {
    KJ_SWITCH_ONEOF(state) {
      KJ_CASE_ONEOF(prober, kj::Own<PathProber>) {
        return prober->whenReady().then([this, context]() mutable {
          KJ_ASSERT(!state.is<kj::Own<PathProber>>());
          return getSubstream(context);
        });
      }
      KJ_CASE_ONEOF(kjStream, kj::Own<kj::AsyncOutputStream>) {
        auto params = context.getParams();
        auto callback = params.getCallback();
        uint64_t limit = params.getLimit();
        context.releaseParams();

        auto results = context.initResults(MessageSize {2, 1});
        results.setSubstream(factory.streamSet.add(kj::heap<SubstreamImpl>(
            factory, *this, thisCap(), *kjStream, kj::mv(callback), kj::mv(limit))));
        state = Borrowed { kj::mv(kjStream) };
        return kj::READY_NOW;
      }
      KJ_CASE_ONEOF(capnpStream, capnp::ByteStream::Client) {
        auto params = context.getParams();
        auto req = capnpStream.getSubstreamRequest(params.totalSize());
        req.setCallback(params.getCallback());
        req.setLimit(params.getLimit());
        return context.tailCall(kj::mv(req));
      }
      KJ_CASE_ONEOF(b, Borrowed) {
        KJ_FAIL_REQUIRE("concurrent streaming calls disallowed") { break; }
        return kj::READY_NOW;
      }
      KJ_CASE_ONEOF(e, Ended) {
        KJ_FAIL_REQUIRE("already called end()") { break; }
        return kj::READY_NOW;
      }
    }
    KJ_UNREACHABLE;
  }

private:
  ByteStreamFactory& factory;

  struct Borrowed { kj::Own<kj::AsyncOutputStream> stream; };
  struct Ended {};

  kj::OneOf<kj::Own<PathProber>, kj::Own<kj::AsyncOutputStream>,
            capnp::ByteStream::Client, Borrowed, Ended> state;

  class SubstreamCallbackImpl final: public capnp::ByteStream::SubstreamCallback::Server {
  public:
    SubstreamCallbackImpl(ByteStreamFactory& factory,
                          kj::Own<PathProber> pathProber,
                          kj::Own<kj::PromiseFulfiller<uint64_t>> originalPumpfulfiller,
                          uint64_t originalPumpLimit)
        : factory(factory),
          pathProber(kj::mv(pathProber)),
          originalPumpfulfiller(kj::mv(originalPumpfulfiller)),
          originalPumpLimit(originalPumpLimit) {}

    ~SubstreamCallbackImpl() noexcept(false) {
      if (!done) {
        originalPumpfulfiller->reject(KJ_EXCEPTION(DISCONNECTED,
            "stream disconnected because SubstreamCallbackImpl was never called back"));
      }
    }

    kj::Promise<void> ended(EndedContext context) override {
      KJ_REQUIRE(!done);
      uint64_t actual = context.getParams().getByteCount();
      KJ_REQUIRE(actual <= originalPumpLimit);

      done = true;

      // EOF before pump completed. Signal a short pump.
      originalPumpfulfiller->fulfill(context.getParams().getByteCount());

      // Give the original pump task a chance to finish up.
      return pathProber->task.attach(kj::mv(pathProber));
    }

    kj::Promise<void> reachedLimit(ReachedLimitContext context) override {
      KJ_REQUIRE(!done);
      done = true;

      // Allow the shortened stream to redirect back to our original underlying stream.
      auto results = context.getResults(capnp::MessageSize { 4, 1 });
      results.setNext(factory.streamSet.add(
          kj::heap<CapnpToKjStreamAdapter>(factory, kj::mv(pathProber))));

      // The full pump completed. Note that it's important that we fulfill this after the
      // PathProber has been attached to the new CapnpToKjStreamAdapter, which will have happened
      // in CapnpToKjStreamAdapter's constructor, which calls pathProber->setNewParent().
      originalPumpfulfiller->fulfill(kj::cp(originalPumpLimit));

      return kj::READY_NOW;
    }

  private:
    ByteStreamFactory& factory;
    kj::Own<PathProber> pathProber;
    kj::Own<kj::PromiseFulfiller<uint64_t>> originalPumpfulfiller;
    uint64_t originalPumpLimit;
    bool done = false;
  };
};

// =======================================================================================

class ByteStreamFactory::KjToCapnpStreamAdapter final: public kj::AsyncOutputStream {
public:
  KjToCapnpStreamAdapter(ByteStreamFactory& factory, capnp::ByteStream::Client innerParam)
      : factory(factory),
        inner(kj::mv(innerParam)),
        findShorterPathTask(findShorterPath(inner).fork()) {}

  ~KjToCapnpStreamAdapter() noexcept(false) {
    // HACK: KJ streams are implicitly ended on destruction, but the RPC stream needs a call. We
    //   use a detached promise for now, which is probably OK since capabilities are refcounted and
    //   asynchronously destroyed anyway.
    // TODO(cleanup): Fix this when KJ streads add an explicit end() method.
    KJ_IF_MAYBE(o, optimized) {
      o->directEnd();
    } else {
      inner.endRequest(MessageSize {2, 0}).send().detach([](kj::Exception&&){});
    }
  }

  kj::Promise<void> write(const void* buffer, size_t size) override {
    KJ_SWITCH_ONEOF(getShortestPath()) {
      KJ_CASE_ONEOF(promise, kj::Promise<void>) {
        return promise.then([this,buffer,size]() {
          return write(buffer, size);
        });
      }
      KJ_CASE_ONEOF(kjStream, StreamServerBase::BorrowedStream) {
        if (size <= kjStream.limit) {
          auto promise = kjStream.stream.write(buffer, size);
          return promise.then([kjStream,size]() mutable {
            kjStream.lender.returnStream(size);
          });
        } else {
          auto promise = kjStream.stream.write(buffer, kjStream.limit);
          return promise.then([this,kjStream,buffer,size]() mutable {
            kjStream.lender.returnStream(kjStream.limit);
            return write(reinterpret_cast<const byte*>(buffer) + kjStream.limit,
                         size - kjStream.limit);
          });
        }
      }
      KJ_CASE_ONEOF(capnpStream, capnp::ByteStream::Client*) {
        auto req = capnpStream->writeRequest(MessageSize { 8 + size / sizeof(word), 0 });
        req.setBytes(kj::arrayPtr(reinterpret_cast<const byte*>(buffer), size));
        return req.send();
      }
    }
    KJ_UNREACHABLE;
  }

  kj::Promise<void> write(kj::ArrayPtr<const kj::ArrayPtr<const byte>> pieces) override {
    KJ_SWITCH_ONEOF(getShortestPath()) {
      KJ_CASE_ONEOF(promise, kj::Promise<void>) {
        return promise.then([this,pieces]() {
          return write(pieces);
        });
      }
      KJ_CASE_ONEOF(kjStream, StreamServerBase::BorrowedStream) {
        size_t size = 0;
        for (auto& piece: pieces) { size += piece.size(); }
        if (size <= kjStream.limit) {
          auto promise = kjStream.stream.write(pieces);
          return promise.then([kjStream,size]() mutable {
            kjStream.lender.returnStream(size);
          });
        } else {
          // ughhhhhhhhhh, we need to split the pieces.
          size_t splitByte = kjStream.limit;
          size_t splitPiece = 0;
          while (pieces[splitPiece].size() <= splitByte) {
            splitByte -= pieces[splitPiece].size();
            ++splitPiece;
          }

          if (splitByte == 0) {
            // Oh thank god, the split is between two pieces.
            auto rest = pieces.slice(splitPiece, pieces.size());
            return kjStream.stream.write(pieces.slice(0, splitPiece))
                .then([this,kjStream,rest]() {
              kjStream.lender.returnStream(kjStream.limit);
              return write(rest);
            });
          } else {
            // FUUUUUUUU---- we need to split one of the pieces in two.
            auto left = kj::heapArrayBuilder<kj::ArrayPtr<const byte>>(splitPiece + 1);
            auto right = kj::heapArrayBuilder<kj::ArrayPtr<const byte>>(pieces.size() - splitPiece);

            for (auto i: kj::zeroTo(splitPiece)) {
              left[i] = pieces[i];
            }
            for (auto i: kj::zeroTo(right.size())) {
              right[i] = pieces[splitPiece + i];
            }

            left.back() = pieces[splitPiece].slice(0, splitByte);
            right.front() = pieces[splitPiece].slice(splitByte, pieces[splitPiece].size());

            return kjStream.stream.write(left).attach(kj::mv(left))
                .then([this,kjStream,right = kj::mv(right)]() mutable {
              kjStream.lender.returnStream(kjStream.limit);
              return write(right).attach(kj::mv(right));
            });
          }
        }
      }
      KJ_CASE_ONEOF(capnpStream, capnp::ByteStream::Client*) {
        size_t size = 0;
        for (auto& piece: pieces) size += piece.size();
        auto req = capnpStream->writeRequest(MessageSize { 8 + size / sizeof(word), 0 });

        auto out = req.initBytes(size);
        byte* ptr = out.begin();
        for (auto& piece: pieces) {
          memcpy(ptr, piece.begin(), piece.size());
          ptr += piece.size();
        }
        KJ_ASSERT(ptr == out.end());

        return req.send();
      }
    }
    KJ_UNREACHABLE;
  }

  kj::Maybe<kj::Promise<uint64_t>> tryPumpFrom(
      kj::AsyncInputStream& input, uint64_t amount = kj::maxValue) override {
    KJ_IF_MAYBE(rpc, kj::dynamicDowncastIfAvailable<CapnpToKjStreamAdapter::PathProber>(input)) {
      // Oh interesting, it turns we're hosting an incoming ByteStream which is pumping to this
      // outgoing ByteStream. We can let the Cap'n Proto RPC layer know that it can shorten the
      // path from one to the other.
      return rpc->pumpToShorterPath(inner, amount);
    } else {
      return pumpLoop(input, 0, amount);
    }
  }

  kj::Promise<void> whenWriteDisconnected() override {
    return findShorterPathTask.addBranch();
  }

private:
  ByteStreamFactory& factory;
  capnp::ByteStream::Client inner;
  kj::Maybe<StreamServerBase&> optimized;

  kj::ForkedPromise<void> findShorterPathTask;
  // This serves two purposes:
  // 1. Waits for the capability to resolve (if it is a promise), and then shortens the path if
  //    possible.
  // 2. Implements whenWriteDisconnected().

  kj::Promise<void> findShorterPath(capnp::ByteStream::Client& capnpClient) {
    // If the capnp stream turns out to resolve back to this process, shorten the path.
    // Also, implement whenWriteDisconnected() based on this.
    return factory.streamSet.getLocalServer(capnpClient)
        .then([this](kj::Maybe<capnp::ByteStream::Server&> server) -> kj::Promise<void> {
      KJ_IF_MAYBE(s, server) {
        // Yay, we discovered that the ByteStream actually points back to a local KJ stream.
        // We can use this to shorten the path by skipping the RPC machinery.
        return findShorterPath(kj::downcast<StreamServerBase>(*s));
      } else {
        // The capability is fully-resolved. This suggests that the remote implementation is
        // NOT a CapnpToKjStreamAdapter at all, because CapnpToKjStreamAdapter is designed to
        // always look like a promise. It's some other implementation that doesn't present
        // itself as a promise. We have no way to detect when it is disconnected.
        return kj::NEVER_DONE;
      }
    }, [](kj::Exception&& e) -> kj::Promise<void> {
      // getLocalServer() thrown when the capability is a promise cap that rejects. We can
      // use this to implement whenWriteDisconnected().
      //
      // (Note that because this exception handler is passed to the .then(), it does NOT catch
      // eoxceptions thrown by the success handler immediately above it. This handler will ONLY
      // catch exceptions from getLocalServer() itself.)
      return kj::READY_NOW;
    });
  }

  kj::Promise<void> findShorterPath(StreamServerBase& capnpServer) {
    // We found a shorter path back to this process. Record it.
    optimized = capnpServer;

    KJ_SWITCH_ONEOF(capnpServer.getShortestPath()) {
      KJ_CASE_ONEOF(promise, kj::Promise<void>) {
        return promise.then([this,&capnpServer]() {
          return findShorterPath(capnpServer);
        });
      }
      KJ_CASE_ONEOF(kjStream, StreamServerBase::BorrowedStream) {
        // The ByteStream::Server wraps a regular KJ stream that does not wrap another capnp
        // stream.
        if (kjStream.limit < (uint64_t)kj::maxValue / 2) {
          // But it isn't wrapping that stream forever. Eventually it plans to redirect back to
          // some other stream. So, let's wait for that, and possibly shorten again.
          kjStream.lender.returnStream(0);
          return KJ_ASSERT_NONNULL(capnpServer.shortenPath())
              .then([this, &capnpServer](auto&&) {
            return findShorterPath(capnpServer);
          });
        } else {
          // This KJ stream is (effectively) the permanent endpoint. We can't get any shorter
          // from here. All we want to do now is watch for disconnect.
          auto promise = kjStream.stream.whenWriteDisconnected();
          kjStream.lender.returnStream(0);
          return promise;
        }
      }
      KJ_CASE_ONEOF(capnpStream, capnp::ByteStream::Client*) {
        return findShorterPath(*capnpStream);
      }
    }
    KJ_UNREACHABLE;
  }

  StreamServerBase::ShortestPath getShortestPath() {
    KJ_IF_MAYBE(o, optimized) {
      return o->getShortestPath();
    } else {
      return &inner;
    }
  }

  kj::Promise<uint64_t> pumpLoop(kj::AsyncInputStream& input,
                                 uint64_t completed, uint64_t remaining) {
    if (remaining == 0) return completed;

    KJ_SWITCH_ONEOF(getShortestPath()) {
      KJ_CASE_ONEOF(promise, kj::Promise<void>) {
        return promise.then([this,&input,completed,remaining]() {
          return pumpLoop(input,completed,remaining);
        });
      }
      KJ_CASE_ONEOF(kjStream, StreamServerBase::BorrowedStream) {
        // Oh hell yes, this capability actually points back to a stream in our own thread. We can
        // stop sending RPCs and just pump directly.

        if (remaining <= kjStream.limit) {
          return input.pumpTo(kjStream.stream, remaining)
              .then([kjStream,completed](uint64_t actual) {
            kjStream.lender.returnStream(actual);
            return actual + completed;
          });
        } else {
          auto promise = input.pumpTo(kjStream.stream, kjStream.limit);
          return promise.then([this,&input,completed,remaining,kjStream]
                              (uint64_t actual) mutable -> kj::Promise<uint64_t> {
            kjStream.lender.returnStream(actual);
            if (actual < kjStream.limit) {
              // EOF reached.
              return completed + actual;
            } else {
              return pumpLoop(input, completed + actual, remaining - actual);
            }
          });
        }
      }
      KJ_CASE_ONEOF(capnpStream, capnp::ByteStream::Client*) {
        // Pumping from some other kind of steram. Optimize the pump by reading from the input
        // directly into outgoing RPC messages.
        size_t size = kj::min(remaining, 8192);
        auto req = capnpStream->writeRequest(MessageSize { 8 + size / sizeof(word) });

        auto orphanage = Orphanage::getForMessageContaining(
            capnp::ByteStream::WriteParams::Builder(req));

        auto buffer = orphanage.newOrphan<Data>(size);

        struct WriteRequestAndBuffer {
          // The order of construction/destruction of lambda captures is unspecified, but we care
          // about ordering between these two things that we want to capture, so... we need a
          // struct.
          StreamingRequest<capnp::ByteStream::WriteParams> request;
          Orphan<Data> buffer;  // points into `request`...
        };

        WriteRequestAndBuffer wrab = { kj::mv(req), kj::mv(buffer) };

        return input.tryRead(wrab.buffer.get().begin(), 1, size)
            .then([this, &input, completed, remaining, size, wrab = kj::mv(wrab)]
                  (size_t actual) mutable -> kj::Promise<uint64_t> {
          if (actual == 0) {
            return completed;
          } if (actual < size) {
            wrab.buffer.truncate(actual);
          }

          wrab.request.adoptBytes(kj::mv(wrab.buffer));
          return wrab.request.send()
              .then([this, &input, completed, remaining, actual]() {
            return pumpLoop(input, completed + actual, remaining - actual);
          });
        });
      }
    }
    KJ_UNREACHABLE;
  }
};

// =======================================================================================

capnp::ByteStream::Client ByteStreamFactory::kjToCapnp(kj::Own<kj::AsyncOutputStream> kjStream) {
  return streamSet.add(kj::heap<CapnpToKjStreamAdapter>(*this, kj::mv(kjStream)));
}

kj::Own<kj::AsyncOutputStream> ByteStreamFactory::capnpToKj(capnp::ByteStream::Client capnpStream) {
  return kj::heap<KjToCapnpStreamAdapter>(*this, kj::mv(capnpStream));
}

}  // namespace capnp
