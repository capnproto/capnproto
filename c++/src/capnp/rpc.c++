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

#include "rpc.h"
#include "message.h"
#include <kj/debug.h>
#include <kj/vector.h>
#include <kj/async.h>
#include <kj/one-of.h>
#include <kj/function.h>
#include <kj/map.h>
#include <functional>  // std::greater
#include <queue>
#include <capnp/rpc.capnp.h>
#include <kj/io.h>
#include <kj/map.h>

namespace capnp {
namespace _ {  // private

[[noreturn]] void throwNo3ph() {
  KJ_FAIL_REQUIRE(
      "This VatNetwork does not support three-party handoff, but received a request from a peer "
      "indicating that a three-party handoff should occur.");
}

namespace {

template <typename T>
inline constexpr uint messageSizeHint() {
  return 1 + sizeInWords<rpc::Message>() + sizeInWords<T>();
}
template <>
inline constexpr uint messageSizeHint<void>() {
  return 1 + sizeInWords<rpc::Message>();
}

constexpr const uint MESSAGE_TARGET_SIZE_HINT = sizeInWords<rpc::MessageTarget>() +
    sizeInWords<rpc::PromisedAnswer>() + 16;  // +16 for ops; hope that's enough

constexpr const uint CAP_DESCRIPTOR_SIZE_HINT = sizeInWords<rpc::CapDescriptor>() +
    sizeInWords<rpc::PromisedAnswer>();

constexpr const uint64_t MAX_SIZE_HINT = 1 << 20;

uint copySizeHint(MessageSize size) {
  uint64_t sizeHint = size.wordCount + size.capCount * CAP_DESCRIPTOR_SIZE_HINT
                    // if capCount > 0, the cap descriptor list has a 1-word tag
                    + (size.capCount > 0);
  return kj::min(MAX_SIZE_HINT, sizeHint);
}

uint firstSegmentSize(kj::Maybe<MessageSize> sizeHint, uint additional) {
  KJ_IF_SOME(s, sizeHint) {
    return copySizeHint(s) + additional;
  } else {
    return 0;
  }
}

kj::Array<PipelineOp> toPipelineOps(List<rpc::PromisedAnswer::Op>::Reader ops) {
  auto result = kj::heapArrayBuilder<PipelineOp>(ops.size());
  for (auto opReader: ops) {
    PipelineOp op;
    switch (opReader.which()) {
      case rpc::PromisedAnswer::Op::NOOP:
        op.type = PipelineOp::NOOP;
        break;
      case rpc::PromisedAnswer::Op::GET_POINTER_FIELD:
        op.type = PipelineOp::GET_POINTER_FIELD;
        op.pointerIndex = opReader.getGetPointerField();
        break;
      default:
        KJ_FAIL_REQUIRE("Unsupported pipeline op.", (uint)opReader.which());
    }
    result.add(op);
  }
  return result.finish();
}

Orphan<List<rpc::PromisedAnswer::Op>> fromPipelineOps(
    Orphanage orphanage, kj::ArrayPtr<const PipelineOp> ops) {
  auto result = orphanage.newOrphan<List<rpc::PromisedAnswer::Op>>(ops.size());
  auto builder = result.get();
  for (uint i: kj::indices(ops)) {
    rpc::PromisedAnswer::Op::Builder opBuilder = builder[i];
    switch (ops[i].type) {
      case PipelineOp::NOOP:
        opBuilder.setNoop();
        break;
      case PipelineOp::GET_POINTER_FIELD:
        opBuilder.setGetPointerField(ops[i].pointerIndex);
        break;
    }
  }
  return result;
}

kj::Exception toException(const rpc::Exception::Reader& exception) {
  auto reason = [&]() {
    if (exception.getReason().startsWith("remote exception: ")) {
      return kj::str(exception.getReason());
    } else {
      return kj::str("remote exception: ", exception.getReason());
    }
  }();

  kj::Exception result(static_cast<kj::Exception::Type>(exception.getType()),
      "(remote)", 0, kj::mv(reason));
  if (exception.hasTrace()) {
    result.setRemoteTrace(kj::str(exception.getTrace()));
  }
  for (auto detail : exception.getDetails()) {
    if (detail.hasData()) {
      result.setDetail(detail.getDetailId(), kj::heapArray(detail.getData()));
    }
  }
  return result;
}

void fromException(const kj::Exception& exception, rpc::Exception::Builder builder,
                   kj::Maybe<kj::Function<kj::String(const kj::Exception&)>&> traceEncoder) {
  kj::StringPtr description = exception.getDescription();

  // Include context, if any.
  kj::Vector<kj::String> contextLines;
  for (auto context = exception.getContext();;) {
    KJ_IF_SOME(c, context) {
      contextLines.add(kj::str("context: ", c.file, ": ", c.line, ": ", c.description));
      context = c.next;
    } else {
      break;
    }
  }
  kj::String scratch;
  if (contextLines.size() > 0) {
    scratch = kj::str(description, '\n', kj::strArray(contextLines, "\n"));
    description = scratch;
  }

  builder.setReason(description);
  builder.setType(static_cast<rpc::Exception::Type>(exception.getType()));

  auto details = exception.getDetails();
  if (details.size()) {
    auto detailsBuilder = builder.initDetails(details.size());
    for (auto i : kj::indices(details)) {
      auto out = detailsBuilder[i];
      out.setDetailId(details[i].id);
      out.setData(details[i].value);
    }
  }

  KJ_IF_SOME(t, traceEncoder) {
    builder.setTrace(t(exception));
  }

  if (exception.getType() == kj::Exception::Type::FAILED &&
      !exception.getDescription().startsWith("remote exception:")) {
    KJ_LOG(INFO, "returning failure over rpc", exception);
  }
}

uint exceptionSizeHint(const kj::Exception& exception) {
  return sizeInWords<rpc::Exception>() + exception.getDescription().size() / sizeof(word) + 1;
}

ClientHook::CallHints callHintsFromReader(rpc::Call::Reader reader) {
  ClientHook::CallHints hints;
  hints.noPromisePipelining = reader.getNoPromisePipelining();
  hints.onlyPromisePipeline = reader.getOnlyPromisePipeline();
  return hints;
}

class RpcSystemBrand: public kj::Refcounted {
  // A dummy object, allocated once per RpcSystem, which serves no other purpose than to be the
  // brand pointer for that RpcSystem's ClientHooks. We can't use a pointer to the RpcSystem itself
  // since it can actually be destroyed while the ClientHooks still exist.
};

// =======================================================================================

template <typename Id>
static constexpr Id ONE_WAY_BIT = 1u << (sizeof(Id) * 8 - 1);
template <typename Id>
static constexpr Id REVERSE_ALLOCATED_BIT = 1u << (sizeof(Id) * 8 - 2);
template <typename Id>
static constexpr Id ALL_HIGH_BITS = ONE_WAY_BIT<Id> | REVERSE_ALLOCATED_BIT<Id>;

template <typename Id, typename T>
class ExportTable {
  // Table mapping integers to T, where the integers are chosen locally.

public:
  bool empty() {
    return slots.size() == freeIds.size() && highSlots.size() == 0;
  }

  bool isOneWay(Id id) {
    // Returns whether this ID is in the range of one-way IDs, that is, the creator of the ID does
    // not expect the peer to ever send a message referencing it. In particular, this is used for
    // calls with `onlyPromisePipeline`, in which the caller does not care to receive a `Return`
    // message. Some peers may not understand this hint and may send a `Return` anyway, which can
    // then be ignored. To avoid confusion with such peers, one-way IDs are allocated round-robin
    // to minimize reuse, unlike regular IDs which always use the lowest-numbered available ID.

    return (id & ONE_WAY_BIT<Id>) != 0;
  }

  bool isReverseAllocated(Id id) {
    // Returns whether this ID is in the range that is actually allocated by the "import" side
    // instead of the "export" side. This is used for ThirdPartyAnswer in particular, where the
    // sender needs to inject an entry into the recipient's question table.

    return (id & REVERSE_ALLOCATED_BIT<Id>) != 0;
  }

  kj::Maybe<T&> find(Id id) {
    if (isHigh(id)) {
      return highSlots.find(id);
    } else if (id < slots.size() && slots[id] != nullptr) {
      return slots[id];
    } else {
      return kj::none;
    }
  }

  T erase(Id id, T& entry) {
    // Remove an entry from the table and return it.  We return it so that the caller can be
    // careful to release it (possibly invoking arbitrary destructors) at a time that makes sense.
    // `entry` is a reference to the entry being released -- we require this in order to prove
    // that the caller has already done a find() to check that this entry exists.  We can't check
    // ourselves because the caller may have nullified the entry in the meantime.

    if (isHigh(id)) {
      auto& slot = KJ_REQUIRE_NONNULL(highSlots.findEntry(id));
      return highSlots.release(slot).value;
    } else {
      KJ_DREQUIRE(&entry == &slots[id]);
      T toRelease = kj::mv(slots[id]);
      slots[id] = T();
      freeIds.push(id);
      return toRelease;
    }
  }

  T& next(Id& id) {
    if (freeIds.empty()) {
      id = slots.size();
      KJ_ASSERT(!isHigh(id), "2^30 concurrent questions?!!?!");
      return slots.add();
    } else {
      id = freeIds.top();
      freeIds.pop();
      return slots[id];
    }
  }

  T& nextOneWay(Id& id) {
    // Choose an ID with the top bit set in round-robin fashion, but don't choose an ID that
    // is still in use.

    uint startCounter = oneWayCounter;
    for (;;) {
      id = oneWayCounter++ | ONE_WAY_BIT<Id>;
      if (oneWayCounter == ONE_WAY_BIT<Id>) {
        // Wrap around.
        oneWayCounter = 0;
      }

      bool created = false;
      T& slot = highSlots.findOrCreate(id, [&]() {
        created = true;
        return typename kj::HashMap<Id, T>::Entry { id, T() };
      });

      if (created) {
        return slot;
      }

      KJ_ASSERT(oneWayCounter != startCounter, "All one-way slots used up?");
    }
  }

  T& injectReverseAllocated(Id id) {
    // Create an entry for an ID that was allocated by the peer in the reverse-allocated range.
    KJ_REQUIRE(isReverseAllocated(id), "ID is not in the reverse-allocated range.");
    return highSlots.upsert(id, T(), [&](auto&, auto&&) {
      KJ_FAIL_REQUIRE("Reverse-allocated ID is already in use.");
    }).value;
  }

  template <typename Func>
  void forEach(Func&& func) {
    for (Id i = 0; i < slots.size(); i++) {
      if (slots[i] != nullptr) {
        func(i, slots[i]);
      }
    }
    for (auto& slot: highSlots) {
      func(slot.key, slot.value);
    }
  }

  void release() {
    // Release memory backing the table.
    { auto drop = kj::mv(slots); }
    { auto drop = kj::mv(freeIds); }
    { auto drop = kj::mv(highSlots); }
  }

private:
  kj::Vector<T> slots;
  std::priority_queue<Id, std::vector<Id>, std::greater<Id>> freeIds;

  kj::HashMap<Id, T> highSlots;
  Id oneWayCounter = 0;

  bool isHigh(Id id) {
    // Does this ID belong in `highSlots`?
    return id & ALL_HIGH_BITS<Id>;
  }
};

template <typename Id, typename T>
class ImportTable {
  // Table mapping integers to T, where the integers are chosen remotely.

public:
  bool empty() {
    return presenceBits == 0 && high.size() == 0;
  }

  T& findOrCreate(Id id) {
    // Get an entry, creating it if it doesn't exist.
    if (id < kj::size(low)) {
      presenceBits |= 1 << id;
      return low[id];
    } else {
      return high.findOrCreate(id, [&]() -> typename decltype(high)::Entry { return {id, {}}; });
    }
  }

  kj::Maybe<T&> find(Id id) {
    // Look up an entry, returning null if it doesn't exist.
    if (id < kj::size(low)) {
      if (presenceBits & (1 << id)) {
        return low[id];
      } else {
        return kj::none;
      }
    } else {
      return high.find(id);
    }
  }

  kj::Maybe<T&> create(Id id) {
    // Strictly create a new entry; return null if it already exists.
    if (id < kj::size(low)) {
      if (presenceBits & (1 << id)) {
        return kj::none;
      }
      presenceBits |= 1 << id;
      return low[id];
    } else {
      bool created = false;
      T& result = high.findOrCreate(id, [&]() -> typename decltype(high)::Entry {
        created = true;
        return {id, {}};
      });
      if (created) {
        return result;
      } else {
        return kj::none;
      }
    }
  }

  T erase(Id id) {
    // Remove an entry from the table and return it.  We return it so that the caller can be
    // careful to release it (possibly invoking arbitrary destructors) at a time that makes sense.
    if (id < kj::size(low)) {
      T toRelease = kj::mv(low[id]);
      low[id] = T();
      presenceBits &= ~(1 << id);
      return toRelease;
    } else {
      KJ_IF_SOME(entry, high.findEntry(id)) {
        return high.release(entry).value;
      } else {
        return {};
      }
    }
  }

  T& nextReverseAllocated(Id& id) {
    // We allocate these round-robin not because we have to (unlike one-way IDs) but rather because
    // it's not worth the effort to track unused IDs for reuse, given most connections will see
    // very few of these (and often only at the start of the connection).
    //
    // I also decided to allocate these in descending order on a vague hunch that I might be happy
    // later that I did but honestly I can't present a great argument for it.

    uint startCounter = reverseAllocatedCounter;
    for (;;) {
      id = (REVERSE_ALLOCATED_BIT<Id> - ++reverseAllocatedCounter) | REVERSE_ALLOCATED_BIT<Id>;
      if (reverseAllocatedCounter == REVERSE_ALLOCATED_BIT<Id>) {
        // Wrap around.
        reverseAllocatedCounter = 0;
      }

      bool created = false;
      T& slot = high.findOrCreate(id, [&]() {
        created = true;
        return typename kj::HashMap<Id, T>::Entry { id, T() };
      });

      if (created) {
        return slot;
      }

      KJ_ASSERT(reverseAllocatedCounter != startCounter, "All one-way slots used up?");
    }
  }

  template <typename Func>
  void forEach(Func&& func) {
    for (Id i: kj::indices(low)) {
      if (presenceBits & (1 << i)) {
        func(i, low[i]);
      }
    }
    for (auto& entry: high) {
      func(entry.key, entry.value);
    }
  }

private:
  T low[16];
  uint presenceBits = 0;
  uint reverseAllocatedCounter = 0;
  kj::HashMap<Id, T> high;
};

}  // namespace

// =======================================================================================

class RpcSystemBase::RpcConnectionState final
    : public kj::TaskSet::ErrorHandler, public kj::Refcounted {
public:
  RpcConnectionState(RpcSystemBase::Impl& rpcSystem,
                     BootstrapFactoryBase& bootstrapFactory,
                     kj::Own<VatNetworkBase::Connection>&& connectionParam,
                     size_t flowLimit,
                     kj::Maybe<kj::Function<kj::String(const kj::Exception&)>&> traceEncoder,
                     RpcSystemBrand& brand)
      : bootstrapFactory(bootstrapFactory), flowLimit(flowLimit),
        traceEncoder(traceEncoder), brand(kj::addRef(brand)), tasks(*this) {
    connection.init<Connected>(
        Connected { rpcSystem, kj::mv(connectionParam), kj::heap<kj::Canceler>() });
    tasks.add(messageLoop());
  }

  kj::Own<ClientHook> bootstrap() {
    if (connection.is<Disconnected>()) {
      return newBrokenCap(kj::cp(connection.get<Disconnected>()));
    }

    setNotIdle();

    QuestionId questionId;
    auto& question = questions.next(questionId);

    question.isAwaitingReturn = true;

    auto questionRef = kj::refcounted<QuestionRef>(*this, questionId);
    question.selfRef = *questionRef;

    {
      auto message = connection.get<Connected>().connection->newOutgoingMessage(
          messageSizeHint<rpc::Bootstrap>());

      auto builder = message->getBody().initAs<rpc::Message>().initBootstrap();
      builder.setQuestionId(questionId);
      message->send();
    }

    auto pipeline = kj::refcounted<RpcPipeline>(*this, kj::mv(questionRef), /*willResolve =*/ true);

    return pipeline->getPipelinedCap(kj::Array<PipelineOp>());
  }

  void taskFailed(kj::Exception&& exception) override {
    disconnect(kj::mv(exception));
  }

  void disconnect(kj::Exception&& exception) {
    // Shut down the connection with the given error.
    //
    // This will cancel `tasks`, so cannot be called from inside a task in `tasks`. Instead, use
    // `tasks.add(exception)` to schedule a shutdown, since any error thrown by a task will be
    // passed to `disconnect()` later.

    // After disconnect(), the RpcSystem could be destroyed, making `traceEncoder` a dangling
    // reference, so null it out before we return from here. We don't need it anymore once
    // disconnected anyway.
    KJ_DEFER(traceEncoder = kj::none);

    if (!connection.is<Connected>()) {
      // Already disconnected.
      return;
    }

    kj::Exception networkException(kj::Exception::Type::DISCONNECTED,
        exception.getFile(), exception.getLine(), kj::heapString(exception.getDescription()));

    // Don't throw away the stack trace.
    if (exception.getRemoteTrace() != nullptr) {
      networkException.setRemoteTrace(kj::str(exception.getRemoteTrace()));
    }
    for (void* addr: exception.getStackTrace()) {
      networkException.addTrace(addr);
    }
    // If your stack trace points here, it means that the exception became the reason that the
    // RPC connection was disconnected. The exception was then thrown by all in-flight calls and
    // all future calls on this connection.
    networkException.addTraceHere();

    // Set our connection state to Disconnected now so that no one tries to write any messages to
    // it in their destructors.
    auto& rpcSystem = connection.get<Connected>().rpcSystem;
    auto dyingConnection = kj::mv(connection.get<Connected>().connection);
    auto canceler = kj::mv(connection.get<Connected>().canceler);
    connection.init<Disconnected>(kj::cp(networkException));

    KJ_IF_SOME(newException, kj::runCatchingExceptions([&]() {
      // Carefully pull all the objects out of the tables prior to releasing them because their
      // destructors could come back and mess with the tables.
      kj::Vector<kj::Own<PipelineHook>> pipelinesToRelease;
      kj::Vector<kj::Own<ClientHook>> clientsToRelease;
      kj::Vector<decltype(Answer::task)> tasksToRelease;
      kj::Vector<kj::Promise<void>> resolveOpsToRelease;
      KJ_DEFER(tasks.clear());

      kj::Vector<kj::Own<QuestionRef>> questionsToReject;
      KJ_DEFER({
        for (auto& questionRef: questionsToReject) {
          questionRef->reject(kj::cp(networkException));
        }
      });

      // All current questions complete with exceptions.
      questions.forEach([&](QuestionId id, Question& question) {
        KJ_IF_SOME(questionRef, question.selfRef) {
          // Defer rejection of the questions until we've disconnected everything, so that they
          // don't mess with our tables.
          questionsToReject.add(kj::addRef(questionRef));

          // We need to fully disconnect each QuestionRef otherwise it holds a reference back to
          // the connection state. Meanwhile `tasks` may hold streaming calls that end up holding
          // these QuestionRefs. Technically this is a cyclic reference, but as long as the cycle
          // is broken on disconnect (which happens when the RpcSystem itself is destroyed), then
          // we're OK.
          questionRef.disconnect();
        }
      });
      // Since we've disconnected the QuestionRefs, they won't clean up the questions table for
      // us, so do that here.
      questions.release();

      answers.forEach([&](AnswerId id, Answer& answer) {
        KJ_IF_SOME(p, answer.pipeline) {
          pipelinesToRelease.add(kj::mv(p));
        }

        KJ_IF_SOME(redirect, answer.task.tryGet<Answer::RedirectedToThirdParty>()) {
          KJ_IF_SOME(e, redirect.embargo) {
            // If we disconnected before a `Finish` message was actually received on a three-party
            // redirected call, we should actually fail the embargo. This is important because it's
            // possible some calls never actually made it all the way through, and it would be a
            // violation of e-order to deliver them anyway.
            e->reject(kj::cp(networkException));
          }
        }

        tasksToRelease.add(kj::mv(answer.task));

        KJ_IF_SOME(context, answer.callContext) {
          context.finish();
        }
      });

      exports.forEach([&](ExportId id, Export& exp) {
        clientsToRelease.add(kj::mv(exp.clientHook));
        KJ_IF_SOME(op, exp.resolveOp) {
          resolveOpsToRelease.add(kj::mv(op));
        }
        exp = Export();
      });

      imports.forEach([&](ImportId id, Import& import) {
        KJ_IF_SOME(f, import.promiseClient) {
          f.reject(kj::cp(networkException));
        }
      });

      embargoes.forEach([&](EmbargoId id, Embargo& embargo) {
        KJ_IF_SOME(f, embargo.fulfiller) {
          f->reject(kj::cp(networkException));
        }
      });
    })) {
      // Some destructor must have thrown an exception.  There is no appropriate place to report
      // these errors.
      KJ_LOG(ERROR, "Uncaught exception when destroying capabilities dropped by disconnect.",
             newException);
    }

    // Send an abort message, but ignore failure. Don't send if idle, because we promised not to
    // send any more messages in that case... we'll just disconnect.
    if (!idle) {
      kj::runCatchingExceptions([&]() {
        auto message = dyingConnection->newOutgoingMessage(
            messageSizeHint<void>() + exceptionSizeHint(exception));
        fromException(exception, message->getBody().getAs<rpc::Message>().initAbort());
        message->send();
      });
    }

    // Indicate disconnect.
    auto& dyingConnectionRef = *dyingConnection;
    auto shutdownPromise = dyingConnection->shutdown()
        .attach(kj::mv(dyingConnection))
        .catch_([self = kj::addRef(*this), origException = kj::mv(exception)](
                  kj::Exception&& shutdownException) -> kj::Promise<void> {
          // Don't report disconnects as an error.
          if (shutdownException.getType() == kj::Exception::Type::DISCONNECTED) {
            return kj::READY_NOW;
          }
          // If the error is just what was passed in to disconnect(), don't report it back out
          // since it shouldn't be anything the caller doesn't already know about.
          if (shutdownException.getType() == origException.getType() &&
              shutdownException.getDescription() == origException.getDescription()) {
            return kj::READY_NOW;
          }
          // We are shutting down after receive error, ignore shutdown exception since underlying
          // transport is probably broken.
          if (self->receiveIncomingMessageError) {
            return kj::READY_NOW;
          }
          return kj::mv(shutdownException);
        });
    canceler->cancel(networkException);
    RpcSystemBase::dropConnection(rpcSystem, dyingConnectionRef, kj::mv(shutdownPromise));
  }

  void setFlowLimit(size_t words) {
    flowLimit = words;
    maybeUnblockFlow();
  }

private:
  class RpcClient;
  class ImportClient;
  class PromiseClient;
  class QuestionRef;
  class RpcPipeline;
  class RpcCallContext;
  class RpcResponse;

  // =======================================================================================
  // The Four Tables entry types
  //
  // We have to define these before we can define the class's fields.

  typedef uint32_t QuestionId;
  typedef QuestionId AnswerId;
  typedef uint32_t ExportId;
  typedef ExportId ImportId;
  // See equivalent definitions in rpc.capnp.
  //
  // We always use the type that refers to the local table of the same name.  So e.g. although
  // QuestionId and AnswerId are the same type, we use QuestionId when referring to an entry in
  // the local question table (which corresponds to the peer's answer table) and use AnswerId
  // to refer to an entry in our answer table (which corresponds to the peer's question table).
  // Since all messages in the RPC protocol are defined from the sender's point of view, this
  // means that any time we read an ID from a received message, its type should invert.
  // TODO(cleanup):  Perhaps we could enforce that in a type-safe way?  Hmm...

  using ThirdPartyCompletion = AnyPointer;
  using ThirdPartyToAwait = AnyPointer;
  using ThirdPartyToContact = AnyPointer;
  // Again, see rpc.capnp.
  //
  // Each VatNetwork implementation defines these types, but the RPC implementation must work with
  // any implementation, so can only treat these as AnyPointer. We use these aliases to make the
  // code easier to understand.

  struct Question {
    kj::Array<ExportId> paramExports;
    // List of exports that were sent in the request.  If the response has `releaseParamCaps` these
    // will need to be released.

    kj::Maybe<QuestionRef&> selfRef;
    // The local QuestionRef, set to nullptr when it is destroyed, which is also when `Finish` is
    // sent.

    bool isAwaitingReturn = false;
    // True from when `Call` is sent until `Return` is received.

    bool isTailCall = false;
    // Is this a tail call?  If so, we don't expect to receive results in the `Return`.

    bool skipFinish = false;
    // If true, don't send a Finish message.
    //
    // This is used in two cases:
    // * The `Return` message had the `noFinishNeeded` hint.
    // * Our attempt to send the `Call` threw an exception, therefore the peer never even received
    //   the call in the first place and would not expect a `Finish`.

    inline bool operator==(decltype(nullptr)) const {
      return !isAwaitingReturn && selfRef == kj::none;
    }
  };

  struct ThirdPartyExchangeValue;

  struct Answer {
    Answer() = default;
    Answer(const Answer&) = delete;
    Answer(Answer&&) = default;
    Answer& operator=(Answer&&) = default;
    // If we don't explicitly write all this, we get some stupid error deep in STL.

    kj::Maybe<kj::Own<PipelineHook>> pipeline;
    // Send pipelined calls here.  Becomes null as soon as a `Finish` is received.

    using Running = kj::Promise<void>;
    struct Finished {};
    using RedirectedToSelf = kj::Promise<kj::Own<RpcResponse>>;
    struct RedirectedToThirdParty { kj::Maybe<kj::Own<kj::PromiseFulfiller<void>>> embargo; };
    using Provide = kj::Rc<ThirdPartyExchangeValue>;

    kj::OneOf<Running, Finished, RedirectedToSelf, RedirectedToThirdParty, Provide> task;
    // While the RPC is running locally, `task` is a `Promise` representing the task to execute
    // the RPC.
    //
    // When `Finish` is received (and results are not redirected), `task` becomes `Finished`, which
    // cancels it if it's still running.
    //
    // For locally-redirected calls (Call.sendResultsTo.yourself), this is a promise for the call
    // result, to be picked up by a subsequent `Return`.

    kj::Maybe<RpcCallContext&> callContext;
    // The call context, if it's still active.  Becomes null when the `Return` message is sent.
    // This object, if non-null, is owned by `asyncOp`.

    kj::Array<ExportId> resultExports;
    // List of exports that were sent in the results.  If the finish has `releaseResultCaps` these
    // will need to be released.

    kj::Maybe<kj::Promise<void>> thirdPartyFinishPromise;
    // Specifically for a call that is the result of a three-party call handoff, that is, a call
    // introduced by sending a `ThirdPartyAnswer` message, this promise resolves when the
    // *original* call receives a `Finish` message, thus cleaning up the long path in favor of
    // the shortened path. If a `ThirdPartyAnswerEmbargo` is received, it will consume this
    // promise.
  };

  struct VineInfo {
    // Metadata possibly attached to an export describing the 3PH vine it represents.

    using Provision = kj::Own<QuestionRef>;
    // This is the root of the vine. The `QuestionRef` is the `Provide` operation initiated from
    // this node.

    using Contact = kj::Own<ThirdPartyToContact::Reader>;
    // This vine was passed over one or more connections. The `AnyPointer` contains the
    // `ThirdPartyToContact` value as received to create this import.

    kj::OneOf<Provision, Contact> info;
    // The info is one of the two above things.
  };

  struct Export {
    uint refcount = 0;
    // When this reaches 0, drop `clientHook` and free this export.

    bool canonical = false;
    // If true, this is the canonical export entry for this clientHook, that is,
    // `exportsByCap[clientHook]` points to this entry.

    kj::Own<ClientHook> clientHook;

    kj::Maybe<kj::Promise<void>> resolveOp = kj::none;
    // If this export is a promise (not a settled capability), the `resolveOp` represents the
    // ongoing operation to wait for that promise to resolve and then send a `Resolve` message.

    kj::Maybe<kj::Own<VineInfo>> vineInfo = kj::none;
    // If this export is a 3PH vine, we need to remember some info about it.

    inline bool operator==(decltype(nullptr)) const { return refcount == 0; }
  };

  struct Import {
    Import() = default;
    Import(const Import&) = delete;
    Import(Import&&) = default;
    Import& operator=(Import&&) = default;
    // If we don't explicitly write all this, we get some stupid error deep in STL.

    kj::Maybe<ImportClient&> importClient;
    // Becomes null when the import is destroyed.
    //
    // TODO(cleanup): This comes from when the ImportTable didn't explicitly track which entries
    //   were present, so you had to check whether `importClient` was null. ImportTable now tracks
    //   this itself, so `importClient` no longer needs tobe a `Maybe`. However, it still can't be
    //   a reference since the current ImportTable implementation requires default-constructable
    //   entries. Changing that is more refactoring than I care to do at this moment.

    kj::Maybe<PromiseClient&> promiseClient;
    // If this is a promise, the wrapping PromiseClient.
    // Becomes null when it is discarded *or* when the import is destroyed (e.g. the promise is
    // resolved and the import is no longer needed).
  };

  typedef uint32_t EmbargoId;

  struct Embargo {
    // For handling the `Disembargo` message when looping back to self.

    kj::Maybe<kj::Own<kj::PromiseFulfiller<void>>> fulfiller;
    // Fulfill this when the Disembargo arrives.

    inline bool operator==(decltype(nullptr)) const { return fulfiller == kj::none; }
  };

  // =======================================================================================
  // OK, now we can define RpcConnectionState's member data.

  BootstrapFactoryBase& bootstrapFactory;

  struct Connected {
    RpcSystemBase::Impl& rpcSystem;
    kj::Own<VatNetworkBase::Connection> connection;
    kj::Own<kj::Canceler> canceler;
    // Will be canceled if and when `connection` is changed from `Connected` to `Disconnected`.
  };

  typedef kj::Exception Disconnected;
  kj::OneOf<Connected, Disconnected> connection;
  // Once the connection has failed, we drop it and replace it with an exception, which will be
  // thrown from all further calls.

  ExportTable<ExportId, Export> exports;
  ExportTable<QuestionId, Question> questions;
  ImportTable<AnswerId, Answer> answers;
  ImportTable<ImportId, Import> imports;
  // The Four Tables!
  // The order of the tables is important for correct destruction.

  kj::HashMap<ClientHook*, ExportId> exportsByCap;
  // Maps already-exported ClientHook objects to their ID in the export table.

  ExportTable<EmbargoId, Embargo> embargoes;
  // There are only four tables.  This definitely isn't a fifth table.  I don't know what you're
  // talking about.

  size_t flowLimit;
  size_t callWordsInFlight = 0;

  kj::Maybe<kj::Own<kj::PromiseFulfiller<void>>> flowWaiter;
  // If non-null, we're currently blocking incoming messages waiting for callWordsInFlight to drop
  // below flowLimit. Fulfill this to un-block.

  kj::Maybe<kj::Function<kj::String(const kj::Exception&)>&> traceEncoder;

  kj::Own<RpcSystemBrand> brand;

  kj::TaskSet tasks;

  bool gotReturnForHighQuestionId = false;
  // Becomes true if we ever get a `Return` message for a high question ID (with top bit set),
  // which we use in cases where we've hinted to the peer that we don't want a `Return`. If the
  // peer sends us one anyway then it seemingly does not implement our hints. We need to stop
  // using the hints in this case before the high question ID space wraps around since otherwise
  // we might reuse an ID that the peer thinks is still in use.

  bool sentCapabilitiesInPipelineOnlyCall = false;
  // Becomes true if `sendPipelineOnly()` is ever called with parameters that include capabilities.

  bool receiveIncomingMessageError = false;
  // Becomes true when receiveIncomingMessage resulted in exception.

  bool idle = true;
  // What was the last value passed to setIdle() on the underlying connection? Used to avoid
  // redundant calls. Initialized to true because we are expected to call setIdle(false) at
  // startup.

  // =====================================================================================
  // Idle handling

  void setNotIdle() {
    // Inform the VatNetwork that this connection is NOT idle, unless we've already done so.

    if (idle) {
      idle = false;
      KJ_IF_SOME(c, connection.tryGet<Connected>()) {
        c.connection->setIdle(false);
      }
    }
  }

  void checkIfBecameIdle() {
    // Checks if the connection has become idle, and if so, informs the VatNetwork by calling
    // setIdle(true). Generally, this must be called after erasing an entry from any of the
    // Four Tables.

    if (idle) return;  // already idle

    bool allTablesEmpty =
        questions.empty() && answers.empty() && exports.empty() && imports.empty() &&
        // Technically the embargoes table should always be empty if the others are, but it's not
        // expensive to check it.
        embargoes.empty();

    if (!allTablesEmpty) {
      // Not idle, don't do anything.
      return;
    }

    // Notes:
    // - `exportsByCap` is a reverse mapping of `exports` so should be empty if `exports` is empty.
    // - `tasks` only contains:
    //   (a) messageLoop()
    //   (b) instances of flowController->waitAllAcked(), which are meaningless if there are no
    //       calls outstanding, and
    //   (c) exceptions inserted specifically to break the connection
    //   Of these, only messageLoop() could cause the connection to become non-idle again, but
    //   that's a well-defined part of the setIdle() contract.
    // - `flowWaiter` is only non-null when a large number of calls are outstanding; if the tables
    //   are empty, it is null.
    //
    // Hence we know at this point that no further messages will be sent on this connection, unless
    // the app initiates a new bootstrap or a message is received. I.e., we are idle.

    KJ_ASSERT(exportsByCap.size() == 0);
    KJ_ASSERT(flowWaiter == kj::none);

    // OK, we can inform the VatNetwork of the idleness.
    idle = true;
    KJ_IF_SOME(c, connection.tryGet<Connected>()) {
      c.connection->setIdle(true);
    }
  }

  void waitAllAckedInBackground(kj::Own<RpcFlowController> flowController) {
    // Calls `waitAllAcked()` on `flowController` as a background task. This is needed when an
    // `RpcFlowController` is being abandoned but we don't actually intend to cancel the calls
    // that were made on it.

    // If we're disconnected, there's no need to wait for anything, so we'll just drop the
    // flowController in that case.
    if (connection.is<Connected>()) {
      tasks.add(flowController->waitAllAcked().attach(kj::mv(flowController)));
    }
  }

  // =====================================================================================
  // ClientHook implementations

  class RpcClient: public ClientHook, public kj::Refcounted {
  public:
    RpcClient(RpcConnectionState& connectionState)
        : ClientHook(connectionState.brand.get()),
          connectionState(kj::addRef(connectionState)) {}

    ~RpcClient() noexcept(false) {
      KJ_IF_SOME(f, this->flowController) {
        // Destroying the client should not cancel outstanding streaming calls.
        // TODO(someday): Is this right? If the application didn't bother to make and await a
        //   non-streaming call after the streaming calls, then that suggests the app doesn't
        //   really care if the streaming calls succeeded or not, which in turn suggests the app
        //   is dropping the stream abruptly because it wants to abort it, in which case we
        //   probably should cancel the calls. But at this very moment I'm not prepared to do the
        //   testing necessary to change this...
        connectionState->waitAllAckedInBackground(kj::mv(f));
      }
    }

    bool isOnConnection(RpcConnectionState& expected) {
      return connectionState.get() == &expected;
    }

    virtual void promiseResolvedToThis(bool receivedCall) {}
    // Called to indicate that an RPC promise was resolved (via a `Resolve` message we received)
    // to point to this client (i.e. the `CapDescriptor` for the resolution pointed to another
    // import table entry). `receivedCall` is true if the resolving promise had received calls
    // in the past, which had been pipelined. Some RpcClient derivatives need to know when this
    // happens in order to arrange to use embargoes where appropriate. Others don't care, so can
    // use the default implementation.

    struct WriteDescriptorResult {
      kj::Maybe<ExportId> exportId;
      // If writing the descriptor adds a new export to the export table, or increments the refcount
      // on an existing one, then this indicates the export ID. The caller is responsible for
      // ensuring the refcount is decremented when this descriptor is released.

      ClientHook& described;
      // The underlying ClientHook which this descriptor actually described. In particular, if the
      // capability turns out to be an RPC PromiseClient which is not yet resolved, then
      // `described` will point at the temporary object that represents the promise pipeline.
      // This client will not resolve later when the promise resolves; it'll keep going to the
      // pipeline.
      //
      // This is needed to handle the Tribble 4-way race condition. If some other promise resolved
      // to *this* promise, then we need to replace the original promise's export table entry
      // with a direct link to the new promise's pipeline, which cannot further resolve.
      // `described` is the correct capability to use for this purpose.
    };

    virtual WriteDescriptorResult writeDescriptor(rpc::CapDescriptor::Builder descriptor,
                                                  kj::Vector<int>& fds) = 0;
    // Writes a CapDescriptor referencing this client.  The CapDescriptor must be sent as part of
    // the very next message sent on the connection, as it may become invalid if other things
    // happen.

    virtual kj::Maybe<kj::Own<ClientHook>> writeTarget(
        rpc::MessageTarget::Builder target) = 0;
    // Writes the appropriate call target for calls to this capability and returns null.
    //
    // - OR -
    //
    // If calls have been redirected to some other local ClientHook, returns that hook instead.
    // This can happen if the capability represents a promise that has been resolved.

    virtual void adoptFlowController(kj::Own<RpcFlowController> flowController) {
      // Called when a PromiseClient resolves to another RpcClient. If streaming calls were
      // outstanding on the old client, we'd like to keep using the same FlowController on the new
      // client, so as to keep the flow steady.

      if (this->flowController == kj::none) {
        // We don't have any existing flowController so we can adopt this one, yay!
        this->flowController = kj::mv(flowController);
      } else {
        // Apparently, there is an existing flowController. This is an unusual scenario: Apparently
        // we had two stream capabilities, we were streaming to both of them, and they later
        // resolved to the same capability. This probably never happens because streaming use cases
        // normally call for there to be only one client. But, it's certainly possible, and we need
        // to handle it. We'll do the conservative thing and just make sure that all the calls
        // finish. This may mean we'll over-buffer temporarily; oh well.
        connectionState->waitAllAckedInBackground(kj::mv(flowController));
      }
    }

    struct VineToExport {
      kj::Own<ClientHook> vine;
      // Capability to export as the "vine" in the ThirdPartyCapDescriptor.

      kj::Maybe<kj::Own<VineInfo>> vineInfo;
      // VineInfo to attach to the vine export.
    };

    using WriteThirdPartyDescriptorResult = kj::OneOf<VineToExport, kj::Own<ClientHook>>;
    // If a three-party handoff was successfully initiated, returns VineToExport.
    //
    // If this is a ClientHook instead, then it was discovered while attempting to write the
    // descriptor that some other capability should be described instead, so nothing was written.
    // In this case, the caller should proceed by attempting to write that capability's descriptor
    // instead.

    virtual WriteThirdPartyDescriptorResult writeThirdPartyDescriptor(
          VatNetworkBase::Connection& provider,
          VatNetworkBase::Connection& recipient,
          ThirdPartyToContact::Builder contact) {
      // Called to fill in a ThirdPartyCapDescriptor when sending this capability to a different
      // connection.
      //
      // `provider` is this object's backing connection; the caller has already verified it is
      // currently connected. (Calling connectionState->tryGetConnection() would return the same
      // object.)
      //
      // `recipient` is the connection receiving the ThirdPartyCapDescriptor.
      //
      // `contact` is the ThirdPartyToContact which goes in the descriptor.
      //
      // Returns the ClientHook which should be used as the "vine". This needs to be callable as
      // a stand-in for the object. It also needs to hold open the `Provide` operation until the
      // returned reference is dropped. Note that `addRef()`s on the returned reference do NOT have
      // to hold open the Provide; the Provide can be held open by an attachment on the ref itself.
      //
      // The default implementation of this method is appropriate most of the time. It is
      // overridded for the specific case of `DeferredThirdPartyClient`.

      // Send `Provide` message to our connection.
      QuestionId questionId;
      auto& question = connectionState->questions.nextOneWay(questionId);
      question.isAwaitingReturn = false;  // No Return needed
      auto questionRef = kj::refcounted<QuestionRef>(*connectionState, questionId);
      question.selfRef = *questionRef;

      auto otherMsg = provider.newOutgoingMessage(messageSizeHint<rpc::Provide>() + 32);
      auto provide = otherMsg->getBody().initAs<rpc::Message>().initProvide();
      provide.setQuestionId(questionId);
      KJ_IF_SOME(redirect, writeTarget(provide.initTarget())) {
        // Oops, this capability was redirected. This *shouldn't* happen since the immediate
        // caller of writeThirdPartyDescriptor() would have already followed redirects, but as it
        // happens, if we simply return the redirect here (without filling in `contact`), we are
        // honoring our contract.
        return kj::mv(redirect);
      }
      recipient.introduceTo(provider, ThreePartyHandoffPurpose::CAPABILITY_PASSING, contact,
                            provide.initRecipient());
      otherMsg->send();

      return VineToExport {
        .vine = addRef(),
        .vineInfo = kj::heap(VineInfo { .info = kj::mv(questionRef) })
      };
    }

    // implements ClientHook -----------------------------------------

    virtual Request<AnyPointer, AnyPointer> newCall(
        uint64_t interfaceId, uint16_t methodId, kj::Maybe<MessageSize> sizeHint,
        CallHints hints) override {
      // NOTE: Some subclasses (PromiseClient, DeferredThirdPartyClient) further override this.
      return newCallNoIntercept(interfaceId, methodId, sizeHint, hints);
    }

    Request<AnyPointer, AnyPointer> newCallNoIntercept(
        uint64_t interfaceId, uint16_t methodId, kj::Maybe<MessageSize> sizeHint,
        CallHints hints) {
      if (!connectionState->connection.is<Connected>()) {
        return newBrokenRequest(kj::cp(connectionState->connection.get<Disconnected>()), sizeHint);
      }

      auto request = kj::heap<RpcRequest>(
          *connectionState, *connectionState->connection.get<Connected>().connection,
          sizeHint, kj::addRef(*this));
      auto callBuilder = request->getCall();

      callBuilder.setInterfaceId(interfaceId);
      callBuilder.setMethodId(methodId);
      callBuilder.setNoPromisePipelining(hints.noPromisePipelining);
      callBuilder.setOnlyPromisePipeline(hints.onlyPromisePipeline);

      auto root = request->getRoot();
      return Request<AnyPointer, AnyPointer>(root, kj::mv(request));
    }

    virtual VoidPromiseAndPipeline call(
        uint64_t interfaceId, uint16_t methodId,
        kj::Own<CallContextHook>&& context, CallHints hints) override {
      // NOTE: Some subclasses (PromiseClient, DeferredThirdPartyClient) further override this.
      return callNoIntercept(interfaceId, methodId, kj::mv(context), hints);
    }

    VoidPromiseAndPipeline callNoIntercept(uint64_t interfaceId, uint16_t methodId,
                                           kj::Own<CallContextHook>&& context, CallHints hints) {
      // Implement call() by copying params and results messages.

      auto params = context->getParams();
      auto request = newCallNoIntercept(interfaceId, methodId, params.targetSize(), hints);

      request.set(params);
      context->releaseParams();

      return context->directTailCall(RequestHook::from(kj::mv(request)));
    }

    kj::Own<ClientHook> addRef() override {
      return kj::addRef(*this);
    }

    kj::Own<RpcConnectionState> connectionState;

    kj::Maybe<kj::Own<RpcFlowController>> flowController;
    // Becomes non-null the first time a streaming call is made on this capability.
  };

  class ImportClient final: public RpcClient {
    // A ClientHook that wraps an entry in the import table.

  public:
    ImportClient(RpcConnectionState& connectionState, ImportId importId,
                 kj::Maybe<kj::OwnFd> fd)
        : RpcClient(connectionState), importId(importId), fd(kj::mv(fd)) {}

    ~ImportClient() noexcept(false) {
      unwindDetector.catchExceptionsIfUnwinding([&]() {
        // Remove self from the import table, if the table is still pointing at us.
        KJ_IF_SOME(import, connectionState->imports.find(importId)) {
          KJ_IF_SOME(i, import.importClient) {
            if (&i == this) {
              connectionState->imports.erase(importId);
            }
          }
        }

        // Send a message releasing our remote references.
        if (remoteRefcount > 0 && connectionState->connection.is<Connected>()) {
          auto message = connectionState->connection.get<Connected>().connection
              ->newOutgoingMessage(messageSizeHint<rpc::Release>());
          rpc::Release::Builder builder = message->getBody().initAs<rpc::Message>().initRelease();
          builder.setId(importId);
          builder.setReferenceCount(remoteRefcount);
          message->send();
        }

        connectionState->checkIfBecameIdle();
      });
    }

    ImportId getImportId() { return importId; }

    void setFdIfMissing(kj::Maybe<kj::OwnFd> newFd) {
      if (fd == kj::none) {
        fd = kj::mv(newFd);
      }
    }

    void addRemoteRef() {
      // Add a new RemoteRef and return a new ref to this client representing it.
      ++remoteRefcount;
    }

    WriteDescriptorResult writeDescriptor(rpc::CapDescriptor::Builder descriptor,
                                          kj::Vector<int>& fds) override {
      descriptor.setReceiverHosted(importId);
      return {
        .exportId = kj::none,
        .described = *this,
      };
    }

    kj::Maybe<kj::Own<ClientHook>> writeTarget(
        rpc::MessageTarget::Builder target) override {
      target.setImportedCap(importId);
      return kj::none;
    }

    // implements ClientHook -----------------------------------------

    kj::Maybe<ClientHook&> getResolved() override {
      return kj::none;
    }

    kj::Maybe<kj::Promise<kj::Own<ClientHook>>> whenMoreResolved() override {
      return kj::none;
    }

    kj::Maybe<int> getFd() override {
      return fd.map([](auto& f) { return f.get(); });
    }

  private:
    ImportId importId;
    kj::Maybe<kj::OwnFd> fd;

    uint remoteRefcount = 0;
    // Number of times we've received this import from the peer.

    kj::UnwindDetector unwindDetector;
  };

  class PipelineClient final: public RpcClient {
    // A ClientHook representing a pipelined promise.  Always wrapped in PromiseClient.

  public:
    PipelineClient(RpcConnectionState& connectionState,
                   kj::Own<QuestionRef>&& questionRef,
                   kj::Array<PipelineOp>&& ops)
        : RpcClient(connectionState), questionRef(kj::mv(questionRef)), ops(kj::mv(ops)) {}

    QuestionRef& getQuestionRef() { return *questionRef; }
    kj::ArrayPtr<const PipelineOp> getPipelineOps() { return ops; }

    WriteDescriptorResult writeDescriptor(rpc::CapDescriptor::Builder descriptor,
                                          kj::Vector<int>& fds) override {
      auto promisedAnswer = descriptor.initReceiverAnswer();
      promisedAnswer.setQuestionId(questionRef->getId());
      promisedAnswer.adoptTransform(fromPipelineOps(
          Orphanage::getForMessageContaining(descriptor), ops));
      return {
        .exportId = kj::none,
        .described = *this,
      };
    }

    kj::Maybe<kj::Own<ClientHook>> writeTarget(
        rpc::MessageTarget::Builder target) override {
      auto builder = target.initPromisedAnswer();
      builder.setQuestionId(questionRef->getId());
      builder.adoptTransform(fromPipelineOps(Orphanage::getForMessageContaining(builder), ops));
      return kj::none;
    }

    // implements ClientHook -----------------------------------------

    kj::Maybe<ClientHook&> getResolved() override {
      return kj::none;
    }

    kj::Maybe<kj::Promise<kj::Own<ClientHook>>> whenMoreResolved() override {
      return kj::none;
    }

    kj::Maybe<int> getFd() override {
      return kj::none;
    }

  private:
    kj::Own<QuestionRef> questionRef;
    kj::Array<PipelineOp> ops;
  };

  class PromiseClient final: public RpcClient {
    // A ClientHook that initially wraps one client (in practice, an ImportClient or a
    // PipelineClient) and then, later on, redirects to some other client.

  public:
    PromiseClient(RpcConnectionState& connectionState, kj::Own<ImportClient> initial)
        : RpcClient(connectionState), cap(kj::mv(initial)), status(IMPORT) {}
    PromiseClient(RpcConnectionState& connectionState, kj::Own<PipelineClient> initial)
        : RpcClient(connectionState), cap(kj::mv(initial)), status(PIPELINE) {
      kj::downcast<PipelineClient>(*cap).getQuestionRef().addPromiseClient(*this);
    }

    ~PromiseClient() noexcept(false) {
      unlink();
    }

    kj::ListLink<PromiseClient> link;

    bool hasReceivedCall() { return receivedCall; }

    void resolveImport(kj::Own<ClientHook> replacement) {
      KJ_ASSERT(status == IMPORT, "peer committed RPC protocol error: resolved a promise that "
          "was already resolved");
      resolve(kj::mv(replacement));
    }

    void resolveQuestion(AnyPointer::Reader results) {
      KJ_REQUIRE(status == PIPELINE);

      // Annoyingly, getPipelinedCap() can throw an exception if the message had the wrong type
      // of pointer in the slot. We'd rather that turn into a broken promise than kill the
      // connection.
      KJ_IF_SOME(exception, kj::runCatchingExceptions([&]() {
        resolve(results.getPipelinedCap(kj::downcast<PipelineClient>(*cap).getPipelineOps()));
      })) {
        reject(kj::mv(exception));
      }
    }

    void resolveQuestion(PipelineHook& newPipeline) {
      KJ_REQUIRE(status == PIPELINE);
      resolve(newPipeline.getPipelinedCap(kj::downcast<PipelineClient>(*cap).getPipelineOps()));
    }

    void reject(kj::Exception&& e) {
      if (status != RESOLVED) {
        resolve(newBrokenCap(kj::mv(e)));
      }
    }

    void promiseResolvedToThis(bool receivedCall) override {
      // Some other promise resolved to *this* promise.
      KJ_REQUIRE(status != RESOLVED,
          "peer committed RPC protocol error: a promise cannot resolve to another promise "
          "where the latter promise is itself already resolved; see "
          "`CapDescriptor.senderPromise`.");
      this->receivedCall = this->receivedCall || receivedCall;
    }

    WriteDescriptorResult writeDescriptor(rpc::CapDescriptor::Builder descriptor,
                                          kj::Vector<int>& fds) override {
      // TODO(now): Setting receivedCall = true seems wrong here, writing a descriptor does not
      //   imply that the capability is being called, and so does not imply that an embargo is
      //   needed when the capability is resolved.
      receivedCall = true;
      return connectionState->writeDescriptor(*cap, descriptor, fds);
    }

    kj::Maybe<kj::Own<ClientHook>> writeTarget(
        rpc::MessageTarget::Builder target) override {
      receivedCall = true;
      return connectionState->writeTarget(*cap, target);
    }

    void adoptFlowController(kj::Own<RpcFlowController> flowController) override {
      KJ_IF_SOME(rpcCap, connectionState->unwrapIfSameConnection(*cap)) {
        // Pass the flow controller on to our inner cap.
        rpcCap.adoptFlowController(kj::mv(flowController));
      } else {
        // We resolved to a capability that isn't another RPC capability. We should simply make
        // sure that all the calls complete.
        connectionState->waitAllAckedInBackground(kj::mv(flowController));
      }
    }

    // implements ClientHook -----------------------------------------

    Request<AnyPointer, AnyPointer> newCall(
        uint64_t interfaceId, uint16_t methodId, kj::Maybe<MessageSize> sizeHint,
        CallHints hints) override {
      if (status == RESOLVED) {
        // Go directly to the resolved capability.
        //
        // If we don't do this now, then we'll return an RpcRequest which, as soon as send() is
        // called, may discover that it's not sending to an RPC capability after all, and will
        // just have to call `newCall()` on the inner capability at that point and copy the request
        // over. Better to redirect now and avoid the copy later.
        return cap->newCall(interfaceId, methodId, sizeHint, hints);
      }

      receivedCall = true;

      // IMPORTANT: We must call our superclass's version of newCall(), NOT cap->newCall(), because
      //   the Request object we create needs to check at send() time whether the promise has
      //   resolved and, if so, redirect to the new target.
      return RpcClient::newCall(interfaceId, methodId, sizeHint, hints);
    }

    VoidPromiseAndPipeline call(uint64_t interfaceId, uint16_t methodId,
                                kj::Own<CallContextHook>&& context, CallHints hints) override {
      // Unlike newCall(), call() always goes directly to the underlying cap, so we don't need an
      // `isResolved()` check. Note that `receivedCall = true` doesn't do anything if we're already
      // resolved.

      receivedCall = true;
      return cap->call(interfaceId, methodId, kj::mv(context), hints);
    }

    kj::Maybe<ClientHook&> getResolved() override {
      if (status == RESOLVED) {
        return *cap;
      } else {
        return kj::none;
      }
    }

    kj::Maybe<kj::Promise<kj::Own<ClientHook>>> whenMoreResolved() override {
      if (status == RESOLVED) {
        return kj::Promise<kj::Own<ClientHook>>(cap->addRef());
      } else KJ_IF_SOME(w, resolutionWaiter) {
        return w.promise.addBranch();
      } else {
        auto paf = kj::newPromiseAndFulfiller<kj::Own<ClientHook>>();
        return resolutionWaiter.emplace(ResolutionWaiter {
          .promise = paf.promise.fork(),
          .fulfiller = kj::mv(paf.fulfiller),
        }).promise.addBranch();
      }
    }

    kj::Maybe<int> getFd() override {
      if (status == RESOLVED) {
        return cap->getFd();
      } else {
        // In theory, before resolution, the ImportClient for the promise could have an FD
        // attached, if the promise itself was presented with an attached FD. However, we can't
        // really return that one here because it may be closed when we get the Resolve message
        // later. In theory we could have the PromiseClient itself take ownership of an FD that
        // arrived attached to a promise cap, but the use case for that is questionable. I'm
        // keeping it simple for now.
        return kj::none;
      }
    }

  private:
    kj::Own<ClientHook> cap;

    struct ResolutionWaiter {
      kj::ForkedPromise<kj::Own<ClientHook>> promise;
      kj::Own<kj::PromiseFulfiller<kj::Own<ClientHook>>> fulfiller;
    };
    kj::Maybe<ResolutionWaiter> resolutionWaiter;

    bool receivedCall = false;

    enum {
      IMPORT,     // cap is an ImportClient
      PIPELINE,   // cap is a PipelineClient
      RESOLVED,   // we've resolved, cap is somethingh else
    } status;

    void resolve(kj::Own<ClientHook> replacement) {
      KJ_ASSERT(replacement.get() != this, "can't resolve promise to itself");

      bool isSameConnection = false;
      bool needsEmbargo = false;
      KJ_IF_SOME(rpcReplacement, connectionState->unwrapIfSameNetwork(*replacement)) {
        isSameConnection = rpcReplacement.isOnConnection(*connectionState);

        // We resolved to some other RPC capability.
        //
        // If any embargo is needed, it will be handled elsewhere. However, whoever is handling it
        // may need to know that a promise that received calls in the past has resolved to it.
        // So, notify it.
        rpcReplacement.promiseResolvedToThis(receivedCall);
      } else {
        if (replacement->isNull() || replacement->isError()) {
          // Null or broken capabilities appear to be local capabilities, but they aren't really.
          // Rather, they are capabilities that are "passed by value". Sending a disembargo to them
          // wouldn't work since the other end doesn't see these capabilities as pointing back to
          // us so wouldn't reflect the disembargo. Luckily, there's no need: call ordering is
          // irrelevant for these since they are just going to reject all calls anyway, so we
          // do not need to worry about embargoing them.
        } else {
          needsEmbargo = receivedCall;
        }
      }

      // If the promise was used for streaming calls, it will have a `flowController` that might
      // still be shepherding those calls. We'll need make sure that it doesn't get thrown away.
      //
      // NOTE: An older version of this code pulled the flow controller off of `*cap` (the original
      //   underlying capability) instead of using our own. This was actually wrong because calls
      //   on the `PromiseClient` will use the `PromiseClient`'s own `flowController` and not the
      //   one on the underlying pre-resolution capability.
      // TODO(cleanup): It seems awkward that both the promise and the thing it is wrapping can
      //   have separate flow controllers?
      KJ_IF_SOME(f, flowController) {
        if (isSameConnection) {
          // The new target is on the same connection. It would make a lot of sense to keep using
          // the same flow controller if possible.
          kj::downcast<RpcClient>(*replacement).adoptFlowController(kj::mv(f));
        } else {
          // The new target is something else. The best we can do is wait for the controller to
          // drain. New calls will be flow-controlled in a new way without knowing about the old
          // controller.
          connectionState->waitAllAckedInBackground(kj::mv(f));
        }
      }

      if (needsEmbargo && connectionState->connection.is<Connected>()) {
        // The new capability is hosted locally, not on the remote machine.  And, we had made calls
        // to the promise.  We need to make sure those calls echo back to us before we allow new
        // calls to go directly to the local capability, so we need to set a local embargo and send
        // a `Disembargo` to echo through the peer.

        auto message = connectionState->connection.get<Connected>().connection->newOutgoingMessage(
            messageSizeHint<rpc::Disembargo>() + MESSAGE_TARGET_SIZE_HINT);

        auto disembargo = message->getBody().initAs<rpc::Message>().initDisembargo();

        {
          auto redirect = connectionState->writeTarget(*cap, disembargo.initTarget());
          KJ_ASSERT(redirect == kj::none,
                    "Original promise target should always be from this RPC connection.");
        }

        EmbargoId embargoId;
        Embargo& embargo = connectionState->embargoes.next(embargoId);

        disembargo.getContext().setSenderLoopback(embargoId);

        auto paf = kj::newPromiseAndFulfiller<void>();
        embargo.fulfiller = kj::mv(paf.fulfiller);

        // Make a promise which resolves to `replacement` as soon as the `Disembargo` comes back.
        auto embargoPromise = paf.promise.then([replacement = kj::mv(replacement)]() mutable {
          return kj::mv(replacement);
        });

        // We need to queue up calls in the meantime, so we'll resolve ourselves to a local promise
        // client instead.
        replacement = newLocalPromiseClient(kj::mv(embargoPromise));

        // Send the `Disembargo`.
        message->send();
      }

      KJ_IF_SOME(w, resolutionWaiter) {
        w.fulfiller->fulfill(replacement->addRef());
      }

      unlink();
      status = RESOLVED;
      cap = kj::mv(replacement);
    }

    void unlink() {
      switch (status) {
        case IMPORT: {
          // This object is representing an import promise.  That means the import table may still
          // contain a pointer back to it.  Remove that pointer.  Note that we have to verify that
          // the import still exists and the pointer still points back to this object because this
          // object may actually outlive the import.
          ImportId id = kj::downcast<ImportClient>(*cap).getImportId();
          KJ_IF_SOME(import, connectionState->imports.find(id)) {
            KJ_IF_SOME(c, import.promiseClient) {
              if (&c == this) {
                import.promiseClient = kj::none;
              }
            }
          }
          break;
        }
        case PIPELINE: {
          kj::downcast<PipelineClient>(*cap).getQuestionRef().removePromiseClient(*this);
          break;
        }
        case RESOLVED:
          // nothing special to do
          break;
      }
    }
  };

  class DeferredThirdPartyClient: public RpcClient {
    // Client constructed from a ThirdPartyCapDescriptor which we have not actually accepted yet.
    // We don't actually attempt to send an `Accept` message to the third party until the
    // application attempts to use the capability. This way, if we merely end up forwarding the
    // cap to another party without using it at all, we don't need to form our own connection to
    // the host of the capability.

  public:
    DeferredThirdPartyClient(RpcConnectionState& connectionState,
        kj::Own<ThirdPartyToContact::Reader> contact, kj::Own<RpcClient> vine)
        : RpcClient(connectionState), state(Deferred {kj::mv(contact), kj::mv(vine)}) {}

    void promiseResolvedToThis(bool receivedCall) override {
      if (receivedCall) {
        // A promise resolved to us, and it had received calls that were pipelined. If we end up
        // accepting this capability locally, we will need to use an embargo.
        needEmbargo = true;

        // promiseResolvedToThis() is always called immediately after the resolution capability was
        // received off the wire, so there should not have been a chance for it to have been
        // accepted yet. This is good, because we need to make sure we embargo it on accept.
        //
        // Note that we know for sure that the resolution message could only have created a new
        // `DeferredThirdPartyClient` -- not referred to an existing one -- because we do not
        // actually ever export a `DeferredThirdPartyClient`, we only export its underlying vine
        // (when forwarding), or the fully-accepted third-party capability. If a peer refers back
        // to the vine itself, we always construct a *new* DeferredThirdPartyClient instance when
        // that vine reference is received.
        //
        // It's good that this all works out, because otherwise we'd probably need to change the
        // embargo protocol so that an embargo could be established *after* accepting the
        // capability.
        KJ_ASSERT(state.is<Deferred>());
      }
    }

    WriteDescriptorResult writeDescriptor(rpc::CapDescriptor::Builder descriptor,
                                          kj::Vector<int>& fds) override {
      KJ_SWITCH_ONEOF(state) {
        KJ_CASE_ONEOF(deferred, Deferred) {
          // A ThirdPartyCapDescriptor was sent to us, and we are sending it *back* over the same
          // connection we received it from.
          //
          // The easiest way to handle this is to send back the vine, which after all points to
          // the capability that the other side originally sent us.
          return deferred.vine->writeDescriptor(descriptor, fds);
        }
        KJ_CASE_ONEOF(cap, kj::Own<ClientHook>) {
          return connectionState->writeDescriptor(*cap, descriptor, fds);
        }
      }
      KJ_UNREACHABLE;
    }

    kj::Maybe<kj::Own<ClientHook>> writeTarget(
        rpc::MessageTarget::Builder target) override {
      // Someone is addressing a message to this capability. We probably need to accept it and
      // have them address the accepted capability instead.
      return connectionState->writeTarget(ensureAccepted(), target);
    }

    // NOTE: We don't need to implement adoptFlowController() since flow controllers are tied to
    //   specific connections, so presumably the old flow controller won't be able to be adopted
    //   by the new capability on the new connection anyway. Additionally, it just makes sense to
    //   recompute the flow from scratch since the new connection probably has different latency
    //   and throughput.

    WriteThirdPartyDescriptorResult writeThirdPartyDescriptor(
          VatNetworkBase::Connection& provider,
          VatNetworkBase::Connection& recipient,
          ThirdPartyToContact::Builder contact) override {
      if (state.is<Deferred>() &&
          provider.canForwardThirdPartyToContact(
              *state.get<Deferred>().contact, recipient,
              ThreePartyHandoffPurpose::CAPABILITY_PASSING)) {
        // We have not accepted this capability yet, and the VatNetwork supports forwarding it!
        // We can get away without accepting the capability.
        auto& deferred = state.get<Deferred>();
        provider.forwardThirdPartyToContact(
            *deferred.contact, recipient,
            ThreePartyHandoffPurpose::CAPABILITY_PASSING, contact);

        return VineToExport {
          .vine = deferred.vine->addRef(),
          .vineInfo = kj::heap(VineInfo {.info = capnp::clone(*deferred.contact)}),
        };
      } else {
        // Either:
        // a. We already accepted this cap, so we should treat this as a normal three-party handoff
        //    and hand off the cap we accepted. (Actually, this doesn't happen in practice because
        //    the caller would already have resolved to the inner capability in this case.)
        // b. The VatNetwork won't allow forwarding, so we must accept the cap first, and then,
        //    again, hand off the accepted cap.
        auto& hook = ensureAccepted();
        KJ_IF_SOME(rpcHook, connectionState->unwrapIfSameNetwork(hook)) {
          KJ_SWITCH_ONEOF(rpcHook.connectionState->connection) {
            KJ_CASE_ONEOF(connected, Connected) {
              // Common case: the accepted capability is on its own connection, which we're still
              // connected to.
              return rpcHook.writeThirdPartyDescriptor(*connected.connection, recipient, contact);
            }
            KJ_CASE_ONEOF(error, Disconnected) {
              // We accepted the capability but the connection we accepted it on is now dead.
              // Don't fill in `contact` and just return a broken cap.
              return newBrokenCap(kj::cp(error));
            }
          }
          KJ_UNREACHABLE;
        } else {
          // Apparently, when we accepted the capability, it turned out to be a self-introduction
          // and now we're left with a capability pointing into our own vat. We need to return
          // that instead.
          return hook.addRef();
        }
      }
    }

    // implements ClientHook -----------------------------------------

    Request<AnyPointer, AnyPointer> newCall(
        uint64_t interfaceId, uint16_t methodId, kj::Maybe<MessageSize> sizeHint,
        CallHints hints) override {
      // If we receive a call, then we should immediately accept the capability in order to
      // dispatch it. (Note that if we didn't override this, then writeTarget() would eventually
      // be called and would accept the capability there, but at that point we've already
      // constructed the requst and would have to redirect it, requiring a copy.)
      return ensureAccepted().newCall(interfaceId, methodId, sizeHint, hints);
    }

    VoidPromiseAndPipeline call(uint64_t interfaceId, uint16_t methodId,
                                kj::Own<CallContextHook>&& context, CallHints hints) override {
      // Same here, when we receive a call we always accept the capability immediately.
      return ensureAccepted().call(interfaceId, methodId, kj::mv(context), hints);
    }

    kj::Maybe<ClientHook&> getResolved() override {
      // This is tricky: Although whenMoreResolved() triggers accept, getResolved() shouldn't,
      // because lots of stuff calls getResolved() optimistically and it doesn't necessarily mean
      // the caller wants to use the capability.
      KJ_SWITCH_ONEOF(state) {
        KJ_CASE_ONEOF(deferred, Deferred) {
          return kj::none;
        }
        KJ_CASE_ONEOF(cap, kj::Own<ClientHook>) {
          return cap->getResolved();
        }
      }
      KJ_UNREACHABLE;
    }

    kj::Maybe<kj::Promise<kj::Own<ClientHook>>> whenMoreResolved() override {
      // Someone (locally) is waiting for this capability to resolve. We had better actually
      // accept it.
      //
      // Note that this will *always* return non-null, because accepting a capability always
      // ends up returning a promise cap of some sort. (Even when introduced-to-self, we need to
      // wait for the `Provite` message to show up.)
      return ensureAccepted().whenMoreResolved();
    }

    kj::Maybe<int> getFd() override {
      // This is free to return null if `whenMoreResolved()` would return non-null, which it
      // always will.
      return kj::none;
    }

  private:
    struct Deferred {
      kj::Own<ThirdPartyToContact::Reader> contact;
      kj::Own<RpcClient> vine;
    };

    kj::OneOf<Deferred, kj::Own<ClientHook>> state;

    bool needEmbargo = false;

    ClientHook& ensureAccepted() {
      // Call when this capability is actually used locally. If it's the first use, the Accept
      // message will be sent to actually accept it.

      KJ_SWITCH_ONEOF(state) {
        KJ_CASE_ONEOF(deferred, Deferred) {
          return *state.init<kj::Own<ClientHook>>(
              connectionState->acceptThirdParty(
                  *deferred.contact, kj::mv(deferred.vine), needEmbargo));
        }
        KJ_CASE_ONEOF(cap, kj::Own<ClientHook>) {
          return *cap;
        }
      }
      KJ_UNREACHABLE;
    }
  };

  RpcClient::WriteDescriptorResult writeDescriptor(
      ClientHook& cap, rpc::CapDescriptor::Builder descriptor, kj::Vector<int>& fds) {
    // Write a descriptor for the given capability.

    // Follow all promise resolutions before writing the descriptor. This is important to comply
    // with the rule that a promise cannot resolve to another promise, where the latter promise is
    // itself already resolved (see docs for `CapDescriptor.senderPromise`).
    ClientHook* inner = &cap;
    for (;;) {
      KJ_IF_SOME(resolved, inner->getResolved()) {
        inner = &resolved;
      } else {
        break;
      }
    }

    KJ_IF_SOME(fd, inner->getFd()) {
      descriptor.setAttachedFd(fds.size());
      fds.add(kj::mv(fd));
    }

    KJ_IF_SOME(rpcInner, unwrapIfSameNetwork(*inner)) {
      if (rpcInner.isOnConnection(*this)) {
        return rpcInner.writeDescriptor(descriptor, fds);
      }

      KJ_IF_SOME(conn, tryGetConnection()) {
        KJ_IF_SOME(otherConn, rpcInner.connectionState->tryGetConnection()) {
          if (conn.canIntroduceTo(otherConn, ThreePartyHandoffPurpose::CAPABILITY_PASSING)) {
            // We can do three-party handoff!

            auto tph = descriptor.initThirdPartyHosted();

            auto id = tph.initId();
            auto result = rpcInner.writeThirdPartyDescriptor(otherConn, conn, id);

            KJ_SWITCH_ONEOF(result) {
              KJ_CASE_ONEOF(vine, RpcClient::VineToExport) {
                // We always have to add a new export to the table to use as the "vine"; we cannot
                // share an existing export pointing to the same capability. The reason is, the
                // vine represents not just the capability but *this specific handoff* of the
                // capability; when it is dropped, we can clean up the handoff. This is
                // accomplished by attaching the `vineInfo` to the export.
                //
                // Additionally, the vine is never treated as a promise, even if the capability
                // backing it is a promise.
                ExportId exportId;
                auto& exp = exports.next(exportId);
                exp.canonical = false;
                exp.refcount = 1;
                exp.clientHook = kj::mv(vine.vine);
                exp.vineInfo = kj::mv(vine.vineInfo);

                tph.setVineId(exportId);
                return {
                  .exportId = exportId,

                  // The vine is the correct inner cap to use for disembargo purposes.
                  .described = *exp.clientHook,
                };
              }
              KJ_CASE_ONEOF(redirect, kj::Own<ClientHook>) {
                // writeThirdPartyDescriptor() ended up deciding that it's not pointing at a
                // third party after all, oops. This is unusual, but DeferredThirdPartyClient has
                // some edge cases where this happens.

                // Unfortunately, this will leave a hole in the message. This should be rare.
                // TODO(perf): Since we're removing the last object allocated, the segment ought
                //   to be able to reclaim the memory, but this doesn't appear to be implemented
                //   at present. Fix that?
                descriptor.disownThirdPartyHosted();
                descriptor.setNone();

                // Recursively apply to the redirected client.
                return writeDescriptor(*redirect, descriptor, fds);
              }
            }
          }
        }
      }
    }

    KJ_IF_SOME(exportId, exportsByCap.find(inner)) {
      // We've already seen and exported this capability before.  Just up the refcount.
      auto& exp = KJ_ASSERT_NONNULL(exports.find(exportId));
      ++exp.refcount;
      if (exp.resolveOp == kj::none) {
        descriptor.setSenderHosted(exportId);
      } else {
        descriptor.setSenderPromise(exportId);
      }
      return {
        .exportId = exportId,
        .described = *inner,
      };
    } else {
      // This is the first time we've seen this capability.
      ExportId exportId;
      auto& exp = exports.next(exportId);
      exportsByCap.insert(inner, exportId);
      exp.canonical = true;
      exp.refcount = 1;
      exp.clientHook = inner->addRef();

      KJ_IF_SOME(wrapped, inner->whenMoreResolved()) {
        // This is a promise.  Arrange for the `Resolve` message to be sent later.
        exp.resolveOp = resolveExportedPromise(exportId, kj::mv(wrapped));
        descriptor.setSenderPromise(exportId);
      } else {
        descriptor.setSenderHosted(exportId);
      }

      return {
        .exportId = exportId,
        .described = *inner,
      };
    }
  }

  kj::Array<ExportId> writeDescriptors(
      kj::ArrayPtr<kj::Maybe<kj::Own<ClientHook>>> capTable,
      rpc::Payload::Builder payload, kj::Vector<int>& fds,
      kj::Maybe<kj::HashMap<ClientHook*, kj::Own<ClientHook>>&> describedMap = kj::none) {
    // Write all descriptors for a cap table.
    //
    // If `describedMap` is non-null, then if writing a descriptor of some capability returns
    // a `WriteDescriptorResult::described` that points to a different ClientHook  than the one
    // in the cap table, it'll be added to the table. This is needed specifically when writing
    // the cap table for returned results, to handle embargoes correctly.

    if (capTable.size() == 0) {
      // Calling initCapTable(0) will still allocate a 1-word tag, which we'd like to avoid...
      return nullptr;
    }

    auto capTableBuilder = payload.initCapTable(capTable.size());
    kj::Vector<ExportId> exports(capTable.size());
    for (uint i: kj::indices(capTable)) {
      KJ_IF_SOME(cap, capTable[i]) {
        auto result = writeDescriptor(*cap, capTableBuilder[i], fds);
        KJ_IF_SOME(exportId, result.exportId) {
          exports.add(exportId);
        }
        KJ_IF_SOME(m, describedMap) {
          if (&result.described != cap.get()) {
            m.upsert(cap, result.described.addRef(),
                [&](kj::Own<ClientHook>& existing, kj::Own<ClientHook>&& replacement) {
              KJ_ASSERT(existing.get() == replacement.get());
            });
          }
        }
      } else {
        capTableBuilder[i].setNone();
      }
    }
    return exports.releaseAsArray();
  }

  kj::Maybe<kj::Own<ClientHook>> writeTarget(ClientHook& cap, rpc::MessageTarget::Builder target) {
    // If calls to the given capability should pass over this connection, fill in `target`
    // appropriately for such a call and return kj::none.  Otherwise, return a `ClientHook` to which
    // the call should be forwarded; the caller should then delegate the call to that `ClientHook`.
    //
    // The main case where this ends up returning non-null is if `cap` is a promise that has
    // recently resolved.  The application might have started building a request before the promise
    // resolved, and so the request may have been built on the assumption that it would be sent over
    // this network connection, but then the promise resolved to point somewhere else before the
    // request was sent.  Now the request has to be redirected to the new target instead.

    KJ_IF_SOME(rpcCap, unwrapIfSameConnection(cap)) {
      return rpcCap.writeTarget(target);
    } else {
      return cap.addRef();
    }
  }

  kj::Promise<void> resolveExportedPromise(
      ExportId exportId, kj::Promise<kj::Own<ClientHook>>&& promise) {
    // Implements exporting of a promise.  The promise has been exported under the given ID, and is
    // to eventually resolve to the ClientHook produced by `promise`.  This method waits for that
    // resolve to happen and then sends the appropriate `Resolve` message to the peer.

    return promise.then(
        [this,exportId](kj::Own<ClientHook>&& resolution) -> kj::Promise<void> {
      // Successful resolution.

      KJ_ASSERT(connection.is<Connected>(),
                "Resolving export should have been canceled on disconnect.") {
        return kj::READY_NOW;
      }

      // If we're resolving to anonther promise that is itself already resolved, skip past it.
      //
      // Note that `writeDescriptor()`, below, will also do this. However, before we call
      // `writeDescriptor()`, we're going to implemnet an optimization where if we resolve to
      // another promise (which is itself *not* resolved), we may reuse the same table entry
      // without sending a `Resolve` at all (see below).
      for (;;) {
        KJ_IF_SOME(inner, resolution->getResolved()) {
          resolution = inner.addRef();
        } else {
          break;
        }
      }

      // Update the export table to point at this object instead.  We know that our entry in the
      // export table is still live because when it is destroyed the asynchronous resolution task
      // (i.e. this code) is canceled.
      auto& exp = KJ_ASSERT_NONNULL(exports.find(exportId));
      if (exp.canonical) {
        KJ_ASSERT(exportsByCap.erase(exp.clientHook));
      }
      exp.clientHook = kj::mv(resolution);

      // The export now points to `resolution`, but it is not necessarily the canonical export
      // for `resolution`. The export itself still represents the promise that ended up resolving
      // to `resolution`, but `resoultion` itself also needs to be exported under a separate
      // export ID to distinguish from the promise. (Unless it's also a promise, see the next
      // bit...)
      exp.canonical = false;

      if (unwrapIfSameNetwork(*exp.clientHook) == kj::none) {
        // We're resolving to a local capability.  If we're resolving to a promise, we might be
        // able to reuse our export table entry and avoid sending a message.

        KJ_IF_SOME(promise, exp.clientHook->whenMoreResolved()) {
          // We're replacing a promise with another local promise.  In this case, we might actually
          // be able to just reuse the existing export table entry to represent the new promise --
          // unless it already has an entry.  Let's check.

          ExportId replacementExportId = exportsByCap.findOrCreate(
              exp.clientHook, [&]() -> decltype(exportsByCap)::Entry {
            // The replacement capability isn't previously exported, so assign it to the existing
            // table entry.
            return {exp.clientHook, exportId};
          });
          if (replacementExportId == exportId) {
            // The new promise was not already in the table, therefore the existing export table
            // entry has now been repurposed to represent it.  There is no need to send a resolve
            // message at all.  We do, however, have to start resolving the next promise.
            exp.canonical = true;
            return resolveExportedPromise(exportId, kj::mv(promise));
          }
        }
      }

      // OK, we have to send a `Resolve` message.
      auto message = connection.get<Connected>().connection->newOutgoingMessage(
          messageSizeHint<rpc::Resolve>() + sizeInWords<rpc::CapDescriptor>() + 16);
      auto resolve = message->getBody().initAs<rpc::Message>().initResolve();
      resolve.setPromiseId(exportId);
      kj::Vector<int> fds;
      auto writeDescResult = writeDescriptor(*exp.clientHook, resolve.initCap(), fds);
      message->setFds(fds.releaseAsArray());
      message->send();

      // `writeDescriptor()` can create new exports, invalidating the `exp` reference. Look it up
      // again.
      auto& exp2 = KJ_ASSERT_NONNULL(exports.find(exportId));
      if (&writeDescResult.described != exp2.clientHook.get()) {
        // Apparently, we actually resolved to another RPC promise, and the descriptor we wrote
        // was for the temporary destination for pipelined calls. To resolve the Tribble 4-way
        // race condition, we must make sure our existing export table entry permanently points
        // strictly to the pipeline, not the promise.
        exp2.clientHook = writeDescResult.described.addRef();
      }

      return kj::READY_NOW;
    }, [this,exportId](kj::Exception&& exception) {
      // send error resolution
      auto message = connection.get<Connected>().connection->newOutgoingMessage(
          messageSizeHint<rpc::Resolve>() + exceptionSizeHint(exception) + 8);
      auto resolve = message->getBody().initAs<rpc::Message>().initResolve();
      resolve.setPromiseId(exportId);
      fromException(exception, resolve.initException());
      message->send();
    }).eagerlyEvaluate([this](kj::Exception&& exception) {
      // Put the exception on the TaskSet which will cause the connection to be terminated.
      tasks.add(kj::mv(exception));
    });
  }

  void fromException(const kj::Exception& exception, rpc::Exception::Builder builder) {
    _::fromException(exception, builder, traceEncoder);
  }

  // =====================================================================================
  // Interpreting CapDescriptor

  kj::Own<RpcClient> import(ImportId importId, bool isPromise, kj::Maybe<kj::OwnFd> fd) {
    // Receive a new import.

    auto& import = imports.findOrCreate(importId);
    kj::Own<ImportClient> importClient;

    // Create the ImportClient, or if one already exists, use it.
    KJ_IF_SOME(c, import.importClient) {
      importClient = kj::addRef(c);

      // If the same import is introduced multiple times, and it is missing an FD the first time,
      // but it has one on a later attempt, we want to attach the later one. This could happen
      // because the first introduction was part of a message that had too many other FDs and went
      // over the per-message limit. Perhaps the protocol design is such that this other message
      // doesn't really care if the FDs are transferred or not, but the later message really does
      // care; it would be bad if the previous message blocked later messages from delivering the
      // FD just because it happened to reference the same capability.
      importClient->setFdIfMissing(kj::mv(fd));
    } else {
      importClient = kj::refcounted<ImportClient>(*this, importId, kj::mv(fd));
      import.importClient = *importClient;
    }

    // We just received a copy of this import ID, so the remote refcount has gone up.
    importClient->addRemoteRef();

    if (isPromise) {
      // We need to construct a PromiseClient around this import, if we haven't already.
      KJ_IF_SOME(c, import.promiseClient) {
        // Use the existing one.
        return kj::addRef(c);
      } else {
        // Create a PromiseClient around it and return it.
        auto result = kj::refcounted<PromiseClient>(*this, kj::mv(importClient));
        import.promiseClient = *result;
        return kj::mv(result);
      }
    } else {
      return kj::mv(importClient);
    }
  }

  class TribbleRaceBlocker: public ClientHook, public kj::Refcounted {
    // Hack to work around a problem that arises during the Tribble 4-way Race Condition as
    // described in rpc.capnp in the documentation for the `Disembargo` message.
    //
    // Consider a remote promise that is resolved by a `Resolve` message. PromiseClient::resolve()
    // is eventually called and given the `ClientHook` for the resolution. Imagine that the
    // `ClientHook` it receives turns out to be an `ImportClient`. There are two ways this could
    // have happened:
    //
    // 1. The `Resolve` message contained a `CapDescriptor` of type `senderHosted`, naming an entry
    //    in the sender's export table, and the `ImportClient` refers to the corresponding slot on
    //    the receiver's import table. In this case, no embargo is needed, because messages to the
    //    resolved location traverse the same path as messages to the promise would have.
    //
    // 2. The `Resolve` message contained a `CapDescriptor` of type `receiverHosted`, naming an
    //    entry in the receiver's export table. That entry just happened to contain an
    //    `ImportClient` referring back to the sender. This specifically happens when the entry
    //    in question had previously itself referred to a promise, and that promise has since
    //    resolved to a remote capability, at which point the export table entry was replaced by
    //    the appropriate `ImportClient` representing that. Presumably, the peer *did not yet know*
    //    about this resolution, which is why it sent a `receiverHosted` pointing to something that
    //    reflects back to the sender, rather than sending `senderHosted` in the first place.
    //
    //    In this case, an embargo *is* required, because peer may still be reflecting messages
    //    sent to this promise back to us. In fact, the peer *must* continue reflecting messages,
    //    even when it eventually learns that the eventual destination is one of its own
    //    capabilities, due to the Tribble 4-way Race Condition rule.
    //
    //    Since this case requires an embargo, somehow PromiseClient::resolve() must be able to
    //    distinguish it from the case (1). One solution would be for us to pass some extra flag
    //    all the way from where the `Resolve` messages is received to `PromiseClient::resolve()`.
    //    That solution is reasonably easy in the `Resolve` case, but gets notably more difficult
    //    in the case of `Return`s, which also resolve promises and are subject to all the same
    //    problems. In the case of a `Return`, some non-RPC-specific code is involved in the
    //    resolution, making it harder to pass along a flag.
    //
    //    Instead, we use this hack: When we read an entry in the export table and discover that
    //    it actually contains an `ImportClient` or a `PipelineClient` reflecting back over our
    //    own connection, then we wrap it in a `TribbleRaceBlocker`. This wrapper prevents
    //    `PromiseClient` from recognizing the capability as being remote, so it instead treats it
    //    as local. That causes it to set up an embargo as desired.
    //
    // TODO(perf): This actually blocks further promise resolution in the case where the
    //   ImportClient or PipelineClient itself ends up being yet another promise that resolves
    //   back over the connection again. What we probably really need to do here is, instead of
    //   placing `ImportClient` or `PipelineClient` on the export table, place a special type there
    //   that both knows what to do with future incoming messages to that export ID, but also knows
    //   what to do when that export is the subject of a `Resolve`.

  public:
    TribbleRaceBlocker(kj::Own<ClientHook> inner): inner(kj::mv(inner)) {}

    Request<AnyPointer, AnyPointer> newCall(
        uint64_t interfaceId, uint16_t methodId, kj::Maybe<MessageSize> sizeHint,
        CallHints hints) override {
      return inner->newCall(interfaceId, methodId, sizeHint, hints);
    }
    VoidPromiseAndPipeline call(uint64_t interfaceId, uint16_t methodId,
                                kj::Own<CallContextHook>&& context, CallHints hints) override {
      return inner->call(interfaceId, methodId, kj::mv(context), hints);
    }
    kj::Maybe<ClientHook&> getResolved() override {
      // We always wrap either PipelineClient or ImportClient, both of which return null for this
      // anyway.
      return kj::none;
    }
    kj::Maybe<kj::Promise<kj::Own<ClientHook>>> whenMoreResolved() override {
      // We always wrap either PipelineClient or ImportClient, both of which return null for this
      // anyway.
      return kj::none;
    }
    kj::Own<ClientHook> addRef() override {
      return kj::addRef(*this);
    }
    kj::Maybe<int> getFd() override {
      return inner->getFd();
    }

  private:
    kj::Own<ClientHook> inner;
  };

  kj::Maybe<kj::Own<ClientHook>> receiveCap(rpc::CapDescriptor::Reader descriptor,
                                            kj::ArrayPtr<kj::OwnFd> fds) {
    uint fdIndex = descriptor.getAttachedFd();
    kj::Maybe<kj::OwnFd> fd;
    if (fdIndex < fds.size() && fds[fdIndex] != nullptr) {
      fd = kj::mv(fds[fdIndex]);
    }

    switch (descriptor.which()) {
      case rpc::CapDescriptor::NONE:
        return kj::none;

      case rpc::CapDescriptor::SENDER_HOSTED:
        return import(descriptor.getSenderHosted(), false, kj::mv(fd));
      case rpc::CapDescriptor::SENDER_PROMISE:
        return import(descriptor.getSenderPromise(), true, kj::mv(fd));

      case rpc::CapDescriptor::RECEIVER_HOSTED:
        KJ_IF_SOME(exp, exports.find(descriptor.getReceiverHosted())) {
          auto result = exp.clientHook->addRef();
          KJ_IF_SOME(vineInfo, exp.vineInfo) {
            KJ_IF_SOME(contact, vineInfo->info.tryGet<VineInfo::Contact>()) {
              // This is a vine, and not the root of the vine. We previously forwarded a
              // three-party handoff to the peer, and the peer reflected it back to us. We need
              // to wrap this in a new `DeferredThirdPartyClient` so that if it is sent to yet
              // another party, it will be handed off correctly. If we just pass along the vine
              // itself, then three-party handoff will stop working.
              KJ_ASSERT(isSameNetwork(*result));
              kj::Own<RpcClient> vine = result.downcast<RpcClient>();
              auto& vineConnection = *vine->connectionState;
              result = kj::refcounted<DeferredThirdPartyClient>(
                  vineConnection, capnp::clone(*contact), kj::mv(vine));
            }
          }
          if (unwrapIfSameConnection(*result) != kj::none) {
            result = kj::refcounted<TribbleRaceBlocker>(kj::mv(result));
          }
          return kj::mv(result);
        } else {
          return newBrokenCap("invalid 'receiverHosted' export ID");
        }

      case rpc::CapDescriptor::RECEIVER_ANSWER: {
        auto promisedAnswer = descriptor.getReceiverAnswer();

        KJ_IF_SOME(answer, answers.find(promisedAnswer.getQuestionId())) {
          KJ_IF_SOME(pipeline, answer.pipeline) {
            auto ops = toPipelineOps(promisedAnswer.getTransform());
            auto result = pipeline->getPipelinedCap(ops);
            if (unwrapIfSameConnection(*result) != kj::none) {
              result = kj::refcounted<TribbleRaceBlocker>(kj::mv(result));
            }
            return kj::mv(result);
          }
        }

        return newBrokenCap("invalid 'receiverAnswer'");
      }

      case rpc::CapDescriptor::THIRD_PARTY_HOSTED: {
        // We need to connect to a third party to accept this capability.

        auto tph = descriptor.getThirdPartyHosted();

        // Import the vine first so that we're sure to drop it if anything goes wrong.
        auto vine = import(tph.getVineId(), false, kj::mv(fd));

        return kj::refcounted<DeferredThirdPartyClient>(
            *this, capnp::clone(tph.getId()), kj::mv(vine));
      }

      default:
        KJ_FAIL_REQUIRE("unknown CapDescriptor type");
        return newBrokenCap("unknown CapDescriptor type");
    }
  }

  kj::Own<ClientHook> acceptThirdParty(ThirdPartyToContact::Reader contact, kj::Own<RpcClient> vine,
                                       bool embargo) {
    KJ_SWITCH_ONEOF(connection) {
      KJ_CASE_ONEOF(state, Connected) {
        // Allocate temporary message to hold ThirdPartyCompletion. Unfortunately we need a temp
        // message here because we can't allocate the actual outgoing message until we have a
        // connection object.
        // TODO(perf): Maybe we can change the signature of connectToIntroduced() to fix this?
        capnp::word scratch[32];
        memset(scratch, 0, sizeof(scratch));
        MallocMessageBuilder message(scratch);
        auto completion = message.getRoot<ThirdPartyCompletion>();

        kj::Maybe<kj::Array<byte>> embargoId;
        if (embargo) {
          // Oh, this accept should be embargoed. Generate embargo ID.
          auto eid = state.connection->generateEmbargoId();

          // Send the Disembargo message to the vine.
          auto disembargoMsg = state.connection->newOutgoingMessage(
              messageSizeHint<rpc::Disembargo>() + MESSAGE_TARGET_SIZE_HINT +
              eid.size() / sizeof(word) + 1);
          auto disembargo = disembargoMsg->getBody().getAs<rpc::Message>().initDisembargo();
          vine->writeTarget(disembargo.initTarget());
          disembargo.getContext().setAccept(eid);
          disembargoMsg->send();

          embargoId = kj::mv(eid);
        }

        // Call connectToIntroduced() and get the other connection state.
        KJ_IF_SOME(ownOtherConn, state.connection->connectToIntroduced(contact, completion)) {
          RpcConnectionState& other = getConnectionState(state.rpcSystem, kj::mv(ownOtherConn));

          other.setNotIdle();
          auto& otherConn = *KJ_ASSERT_NONNULL(other.connection.tryGet<Connected>()).connection;

          // Start a question for the Accept message.
          QuestionId questionId;
          auto& question = other.questions.next(questionId);
          question.isAwaitingReturn = true;
          auto questionRef = kj::refcounted<QuestionRef>(other, questionId);
          question.selfRef = *questionRef;

          // Send the Accept message.
          {
            auto acceptMsg = otherConn.newOutgoingMessage(messageSizeHint<rpc::Accept>() +
                completion.targetSize().wordCount + (embargo ? 8 : 0));
            auto accept = acceptMsg->getBody().getAs<rpc::Message>().initAccept();
            accept.setQuestionId(questionId);
            accept.getProvision().set(completion.asReader());

            KJ_IF_SOME(eid, embargoId) {
              // And finally adopt the embargo ID into the accept message.
              accept.setEmbargo(eid);
            }

            acceptMsg->send();
          }

          // Arrange to drop the vine as soon as the Accept completes, which lets the introducer
          // know that it can finish out the Provide.
          questionRef->setDropWhenDone(kj::mv(vine));

          // We have not create an RpcPipeline just to fetch its root object, because RpcPipeline
          // is what QuestionRef knows how to fulfill.
          auto pipeline = kj::refcounted<RpcPipeline>(
              other, kj::mv(questionRef), /*willResolve =*/ true);
          return pipeline->getPipelinedCap(kj::Array<PipelineOp>());
        } else {
          // We've been introduced to ourselves. A corresponding `Provide` message will come
          // directly to us via another connection.
          return newLocalPromiseClient(doAccept(state, completion, kj::mv(embargoId)));
        }
      }
      KJ_CASE_ONEOF(error, Disconnected) {
        return newBrokenCap(kj::cp(error));
      }
    }
    KJ_UNREACHABLE;
  }

  kj::Array<kj::Maybe<kj::Own<ClientHook>>> receiveCaps(List<rpc::CapDescriptor>::Reader capTable,
                                                        kj::ArrayPtr<kj::OwnFd> fds) {
    auto result = kj::heapArrayBuilder<kj::Maybe<kj::Own<ClientHook>>>(capTable.size());
    for (auto cap: capTable) {
      result.add(receiveCap(cap, fds));
    }
    return result.finish();
  }

  // =====================================================================================
  // RequestHook/PipelineHook/ResponseHook implementations

  class QuestionRef: public kj::Refcounted {
    // A reference to an entry on the question table.  Used to detect when the `Finish` message
    // can be sent.

  public:
    inline QuestionRef(RpcConnectionState& connectionState, QuestionId id)
        : connectionState(kj::addRef(connectionState)), id(id) {}

    ~QuestionRef() noexcept {
      // Contrary to KJ style, we declare this destructor `noexcept` because if anything in here
      // throws (without being caught) we're probably in pretty bad shape and going to be crashing
      // later anyway. Better to abort now.

      KJ_IF_SOME(c, connectionState) {
        auto& connectionState = c;

        auto& question = KJ_ASSERT_NONNULL(
            connectionState->questions.find(id), "Question ID no longer on table?");

        // Send the "Finish" message (if the connection is not already broken).
        if (connectionState->connection.is<Connected>() && !question.skipFinish) {
          KJ_IF_SOME(e, kj::runCatchingExceptions([&]() {
            auto message = connectionState->connection.get<Connected>().connection
                ->newOutgoingMessage(messageSizeHint<rpc::Finish>());
            auto builder = message->getBody().getAs<rpc::Message>().initFinish();
            builder.setQuestionId(id);
            // If we're still awaiting a return, then this request is being canceled, and we're going
            // to ignore any capabilities in the return message, so set releaseResultCaps true. If we
            // already received the return, then we've already built local proxies for the caps and
            // will send Release messages when those are destroyed.
            builder.setReleaseResultCaps(question.isAwaitingReturn);

            // Let the peer know we don't have the early cancellation bug.
            builder.setRequireEarlyCancellationWorkaround(false);

            message->send();
          })) {
            connectionState->tasks.add(kj::mv(e));
          }
        }

        // Check if the question has returned and, if so, remove it from the table.
        // Remove question ID from the table.  Must do this *after* sending `Finish` to ensure that
        // the ID is not re-allocated before the `Finish` message can be sent.
        if (question.isAwaitingReturn) {
          // Still waiting for return, so just remove the QuestionRef pointer from the table.
          question.selfRef = kj::none;
        } else {
          // Call has already returned, so we can now remove it from the table.
          connectionState->questions.erase(id, question);
          connectionState->checkIfBecameIdle();
        }
      }
    }

    inline QuestionId getId() const { return id; }
    inline bool isDone() const { return done; }

    bool hasReceivedPipeliendCall() {
      for (auto& w: waitingPromises) {
        if (w.hasReceivedCall()) return true;
      }
      return false;
    }

    void fulfill(kj::Own<RpcResponse>&& response) {
      // Pipelined caps must get notified of resolution before the app does to maintain ordering.
      if (waitingPipeline != kj::none || !waitingPromises.empty()) {
        callAfterTpaEmbargo([this, response = response->addRef()]() mutable {
          auto results = response->getResults();
          for (auto& w: waitingPromises) {
            w.resolveQuestion(results);
          }

          KJ_IF_SOME(p, waitingPipeline) {
            p.resolve({}, response->addRef());
          }
        });
      }

      // TODO(someday): If the response object contains capabilities on which pipelined calls were
      //   made, in theory the response object's copies of those capabilities should be embargoed
      //   too, but we have never actually made this so. Only the capabilities obtained from the
      //   Pipeline object ever respect any embargo. That said, any application that actually does
      //   pipelined calls in practice holds on to the pipeline cap long-term and does not fetch
      //   the same cap off of the promise later, so the embargoes work as expected. Still,
      //   could consider replacing the capabilities on the response with the corresponding
      //   PipelineClients if they exist, in order to make things actually fully consistent.
      //   (This TODO applies to the other similar methods below as well.)
      KJ_IF_SOME(f, fulfiller) {
        f.fulfill(kj::mv(response));
      }

      dropWhenDone = kj::none;
      done = true;
    }

    void fulfill(kj::Promise<kj::Own<RpcResponse>>&& promise) {
      // Pipelined caps must get notified of resolution before the app does to maintain ordering.
      if (waitingPipeline != kj::none || !waitingPromises.empty()) {
        auto fork = promise.fork();
        auto newPipeline = newLocalPromisePipeline(fork.addBranch()
            .then([](kj::Own<RpcResponse> response) {
          AnyPointer::Reader results = response->getResults();
          return newLocalPipeline(kj::mv(response), results);
        }));
        promise = fork.addBranch();

        callAfterTpaEmbargo([this, newPipeline = kj::mv(newPipeline)]() mutable {
          for (auto& w: waitingPromises) {
            w.resolveQuestion(*newPipeline);
          }

          KJ_IF_SOME(w, waitingPipeline) {
            w.forward({}, kj::mv(newPipeline));
          }
        });
      }

      KJ_IF_SOME(f, fulfiller) {
        f.fulfill(kj::mv(promise));
      }

      dropWhenDone = kj::none;
      done = true;
    }

    void fulfillTailCall() {
      // When returning from a tail call we do not want to resolve the pipeline since we don't
      // actually have a response to resolve it with. Pipelined calls should keep going all
      // the way to the endpoint.

      KJ_IF_SOME(f, fulfiller) {
        // As a hack, we fulfill the response with null. RpcRequest::tailSend() expects this and
        // will not use the response.
        // TODO(cleanup): This is pretty ugly.
        f.fulfill(kj::Own<RpcResponse>());
      }

      dropWhenDone = kj::none;
      done = true;
    }

    void redirect(kj::Promise<kj::Own<RpcResponse>>&& promise,
                  kj::Own<PipelineHook> newPipeline) {
      // Redirect to a new promise and pipeline, for accepting a third-party call.

      if (waitingPipeline != kj::none || !waitingPromises.empty()) {
        callAfterTpaEmbargo([this, newPipeline = kj::mv(newPipeline)]() mutable {
          for (auto& w: waitingPromises) {
            w.resolveQuestion(*newPipeline);
          }

          KJ_IF_SOME(w, waitingPipeline) {
            w.forward({}, kj::mv(newPipeline));
          }
        });
      }

      KJ_IF_SOME(f, fulfiller) {
        f.fulfill(kj::mv(promise));
      }

      dropWhenDone = kj::none;
      done = true;
    }

    void reject(kj::Exception&& exception) {
      // Pipelined caps must get notified of resolution before the app does to maintain ordering.
      for (auto& w: waitingPromises) {
        w.reject(kj::cp(exception));
      }

      KJ_IF_SOME(p, waitingPipeline) {
        p.reject({}, kj::cp(exception));
      }

      KJ_IF_SOME(f, fulfiller) {
        f.reject(kj::cp(exception));
      }

      this->exception = kj::heap(kj::mv(exception));

      dropWhenDone = kj::none;
      done = true;
    }

    void disconnect() {
      connectionState = kj::none;
    }

    kj::Maybe<VatNetworkBase::Connection&> tryGetConnection() {
      KJ_IF_SOME(c, connectionState) {
        return c->tryGetConnection();
      } else {
        return kj::none;
      }
    }

    using FulfillerT = kj::Promise<kj::Own<RpcResponse>>;
    using Fulfiller = kj::PromiseFulfiller<FulfillerT>;
    void setFulfiller(Fulfiller& f) {
      KJ_ASSERT(fulfiller == nullptr);
      KJ_IF_SOME(e, exception) {
        f.reject(kj::cp(*e));
      } else {
        KJ_REQUIRE(!done);
        fulfiller = f;
      }
    }
    void clearFulfiller() {
      fulfiller = kj::none;
    }

    void setPipeline(RpcPipeline& pipeline) {
      KJ_ASSERT(waitingPipeline == kj::none);
      KJ_IF_SOME(e, exception) {
        pipeline.reject({}, kj::cp(*e));
      } else {
        KJ_REQUIRE(!done);
        waitingPipeline = pipeline;
      }
    }
    void clearPipeline() {
      waitingPipeline = kj::none;
    }

    void addPromiseClient(PromiseClient& promise) {
      KJ_IF_SOME(e, exception) {
        promise.reject(kj::cp(*e));
      } else {
        KJ_REQUIRE(!done || tpaEmbargoLifter != kj::none);
        waitingPromises.add(promise);
      }
    }
    void removePromiseClient(PromiseClient& promise) {
      waitingPromises.remove(promise);
    }

    void setDropWhenDone(kj::Own<void> obj) {
      // Arrange that the given object will be dropped when this question completes or is canceled.
      KJ_IF_SOME(d, dropWhenDone) {
        // Oops, we alerady have a drop-when-done... we can chain them.
        // (I don't think this ever happens, just being safe.)
        obj = obj.attach(kj::mv(d));
      }
      dropWhenDone = kj::mv(obj);
    }

    void expectThirdPartyAnswerDisembargo() {
      // Mark that this question expects a ThirdPartyAnswerDisembargo to be delivered later.

      KJ_REQUIRE(!done);
      expectingTpaDisembargo = true;
    }

    void gotThirdPartyAnswerDisembargo() {
      // Indicates that we received the ThirdPartyAnswerDisembargo we were waiting for.

      KJ_IF_SOME(f, tpaEmbargoLifter) {
        f();
        tpaEmbargoLifter = kj::none;
      }

      // Note that if the embargo is lifted before the `Return` is received, then
      // `tpaDisembargoFulfiller` is null. But in this case we don't actually have to do anything
      // on our end anyway; the embargo was fully managed on the remote end. So we can just unset
      // the flag here to pretend we never had an embargo.
      expectingTpaDisembargo = false;
    }

  private:
    kj::Maybe<kj::Own<RpcConnectionState>> connectionState;
    QuestionId id;
    bool done = false;
    bool expectingTpaDisembargo = false;
    kj::Maybe<Fulfiller&> fulfiller;

    kj::Maybe<RpcPipeline&> waitingPipeline;
    // The RpcPipeline waiting for the result of this question.
    //
    // As an optimization, the RpcPipeline does not wait on the promise fulfilled by `fulfiller`,
    // as doing so would involve a lot of promise node allocation and event loop spinning on
    // completion.

    kj::List<PromiseClient, &PromiseClient::link> waitingPromises;
    // Similar to `waitingPipeline`, a list of promise capabilities waiting on the result of this
    // question.

    kj::Maybe<kj::Own<void>> dropWhenDone;

    kj::Maybe<kj::Own<kj::Exception>> exception;

    kj::Maybe<kj::Function<void()>> tpaEmbargoLifter;

    template <typename Func>
    void callAfterTpaEmbargo(Func&& func) {
      // If no three-party embargo is in effect, call func() now.
      //
      // If one is in effect, call func() as soon as it is lifted.

      if (expectingTpaDisembargo) {
        KJ_ASSERT(tpaEmbargoLifter == kj::none, "Duplicate return?");
        tpaEmbargoLifter = kj::mv(func);
      } else {
        func();
      }
    }
  };

  class QuestionPromiseAdapter {
  public:
    QuestionPromiseAdapter(QuestionRef::Fulfiller& fulfiller,
                           kj::Own<QuestionRef> questionRefParam)
        : questionRef(kj::mv(questionRefParam)) {
      questionRef->setFulfiller(fulfiller);
    }
    ~QuestionPromiseAdapter() noexcept(false) {
      questionRef->clearFulfiller();
    }

  private:
    kj::Own<QuestionRef> questionRef;
  };

  static kj::Promise<kj::Own<RpcResponse>> toPromise(kj::Own<QuestionRef> questionRef) {
    return kj::newAdaptedPromise<QuestionRef::FulfillerT, QuestionPromiseAdapter>(
        kj::mv(questionRef));
  }

  class RpcRequest final: public RequestHook {
  public:
    RpcRequest(RpcConnectionState& connectionState, VatNetworkBase::Connection& connection,
               kj::Maybe<MessageSize> sizeHint, kj::Own<RpcClient>&& target)
        : RequestHook(connectionState.brand.get()),
          connectionState(kj::addRef(connectionState)),
          target(kj::mv(target)),
          message(connection.newOutgoingMessage(
              firstSegmentSize(sizeHint, messageSizeHint<rpc::Call>() +
                  sizeInWords<rpc::Payload>() + MESSAGE_TARGET_SIZE_HINT))),
          callBuilder(message->getBody().getAs<rpc::Message>().initCall()),
          paramsBuilder(capTable.imbue(callBuilder.getParams().getContent())) {}

    bool isOnConnection(RpcConnectionState& expected) {
      return connectionState.get() == &expected;
    }

    inline AnyPointer::Builder getRoot() {
      return paramsBuilder;
    }
    inline rpc::Call::Builder getCall() {
      return callBuilder;
    }

    RemotePromise<AnyPointer> send() override {
      if (!connectionState->connection.is<Connected>()) {
        // Connection is broken.
        // TODO(bug): Seems like we should check for redirect before this?
        const kj::Exception& e = connectionState->connection.get<Disconnected>();
        return RemotePromise<AnyPointer>(
            kj::Promise<Response<AnyPointer>>(kj::cp(e)),
            AnyPointer::Pipeline(newBrokenPipeline(kj::cp(e))));
      }

      KJ_IF_SOME(redirect, target->writeTarget(callBuilder.getTarget())) {
        // Whoops, this capability has been redirected while we were building the request!
        // We'll have to make a new request and do a copy.  Ick.

        auto replacement = redirect->newCall(
            callBuilder.getInterfaceId(), callBuilder.getMethodId(), paramsBuilder.targetSize(),
            callHintsFromReader(callBuilder));
        replacement.set(paramsBuilder);
        return replacement.send();
      } else {
        bool noPromisePipelining = callBuilder.getNoPromisePipelining();

        auto questionRef = sendInternal(false);

        kj::Own<PipelineHook> pipeline;
        if (noPromisePipelining) {
          pipeline = getDisabledPipeline();
        } else {
          pipeline = kj::refcounted<RpcPipeline>(*connectionState, kj::addRef(*questionRef),
                                                 /*willResolve =*/ true);
        }

        auto appPromise = toPromise(kj::mv(questionRef)).then(
            [=](kj::Own<RpcResponse>&& response) {
              auto reader = response->getResults();
              return Response<AnyPointer>(reader, kj::mv(response));
            });

        return RemotePromise<AnyPointer>(
            kj::mv(appPromise),
            AnyPointer::Pipeline(kj::mv(pipeline)));
      }
    }

    kj::Promise<void> sendStreaming() override {
      if (!connectionState->connection.is<Connected>()) {
        // Connection is broken.
        // TODO(bug): Seems like we should check for redirect before this?
        return kj::cp(connectionState->connection.get<Disconnected>());
      }

      KJ_IF_SOME(redirect, target->writeTarget(callBuilder.getTarget())) {
        // Whoops, this capability has been redirected while we were building the request!
        // We'll have to make a new request and do a copy.  Ick.

        auto replacement = redirect->newCall(
            callBuilder.getInterfaceId(), callBuilder.getMethodId(), paramsBuilder.targetSize(),
            callHintsFromReader(callBuilder));
        replacement.set(paramsBuilder);
        return RequestHook::from(kj::mv(replacement))->sendStreaming();
      } else {
        return sendStreamingInternal(false);
      }
    }

    AnyPointer::Pipeline sendForPipeline() override {
      if (!connectionState->connection.is<Connected>()) {
        // Connection is broken.
        // TODO(bug): Seems like we should check for redirect before this?
        const kj::Exception& e = connectionState->connection.get<Disconnected>();
        return AnyPointer::Pipeline(newBrokenPipeline(kj::cp(e)));
      }

      KJ_IF_SOME(redirect, target->writeTarget(callBuilder.getTarget())) {
        // Whoops, this capability has been redirected while we were building the request!
        // We'll have to make a new request and do a copy.  Ick.

        auto replacement = redirect->newCall(
            callBuilder.getInterfaceId(), callBuilder.getMethodId(), paramsBuilder.targetSize(),
            callHintsFromReader(callBuilder));
        replacement.set(paramsBuilder);
        return replacement.sendForPipeline();
      } else if (connectionState->gotReturnForHighQuestionId) {
        // Peer doesn't implement our hints. Fall back to a regular send().
        return send();
      } else {
        auto questionRef = sendForPipelineInternal();
        kj::Own<PipelineHook> pipeline = kj::refcounted<RpcPipeline>(
            *connectionState, kj::mv(questionRef), /*willResolve =*/ false);
        return AnyPointer::Pipeline(kj::mv(pipeline));
      }
    }

    struct TailInfo {
      QuestionId questionId;
      kj::Promise<void> promise;
      kj::Own<PipelineHook> pipeline;
    };

    template <typename InitSendResultsToFunc>
    kj::Maybe<TailInfo> tailSend(InitSendResultsToFunc&& initSendResultsTo) {
      // Send the request as an optimized tail call (with results redirected directly to the
      // original caller).
      //
      // Returns null if for some reason a tail call is not possible and the caller should fall
      // back to using send() and copying the response.
      //
      // Calls `initSendResultsTo(callBuilder.getSendResultsTo())` to initialize the
      // `sendResultsTo` part of the call. This is only called after checking for any circumstances
      // that might cause this to return null.

      if (!connectionState->connection.is<Connected>()) {
        // Disconnected; fall back to a regular send() which will fail appropriately.
        return kj::none;
      }

      if (target->writeTarget(callBuilder.getTarget()) != kj::none) {
        // Whoops, this capability has been redirected while we were building the request!
        // Fall back to regular send().
        return kj::none;
      }

      // OK, now that we're definitely sending the call, we can initialize `sendResultsTo`.
      initSendResultsTo(callBuilder.getSendResultsTo());

      auto questionRef = sendInternal(true);

      auto promise = toPromise(kj::addRef(*questionRef)).then([](kj::Own<RpcResponse>&& response) {
        // Response should be null if `Return` handling code is correct.
        KJ_ASSERT(!response);
      });

      QuestionId questionId = questionRef->getId();

      kj::Own<PipelineHook> pipeline;
      bool noPromisePipelining = callBuilder.getNoPromisePipelining();
      if (noPromisePipelining) {
        pipeline = getDisabledPipeline();
      } else {
        pipeline = kj::refcounted<RpcPipeline>(
            *connectionState, kj::mv(questionRef), /*willResolve =*/ false);
      }

      return TailInfo { questionId, kj::mv(promise), kj::mv(pipeline) };
    }

    kj::Own<RpcConnectionState> connectionState;

  private:
    kj::Own<RpcClient> target;
    kj::Own<OutgoingRpcMessage> message;
    BuilderCapabilityTable capTable;
    rpc::Call::Builder callBuilder;
    AnyPointer::Builder paramsBuilder;

    struct SetupSendResult {
      kj::Own<QuestionRef> questionRef;
      QuestionId questionId;
      Question& question;
    };

    SetupSendResult setupSend(bool isTailCall) {
      // Build the cap table.
      kj::Vector<int> fds;
      auto exports = connectionState->writeDescriptors(
          capTable.getTable(), callBuilder.getParams(), fds);
      message->setFds(fds.releaseAsArray());

      // Init the question table.  Do this after writing descriptors to avoid interference.
      QuestionId questionId;
      auto& question = connectionState->questions.next(questionId);
      question.isAwaitingReturn = true;
      question.paramExports = kj::mv(exports);
      question.isTailCall = isTailCall;

      // Make the QuestionRef and result promise.
      auto questionRef = kj::refcounted<QuestionRef>(*connectionState, questionId);
      question.selfRef = *questionRef;

      return { kj::mv(questionRef), questionId, question };
    }

    kj::Own<QuestionRef> sendInternal(bool isTailCall) {
      auto result = setupSend(isTailCall);

      // Finish and send.
      callBuilder.setQuestionId(result.questionId);
      KJ_IF_SOME(exception, kj::runCatchingExceptions([&]() {
        KJ_CONTEXT("sending RPC call",
           callBuilder.getInterfaceId(), callBuilder.getMethodId());
        message->send();
      })) {
        // We can't safely throw the exception from here since we've already modified the question
        // table state. We'll have to reject the promise instead.
        // TODO(bug): Attempts to use the pipeline will end up sending a request referencing a
        //   bogus question ID. Can we rethrow after doing the appropriate cleanup, so the pipeline
        //   is never created? See the approach in sendForPipelineInternal() below.
        result.question.isAwaitingReturn = false;
        result.question.skipFinish = true;
        connectionState->releaseExports(result.question.paramExports);
        result.questionRef->reject(kj::mv(exception));
      }

      // Send and return.
      return kj::mv(result.questionRef);
    }

    kj::Promise<void> sendStreamingInternal(bool isTailCall) {
      auto setup = setupSend(isTailCall);

      // Finish and send.
      callBuilder.setQuestionId(setup.questionId);
      kj::Promise<void> flowPromise = nullptr;
      KJ_IF_SOME(exception, kj::runCatchingExceptions([&]() {
        KJ_CONTEXT("sending RPC call",
           callBuilder.getInterfaceId(), callBuilder.getMethodId());
        RpcFlowController* flow;
        KJ_IF_SOME(f, target->flowController) {
          flow = f;
        } else {
          flow = target->flowController.emplace(
              connectionState->connection.get<Connected>().connection->newStream());
        }
        flowPromise = flow->send(kj::mv(message),
            toPromise(kj::mv(setup.questionRef)).ignoreResult());
      })) {
        // We can't safely throw the exception from here since we've already modified the question
        // table state. We'll have to reject the promise instead.
        setup.question.isAwaitingReturn = false;
        setup.question.skipFinish = true;
        setup.questionRef->reject(kj::cp(exception));
        return kj::mv(exception);
      }

      return kj::mv(flowPromise);
    }

    kj::Own<QuestionRef> sendForPipelineInternal() {
      // Since must of setupSend() is subtly different for this case, we don't reuse it.

      // Build the cap table.
      kj::Vector<int> fds;
      auto exports = connectionState->writeDescriptors(
          capTable.getTable(), callBuilder.getParams(), fds);
      message->setFds(fds.releaseAsArray());

      if (exports.size() > 0) {
        connectionState->sentCapabilitiesInPipelineOnlyCall = true;
      }

      // Init the question table.  Do this after writing descriptors to avoid interference.
      QuestionId questionId;
      auto& question = connectionState->questions.nextOneWay(questionId);
      question.isAwaitingReturn = false;  // No Return needed
      question.paramExports = kj::mv(exports);
      question.isTailCall = false;

      // Make the QuestionRef and result promise.
      auto questionRef = kj::refcounted<QuestionRef>(*connectionState, questionId);
      question.selfRef = *questionRef;

      // If sending throws, we'll need to fix up the state a little...
      KJ_ON_SCOPE_FAILURE({
        question.skipFinish = true;
        connectionState->releaseExports(question.paramExports);
      });

      // Finish and send.
      callBuilder.setQuestionId(questionId);
      callBuilder.setOnlyPromisePipeline(true);

      KJ_CONTEXT("sending RPC call",
          callBuilder.getInterfaceId(), callBuilder.getMethodId());
      message->send();

      return kj::mv(questionRef);
    }
  };

  class RpcPipeline final: public PipelineHook, public kj::Refcounted {
  public:
    RpcPipeline(RpcConnectionState& connectionState, kj::Own<QuestionRef>&& questionRef,
                bool willResolve)
        : connectionState(kj::addRef(connectionState)), willResolve(willResolve) {
      // Construct a new RpcPipeline.

      questionRef->setPipeline(*this);
      state.init<Waiting>(kj::mv(questionRef));
    }

    ~RpcPipeline() noexcept(false) {
      KJ_IF_SOME(question, state.tryGet<Waiting>()) {
        question->clearPipeline();
      }
    }

    // implements PipelineHook ---------------------------------------

    kj::Own<PipelineHook> addRef() override {
      return kj::addRef(*this);
    }

    kj::Own<ClientHook> getPipelinedCap(kj::ArrayPtr<const PipelineOp> ops) override {
      auto copy = kj::heapArrayBuilder<PipelineOp>(ops.size());
      for (auto& op: ops) {
        copy.add(op);
      }
      return getPipelinedCap(copy.finish());
    }

    kj::Own<ClientHook> getPipelinedCap(kj::Array<PipelineOp>&& ops) override {
      if (state.is<Waiting>() && willResolve) {
        return kj::addRef(*clientMap.findOrCreate(ops.asPtr(), [&]() {
          auto& question = state.get<Waiting>();

          // Wrap a PipelineClient in a PromiseClient.
          auto pipelineClient = kj::refcounted<PipelineClient>(
              *connectionState, kj::addRef(*question), kj::heapArray(ops.asPtr()));
          auto promiseClient = kj::refcounted<PromiseClient>(
                *connectionState, kj::mv(pipelineClient));

          return kj::HashMap<kj::Array<PipelineOp>, kj::Own<PromiseClient>>::Entry {
            kj::mv(ops), kj::mv(promiseClient)
          };
        }));
      } else {
        // In all other cases, we don't need to remember the cap on the map. It's OK to return a
        // new one each time. This is because:
        // - When Waiting and !willResolve, we're just returning a PipelineClient which just sends
        //   a network message when invoked. Since it never resolves there are no ordering
        //   concerns.
        // - When Resolved, we're just pulling capabilities off the final response object. They
        //   will be the same every time inherenlty.
        // - When Forwarded, we can count on the new PipelineHook to memoize if needed.
        // - When Broken, ordering is irrelevant, everything is an error.
        //
        // With that said, if a PipelineClient was added to the map earlier, we do need to keep
        // using it.

        KJ_IF_SOME(client, clientMap.find(ops.asPtr())) {
          return kj::addRef(*client);
        }

        KJ_SWITCH_ONEOF(state) {
          KJ_CASE_ONEOF(question, Waiting) {
            // This pipeline will never get redirected (we checked `willResolve` earlier), so just
            // return the PipelineClient.
            return kj::refcounted<PipelineClient>(
                *connectionState, kj::addRef(*question), kj::heapArray(ops.asPtr()));
          }
          KJ_CASE_ONEOF(response, Resolved) {
            return response->getResults().getPipelinedCap(ops);
          }
          KJ_CASE_ONEOF(pipeline, Forwarded) {
            return pipeline->getPipelinedCap(ops);
          }
          KJ_CASE_ONEOF(exception, Broken) {
            return newBrokenCap(kj::cp(exception));
          }
        }
        KJ_UNREACHABLE;
      }
    }

    void resolve(kj::Badge<QuestionRef>, kj::Own<RpcResponse>&& response) {
      KJ_REQUIRE(state.is<Waiting>(), "Already resolved?");
      KJ_REQUIRE(willResolve, "this pipeline isn't supposed to resolve");
      state.get<Waiting>()->clearPipeline();
      state.init<Resolved>(kj::mv(response));
    }

    void reject(kj::Badge<QuestionRef>, kj::Exception&& exception) {
      KJ_REQUIRE(state.is<Waiting>(), "Already resolved?");
      state.get<Waiting>()->clearPipeline();
      state.init<Broken>(kj::mv(exception));
    }

    void forward(kj::Badge<QuestionRef>, kj::Own<PipelineHook> newPipeline) {
      KJ_REQUIRE(state.is<Waiting>(), "Already resolved?");
      KJ_REQUIRE(willResolve, "this pipeline isn't supposed to resolve");
      state.get<Waiting>()->clearPipeline();
      state.init<Forwarded>(kj::mv(newPipeline));
    }

  private:
    kj::Own<RpcConnectionState> connectionState;
    bool willResolve;

    typedef kj::Own<QuestionRef> Waiting;
    typedef kj::Own<RpcResponse> Resolved;
    typedef kj::Own<PipelineHook> Forwarded;
    typedef kj::Exception Broken;
    kj::OneOf<Waiting, Resolved, Forwarded, Broken> state;

    kj::HashMap<kj::Array<PipelineOp>, kj::Own<PromiseClient>> clientMap;
    // See QueuedPipeline::clientMap in capability.c++ for a discussion of why we must memoize
    // the results of getPipelinedCap(). RpcPipeline has a similar problem when a capability we
    // return is later subject to an embargo. It's important that the embargo is correctly applied
    // across all calls to the same capability.
  };

  class RpcResponse: public ResponseHook {
  public:
    virtual AnyPointer::Reader getResults() = 0;
    virtual kj::Own<RpcResponse> addRef() = 0;
  };

  class RpcResponseImpl final: public RpcResponse, public kj::Refcounted {
  public:
    RpcResponseImpl(RpcConnectionState& connectionState,
                    kj::Own<QuestionRef>&& questionRef,
                    kj::Own<IncomingRpcMessage>&& message,
                    kj::Array<kj::Maybe<kj::Own<ClientHook>>> capTableArray,
                    AnyPointer::Reader results)
        : connectionState(kj::addRef(connectionState)),
          message(kj::mv(message)),
          capTable(kj::mv(capTableArray)),
          reader(capTable.imbue(results)),
          questionRef(kj::mv(questionRef)) {}

    AnyPointer::Reader getResults() override {
      return reader;
    }

    kj::Own<RpcResponse> addRef() override {
      return kj::addRef(*this);
    }

  private:
    kj::Own<RpcConnectionState> connectionState;
    kj::Own<IncomingRpcMessage> message;
    ReaderCapabilityTable capTable;
    AnyPointer::Reader reader;
    kj::Own<QuestionRef> questionRef;
  };

  kj::Maybe<RpcClient&> unwrapIfSameConnection(ClientHook& hook) {
    if (hook.isBrand(brand.get())) {
      RpcClient& result = kj::downcast<RpcClient>(hook);
      if (result.isOnConnection(*this)) {
        return result;
      }
    }
    return kj::none;
  }

  kj::Maybe<RpcRequest&> unwrapIfSameConnection(RequestHook& hook) {
    if (hook.isBrand(brand.get())) {
      RpcRequest& result = kj::downcast<RpcRequest>(hook);
      if (result.isOnConnection(*this)) {
        return result;
      }
    }
    return kj::none;
  }

  kj::Maybe<RpcClient&> unwrapIfSameNetwork(ClientHook& hook) {
    if (hook.isBrand(brand.get())) {
      return kj::downcast<RpcClient>(hook);
    }
    return kj::none;
  }

  bool isSameNetwork(ClientHook& hook) {
    return hook.isBrand(brand.get());
  }

  kj::Maybe<RpcRequest&> unwrapIfSameNetwork(RequestHook& hook) {
    if (hook.isBrand(brand.get())) {
      return kj::downcast<RpcRequest>(hook);
    }
    return kj::none;
  }

  kj::Maybe<VatNetworkBase::Connection&> tryGetConnection() {
    KJ_IF_SOME(c, connection.tryGet<Connected>()) {
      return *c.connection;
    } else {
      return kj::none;
    }
  }

  // =====================================================================================
  // CallContextHook implementation

  class RpcServerResponse {
  public:
    virtual AnyPointer::Builder getResultsBuilder() = 0;
  };

  class RpcServerResponseImpl final: public RpcServerResponse {
  public:
    RpcServerResponseImpl(RpcConnectionState& connectionState,
                          kj::Own<OutgoingRpcMessage>&& message,
                          rpc::Payload::Builder payload)
        : connectionState(connectionState),
          message(kj::mv(message)),
          payload(payload) {}

    AnyPointer::Builder getResultsBuilder() override {
      return capTable.imbue(payload.getContent());
    }

    inline bool hasCapabilities() {
      return capTable.getTable().size() > 0;
    }

    kj::Maybe<kj::Array<ExportId>> send() {
      // Send the response and return the export list.  Returns kj::none if there were no caps.
      // (Could return a non-null empty array if there were caps but none of them were exports.)

      // Build the cap table.
      auto capTable = this->capTable.getTable();
      kj::Vector<int> fds;
      auto exports = connectionState.writeDescriptors(
          capTable, payload, fds, resolutionsAtReturnTime);
      message->setFds(fds.releaseAsArray());

      message->send();
      if (capTable.size() == 0) {
        return kj::none;
      } else {
        return kj::mv(exports);
      }
    }

    struct Resolution {
      kj::Own<ClientHook> returnedCap;
      // The capability that appeared in the response message in this slot.

      kj::Own<ClientHook> unwrapped;
      // Exactly what `getInnermostClient(returnedCap)` produced at the time that the return
      // message was encoded.
    };

    Resolution getResolutionAtReturnTime(kj::ArrayPtr<const PipelineOp> ops) {
      auto returnedCap = getResultsBuilder().asReader().getPipelinedCap(ops);
      kj::Own<ClientHook> unwrapped;
      KJ_IF_SOME(u, resolutionsAtReturnTime.find(returnedCap.get())) {
        unwrapped = u->addRef();
      } else {
        unwrapped = returnedCap->addRef();
      }
      return { kj::mv(returnedCap), kj::mv(unwrapped) };
    }

  private:
    RpcConnectionState& connectionState;
    kj::Own<OutgoingRpcMessage> message;
    BuilderCapabilityTable capTable;
    rpc::Payload::Builder payload;

    kj::HashMap<ClientHook*, kj::Own<ClientHook>> resolutionsAtReturnTime;
    // For each capability in `capTable` as of the time when the call returned, this map stores
    // the result of calling `getInnermostClient()` on that capability. This is needed in order
    // to solve the Tribble 4-way race condition described in the documentation for `Disembargo`
    // in `rpc.capnp`. `PostReturnRpcPipeline`, below, uses this.
    //
    // As an optimization, if the innermost client is exactly the same object then nothing is
    // stored in the map.
  };

  class LocallyRedirectedRpcResponse final
      : public RpcResponse, public RpcServerResponse, public kj::Refcounted{
  public:
    LocallyRedirectedRpcResponse(kj::Maybe<MessageSize> sizeHint)
        : message(sizeHint.map([](MessageSize size) { return size.wordCount; })
                          .orDefault(SUGGESTED_FIRST_SEGMENT_WORDS)) {}

    AnyPointer::Builder getResultsBuilder() override {
      return message.getRoot<AnyPointer>();
    }

    AnyPointer::Reader getResults() override {
      return message.getRoot<AnyPointer>();
    }

    kj::Own<RpcResponse> addRef() override {
      return kj::addRef(*this);
    }

  private:
    MallocMessageBuilder message;
  };

  class PostReturnRpcPipeline final: public PipelineHook, public kj::Refcounted {
    // Once an incoming call has returned, we may need to replace the `PipelineHook` with one that
    // correctly handles the Tribble 4-way race condition. Namely, we must ensure that if the
    // response contained any capabilities pointing back out to the network, then any further
    // pipelined calls received targeting those capabilities (as well as any Disembargo messages)
    // will resolve to the same network capability forever, *even if* that network capability is
    // itself a promise which later resolves to somewhere else.
  public:
    PostReturnRpcPipeline(kj::Own<PipelineHook> inner,
                          RpcServerResponseImpl& response,
                          kj::Own<RpcCallContext> context)
        : inner(kj::mv(inner)), response(response), context(kj::mv(context)) {}

    kj::Own<PipelineHook> addRef() override {
      return kj::addRef(*this);
    }

    kj::Own<ClientHook> getPipelinedCap(kj::ArrayPtr<const PipelineOp> ops) override {
      auto resolved = response.getResolutionAtReturnTime(ops);
      auto original = inner->getPipelinedCap(ops);
      return getResolutionAtReturnTime(kj::mv(original), kj::mv(resolved));
    }

    kj::Own<ClientHook> getPipelinedCap(kj::Array<PipelineOp>&& ops) override {
      auto resolved = response.getResolutionAtReturnTime(ops);
      auto original = inner->getPipelinedCap(kj::mv(ops));
      return getResolutionAtReturnTime(kj::mv(original), kj::mv(resolved));
    }

  private:
    kj::Own<PipelineHook> inner;
    RpcServerResponseImpl& response;
    kj::Own<RpcCallContext> context;  // owns `response`

    static kj::Own<ClientHook> getResolutionAtReturnTime(
        kj::Own<ClientHook> original, RpcServerResponseImpl::Resolution resolution) {
      // Wait for `original` to resolve to `resolution.returnedCap`, then return
      // `resolution.unwrapped`.

      ClientHook* ptr = original.get();
      for (;;) {
        if (ptr == resolution.returnedCap.get()) {
          return kj::mv(resolution.unwrapped);
        } else KJ_IF_SOME(r, ptr->getResolved()) {
          ptr = &r;
        } else {
          break;
        }
      }

      KJ_IF_SOME(p, ptr->whenMoreResolved()) {
        return newLocalPromiseClient(p.then(
            [original = kj::mv(original), resolution = kj::mv(resolution)]
            (kj::Own<ClientHook> r) mutable {
          return getResolutionAtReturnTime(kj::mv(r), kj::mv(resolution));
        }));
      } else if (ptr->isError() || ptr->isNull()) {
        // This is already a broken capability, the error probably explains what went wrong. In
        // any case, message ordering is irrelevant here since all calls will throw anyway.
        return ptr->addRef();
      } else {
        return newBrokenCap(
            "An RPC call's capnp::PipelineHook object resolved a pipelined capability to a "
            "different final object than what was returned in the actual response. This could "
            "be a bug in Cap'n Proto, or could be due to a use of context.setPipeline() that "
            "was inconsistent with the later results.");
      }
    }
  };

  struct SendToCaller {};
  struct SendToSelf {};
  using SendToThirdParty = ThirdPartyToContact::Reader;
  using SendResultsTo = kj::OneOf<SendToCaller, SendToSelf, SendToThirdParty>;

  class RpcCallContext final: public CallContextHook, public kj::Refcounted {
  public:
    RpcCallContext(RpcConnectionState& connectionState, AnswerId answerId,
                   kj::Own<IncomingRpcMessage>&& request,
                   kj::Array<kj::Maybe<kj::Own<ClientHook>>> capTableArray,
                   const AnyPointer::Reader& params,
                   SendResultsTo sendResultsTo, uint64_t interfaceId, uint16_t methodId,
                   ClientHook::CallHints hints)
        : connectionState(kj::addRef(connectionState)),
          answerId(answerId),
          hints(hints),
          interfaceId(interfaceId),
          methodId(methodId),
          requestSize(request->sizeInWords()),
          request(kj::mv(request)),
          paramsCapTable(kj::mv(capTableArray)),
          params(paramsCapTable.imbue(params)),
          returnMessage(nullptr),
          sendResultsTo(sendResultsTo) {
      connectionState.callWordsInFlight += requestSize;
    }

    RpcCallContext(RpcConnectionState& connectionState, AnswerId answerId)
        : connectionState(kj::addRef(connectionState)),
          answerId(answerId),
          interfaceId(0),
          methodId(0),
          requestSize(0),
          paramsCapTable(nullptr),
          returnMessage(nullptr),
          sendResultsTo(SendToCaller()) {}
    // Creates a dummy RpcCallContext used for pseudo-calls like `Accept`. This is helpful to
    // reuse the state machine around `Return` and `Finish` messages.

    ~RpcCallContext() noexcept(false) {
      if (isFirstResponder()) {
        // We haven't sent a return yet, so we must have been canceled.  Send a cancellation return.
        unwindDetector.catchExceptionsIfUnwinding([&]() {
          // Don't send anything if the connection is broken, or if the onlyPromisePipeline hint
          // was used (in which case the caller doesn't care to receive a `Return`).
          bool shouldFreePipeline = true;
          if (connectionState->connection.is<Connected>() && !hints.onlyPromisePipeline) {
            auto message = connectionState->connection.get<Connected>().connection
                ->newOutgoingMessage(messageSizeHint<rpc::Return>() + sizeInWords<rpc::Payload>());
            auto builder = message->getBody().initAs<rpc::Message>().initReturn();

            builder.setAnswerId(answerId);
            builder.setReleaseParamCaps(false);

            builder.setCanceled();

            message->send();
          }

          cleanupAnswerTable(nullptr, shouldFreePipeline);
        });
      }
    }

    void initializeAnswer(Answer& answer, kj::Own<PipelineHook> pipeline,
                          decltype(Answer::task) task) {
      // Called immediately after the call is started, when the answer table's `answer.pipeline`
      // is being assigned, to give the `RpcCallContext` a chance to do something with the
      // pipeline if it needs to.

      answerIsInitialized = true;

      if (acceptedThirdPartyRedirectBeforeInitialized) {
        auto& newAnswer = KJ_ASSERT_NONNULL(connectionState->answers.find(answerId));
        if (!hints.noPromisePipelining) {
          newAnswer.pipeline = pipeline->addRef();
        }
        newAnswer.task = kj::mv(task);
      } else {
        answer.task = kj::mv(task);
      }

      // We always want to initialize the pipeline.
      answer.pipeline = kj::mv(pipeline);
    }

    kj::Own<RpcResponse> consumeRedirectedToSelfResponse() {
      // Consume a "send results to yourself" response, taking ownership of the response locally
      // without it ever being sent over a network.

      KJ_ASSERT(sendResultsTo.is<SendToSelf>());

      if (response == kj::none) getResults(MessageSize{0, 0});  // force initialization of response

      // Note that the context needs to keep its own reference to the response so that it doesn't
      // get GC'd until the PipelineHook drops its reference to the context.
      return kj::downcast<LocallyRedirectedRpcResponse>(*KJ_ASSERT_NONNULL(response)).addRef();
    }

    void sendReturn() {
      KJ_ASSERT(!sendResultsTo.is<SendToSelf>());
      KJ_ASSERT(!hints.onlyPromisePipeline);

      // Force initialization of response. (Also forces acceptThirdPartyRedirectIfNeeded().)
      if (response == kj::none) getResults(MessageSize{0, 0});

      KJ_IF_SOME(selfIntro, sendResultsTo.tryGet<SendToSelfIntroduced>()) {
        // We had a self-introduction earlier, we just want to fulfill the fulfillers...

        auto resp = KJ_ASSERT_NONNULL(response).downcast<LocallyRedirectedRpcResponse>();

        if (selfIntro.pipelineFulfiller->isWaiting()) {
          selfIntro.pipelineFulfiller->fulfill(
              newLocalPipeline(kj::addRef(*resp), resp->getResults()));
        }

        selfIntro.responseFulfiller->fulfill(kj::mv(resp));
        return;
      }

      // Avoid sending results if canceled so that we don't have to figure out whether or not
      // `releaseResultCaps` was set in the already-received `Finish`.
      if (!receivedFinish && isFirstResponder()) {
        KJ_ASSERT(connectionState->connection.is<Connected>(),
                  "Cancellation should have been requested on disconnect.") {
          return;
        }

        returnMessage.setAnswerId(answerId);
        returnMessage.setReleaseParamCaps(false);

        auto& responseImpl = kj::downcast<RpcServerResponseImpl>(*KJ_ASSERT_NONNULL(response));
        if (!responseImpl.hasCapabilities()) {
          returnMessage.setNoFinishNeeded(true);

          // Tell ourselves that a finish was already received, so that `cleanupAnswerTable()`
          // removes the answer table entry.
          receivedFinish = true;

          // HACK: The answer table's `task` is the thing which is calling `sendReturn()`. We can't
          //   cancel ourselves. However, we know calling `sendReturn()` is the last thing it does,
          //   so we can safely detach() it.
          auto& answer = KJ_ASSERT_NONNULL(connectionState->answers.find(answerId));
          auto& selfPromise = KJ_ASSERT_NONNULL(answer.task.tryGet<Answer::Running>());
          selfPromise.detach([](kj::Exception&&) {});
        }

        kj::Maybe<kj::Array<ExportId>> exports;
        KJ_IF_SOME(exception, kj::runCatchingExceptions([&]() {
          // Debug info in case send() fails due to overside message.
          KJ_CONTEXT("returning from RPC call", interfaceId, methodId);
          exports = responseImpl.send();
        })) {
          responseSent = false;
          sendErrorReturn(kj::mv(exception));
          return;
        }

        if (responseImpl.hasCapabilities()) {
          auto& answer = KJ_ASSERT_NONNULL(connectionState->answers.find(answerId));
          // Swap out the `pipeline` in the answer table for one that will return capabilities
          // consistent with whatever the result caps resolved to as of the time the return was sent.
          answer.pipeline = answer.pipeline.map([&](kj::Own<PipelineHook>& inner) {
            return kj::refcounted<PostReturnRpcPipeline>(
                kj::mv(inner), responseImpl, kj::addRef(*this));
          });
        }

        KJ_IF_SOME(e, exports) {
          // Caps were returned, so we can't free the pipeline yet.
          cleanupAnswerTable(kj::mv(e), false);
        } else {
          // No caps in the results, therefore the pipeline is irrelevant.
          cleanupAnswerTable(nullptr, true);
        }
      }
    }
    void sendErrorReturn(kj::Exception&& exception) {
      KJ_ASSERT(!sendResultsTo.is<SendToSelf>());
      KJ_ASSERT(!hints.onlyPromisePipeline);

      // If we were told to redirect to a third party, we have to do so even if the result was an
      // error. This is a bit lame since we might open a connection that literally won't do
      // anything except report this one error. However, if we were to send the error back to
      // the caller, they have no way to propagate it back, since they already sent a `Return`
      // to the original caller with `awaitFromThirdParty`.
      //
      // TODO(someday): Maybe we need some new message we can send here which says "actually
      //   that handoff failed"? Otherwise I guess any failure to connect back to the original
      //   caller turns into a hang / timeout.
      acceptThirdPartyRedirectIfNeeded();

      KJ_IF_SOME(selfIntro, sendResultsTo.tryGet<SendToSelfIntroduced>()) {
        // We had a self-introduction earlier, we just want to reject the fulfillers.
        if (selfIntro.pipelineFulfiller->isWaiting()) {
          selfIntro.pipelineFulfiller->reject(kj::cp(exception));
        }
        selfIntro.responseFulfiller->reject(kj::mv(exception));
        return;
      }

      if (isFirstResponder()) {
        if (connectionState->connection.is<Connected>()) {
          auto message = connectionState->connection.get<Connected>().connection
              ->newOutgoingMessage(messageSizeHint<rpc::Return>() + exceptionSizeHint(exception));
          auto builder = message->getBody().initAs<rpc::Message>().initReturn();

          builder.setAnswerId(answerId);
          builder.setReleaseParamCaps(false);
          connectionState->fromException(exception, builder.initException());

          // Note that even though the response contains no capabilities, we don't want to set
          // `noFinishNeeded` here because if any pipelined calls were made, we want them to
          // fail with the correct exception. (Perhaps if the request had `noPromisePipelining`,
          // then we could set `noFinishNeeded`, but optimizing the error case doesn't seem that
          // important.)

          message->send();
        }

        // Do not allow releasing the pipeline because we want pipelined calls to propagate the
        // exception rather than fail with a "no such field" exception.
        cleanupAnswerTable(nullptr, false);
      }
    }
    void sendRedirectToSelfReturn() {
      KJ_ASSERT(sendResultsTo.is<SendToSelf>());
      KJ_ASSERT(!hints.onlyPromisePipeline);

      if (isFirstResponder()) {
        auto message = connectionState->connection.get<Connected>().connection
            ->newOutgoingMessage(messageSizeHint<rpc::Return>());
        auto builder = message->getBody().initAs<rpc::Message>().initReturn();

        builder.setAnswerId(answerId);
        builder.setReleaseParamCaps(false);
        builder.setResultsSentElsewhere();

        // TODO(now): We have no idea if the response contains capabilities, so we can't use
        //   `noFinishNeeded` as aggressively as usual, but we could probably still use it as
        //   long as `hints.noPromisePipelining` is true. In that case we can assume no pipelined
        //   calls and also assume no disembargo, since an embargo would only be needed after
        //   pipelined calls.

        message->send();

        cleanupAnswerTable(nullptr, false);
      }
    }

    void finish() {
      // Called when a `Finish` message is received while this object still exists.

      receivedFinish = true;
    }

    // implements CallContextHook ------------------------------------

    AnyPointer::Reader getParams() override {
      KJ_REQUIRE(request != kj::none, "Can't call getParams() after releaseParams().");
      return params;
    }
    void releaseParams() override {
      if (sendResultsTo.is<SendToThirdParty>()) {
        // Can't actually release params since the SendToThirdParty points into them.
        // TODO(perf): Maybe we can carefully release if we know we've consumed the redirect
        //   already?
      } else {
        request = kj::none;
      }
    }
    AnyPointer::Builder getResults(kj::Maybe<MessageSize> sizeHint) override {
      KJ_IF_SOME(r, response) {
        return r->getResultsBuilder();
      } else {
        // It's important we are using the correct connection before we initialize any results.
        // But if `getResults()` is called we can be pretty sure we're not going to end up in
        // a tail call so won't be able to forward further anyawy.
        acceptThirdPartyRedirectIfNeeded();

        kj::Own<RpcServerResponse> response;

        if (sendResultsTo.is<SendToSelf>() || sendResultsTo.is<SendToSelfIntroduced>() ||
            !connectionState->connection.is<Connected>()) {
          response = kj::refcounted<LocallyRedirectedRpcResponse>(sizeHint);
        } else {
          auto message = connectionState->connection.get<Connected>().connection
              ->newOutgoingMessage(
                  firstSegmentSize(sizeHint, messageSizeHint<rpc::Return>() +
                                  sizeInWords<rpc::Payload>()));
          returnMessage = message->getBody().initAs<rpc::Message>().initReturn();
          response = kj::heap<RpcServerResponseImpl>(
              *connectionState, kj::mv(message), returnMessage.getResults());
        }

        auto results = response->getResultsBuilder();
        this->response = kj::mv(response);
        return results;
      }
    }
    void setPipeline(kj::Own<PipelineHook>&& pipeline) override {
      // If setPipeline() is called, we can probably guess that we are not likely to end up in
      // a tail call, because if a tail call were the intent, the application would have just done
      // that in the first place, instead of using setPipeline().
      //
      // Moreover, setPipeline() is a great hint that we might be execution a long-running
      // RPC that expects to receive a lot of pipelined calls, such as an http-over-capnp request.
      // In that case we'd really like to shorten the pipeline path promptly without waiting for
      // the call to complete first.
      //
      // So, let's go ahead and accept any third-party redirect now.
      acceptThirdPartyRedirectIfNeeded();

      KJ_IF_SOME(selfIntro, sendResultsTo.tryGet<SendToSelfIntroduced>()) {
        // We saw a three-party redirection to ourselves. We'd like to hook up the pipeline as soon
        // as we can.
        selfIntro.pipelineFulfiller->fulfill(pipeline->addRef());
      }

      KJ_IF_SOME(f, tailCallPipelineFulfiller) {
        f->fulfill(AnyPointer::Pipeline(kj::mv(pipeline)));
      }
    }
    kj::Promise<void> tailCall(kj::Own<RequestHook>&& request) override {
      auto result = directTailCall(kj::mv(request));
      KJ_IF_SOME(f, tailCallPipelineFulfiller) {
        f->fulfill(AnyPointer::Pipeline(kj::mv(result.pipeline)));
      }
      return kj::mv(result.promise);
    }
    ClientHook::VoidPromiseAndPipeline directTailCall(kj::Own<RequestHook>&& request) override {
      KJ_REQUIRE(response == kj::none,
                 "Can't call tailCall() after initializing the results struct.");

      KJ_IF_SOME(rpcRequest, connectionState->unwrapIfSameNetwork(*request)) {
        // Tail-calling to an RPC request on the same network. We can shorten the return path!

        if (hints.onlyPromisePipeline) {
          // Wait, this is an only-promise-pipeline request. Per the documentation of
          // `sendForPipeline()`, we won't do path-shortening. The assumption is that the caller
          // has *already* queued any pipelined calls they want to make and so path-shortening
          // now would be useless, as nothing would actually use the shorter path.
        } else if (sendResultsTo.is<SendToSelf>()) {
          // Actually, there's no return path to shorten. The results are already going to be
          // consumed locally. So don't bother.
        } else if (!connectionState->connection.is<Connected>() ||
                   !rpcRequest.connectionState->connection.is<Connected>()) {
          // Either our connection or the request's connection are disconnecnted so don't try
          // anything fancy.
        } else if (responseSent) {
          // Umm we sent a response already? Better not do anything fancy.
        } else if (sendResultsTo.is<SendToCaller>() &&
                   rpcRequest.isOnConnection(*connectionState)) {
          // The tail call is headed towards the peer that called us in the first place, so we can
          // optimize out the return trip.
          //
          // If the onlyPromisePipeline hint was sent, we skip this trick since the caller will
          // ignore the `Return` message anyway.

          auto initSendResultsTo = [](rpc::Call::SendResultsTo::Builder sendResultsTo) {
            sendResultsTo.setYourself();
          };

          KJ_IF_SOME(tailInfo, rpcRequest.tailSend(initSendResultsTo)) {
            KJ_ASSERT(isFirstResponder());
            auto message = connectionState->connection.get<Connected>().connection
                ->newOutgoingMessage(messageSizeHint<rpc::Return>());
            auto builder = message->getBody().initAs<rpc::Message>().initReturn();

            builder.setAnswerId(answerId);
            builder.setReleaseParamCaps(false);
            builder.setTakeFromOtherQuestion(tailInfo.questionId);

            message->send();

            // There are no caps in our return message, but of course the tail results could have
            // caps, so we must continue to honor pipeline calls (and just bounce them back).
            cleanupAnswerTable(nullptr, false);

            return { kj::mv(tailInfo.promise), kj::mv(tailInfo.pipeline) };
          }
        } else if (sendResultsTo.is<SendToThirdParty>()) {
          // Our own results are supposed to go to a third party. Let's try to arrange to forward
          // the three-party handoff without actually accepting it, so that we avoid creating
          // an unnecessary connection.

          auto contact = sendResultsTo.get<SendToThirdParty>();
          auto& conn = *connectionState->connection.get<Connected>().connection;
          auto& otherConn = *rpcRequest.connectionState->connection.get<Connected>().connection;

          if (conn.canForwardThirdPartyToContact(
              contact, otherConn, ThreePartyHandoffPurpose::CALL_FORWARDING)) {
            // Yay, forwarding is supported.

            auto initSendResultsTo = [&](rpc::Call::SendResultsTo::Builder sendResultsTo) {
              conn.forwardThirdPartyToContact(contact, otherConn,
                  ThreePartyHandoffPurpose::CALL_FORWARDING, sendResultsTo.initThirdParty());
            };

            KJ_IF_SOME(tailInfo, rpcRequest.tailSend(initSendResultsTo)) {
              KJ_ASSERT(isFirstResponder());
              auto message = connectionState->connection.get<Connected>().connection
                  ->newOutgoingMessage(messageSizeHint<rpc::Return>());
              auto builder = message->getBody().initAs<rpc::Message>().initReturn();

              builder.setAnswerId(answerId);
              builder.setReleaseParamCaps(false);

              // TODO(now): Consider if, in the case that `conn` and `otherConn` are actually the
              //   same, we can actually use `takeFromOtherQuestion` here in order to more rapidly
              //   cut this spur off of the path, speeding up the later disembargo...
              builder.setResultsSentElsewhere();

              message->send();

              cleanupAnswerTable(nullptr, false);

              // TODO(now): When forwarding, we don't actually set up any state such that if we
              //   disconnect before seeing the final `Finish`, embargoes are rejected. So, a
              //   disconnect could violate e-order my causing an embargo to resolve even though
              //   one of the pre-embargo pipelined calls was never delivered.

              return { kj::mv(tailInfo.promise), kj::mv(tailInfo.pipeline) };
            }
          }

          // Failed to forward, so we will need to accept the handoff ourselves.
          acceptThirdPartyRedirectIfNeeded();

          // And now we should start over as we're now on a different connection than before so
          // we should re-check everything above!
          return directTailCall(kj::mv(request));
        } else {
          // OK, we are a regular old incoming call returning results to the caller, and we're
          // tail-calling to a third party. Let's initiate a three-party handoff.

          auto& conn = *connectionState->connection.get<Connected>().connection;
          auto& otherConn = *rpcRequest.connectionState->connection.get<Connected>().connection;

          // Note that we are introducing `otherConn` to `conn`, not vice versa, because with call
          // forwarding, the vat that we're forwarding the call to (the callee) is expected to
          // connect back to the caller.
          if (otherConn.canIntroduceTo(conn, ThreePartyHandoffPurpose::CALL_FORWARDING)) {
            // Start building the return message so we can init `awaitFromThirdParty` at the
            // same time as `sendResultsTo`. We'll just have to toss out the message object if
            // we don't end up being able to send it.
            auto message = connectionState->connection.get<Connected>().connection
                ->newOutgoingMessage(messageSizeHint<rpc::Return>());
            auto builder = message->getBody().initAs<rpc::Message>().initReturn();

            auto initSendResultsTo = [&](rpc::Call::SendResultsTo::Builder sendResultsTo) {
              otherConn.introduceTo(conn, ThreePartyHandoffPurpose::CALL_FORWARDING,
                                    sendResultsTo.initThirdParty(),
                                    builder.initAwaitFromThirdParty());
            };

            KJ_IF_SOME(tailInfo, rpcRequest.tailSend(initSendResultsTo)) {
              KJ_ASSERT(isFirstResponder());

              builder.setAnswerId(answerId);
              builder.setReleaseParamCaps(false);

              message->send();

              cleanupAnswerTable(nullptr, false);

              return { kj::mv(tailInfo.promise), kj::mv(tailInfo.pipeline) };
            }
          }
        }
      }

      // Just forwarding to another local call.

      if (hints.onlyPromisePipeline) {
        return {
          kj::NEVER_DONE,
          PipelineHook::from(request->sendForPipeline())
        };
      }

      auto promise = request->send();

      // Wait for response.
      auto voidPromise = promise.then([this](Response<AnyPointer>&& tailResponse) {
        // Copy the response.
        // TODO(perf):  It would be nice if we could somehow make the response get built in-place
        //   but requires some refactoring.
        // TODO(now): Actually this seems important to fix. We don't want a local tail call in the
        //   chain to break three-party forwarding. Can we just pass the same CallContext to the
        //   new call?
        getResults(tailResponse.targetSize()).set(tailResponse);
      });

      return { kj::mv(voidPromise), PipelineHook::from(kj::mv(promise)) };
    }
    kj::Promise<AnyPointer::Pipeline> onTailCall() override {
      auto paf = kj::newPromiseAndFulfiller<AnyPointer::Pipeline>();
      tailCallPipelineFulfiller = kj::mv(paf.fulfiller);
      return kj::mv(paf.promise);
    }
    kj::Own<CallContextHook> addRef() override {
      return kj::addRef(*this);
    }

  private:
    kj::Own<RpcConnectionState> connectionState;
    AnswerId answerId;

    ClientHook::CallHints hints;

    uint64_t interfaceId;
    uint16_t methodId;
    // For debugging.

    bool answerIsInitialized = false;
    // Has the context's answer table entry actually been initialized yet?

    bool acceptedThirdPartyRedirectBeforeInitialized = false;
    // Did acceptThirdPartyRedirectIfNeeded() cause this context to be redirected before
    // initializeAnswer() could be called?

    // Request ---------------------------------------------

    size_t requestSize;  // for flow limit purposes
    kj::Maybe<kj::Own<IncomingRpcMessage>> request;
    ReaderCapabilityTable paramsCapTable;
    AnyPointer::Reader params;

    // Response --------------------------------------------

    struct SendToSelfIntroduced {
      // Special state set in the case that we were told to send our results to a third party, but
      // it turned out the third party is us.
      kj::Own<kj::PromiseFulfiller<kj::Own<RpcResponse>>> responseFulfiller;
      kj::Own<kj::PromiseFulfiller<kj::Own<PipelineHook>>> pipelineFulfiller;
    };
    using ExtendedSendResultsTo =
        kj::OneOf<SendToCaller, SendToSelf, SendToThirdParty, SendToSelfIntroduced>;

    kj::Maybe<kj::Own<RpcServerResponse>> response;
    rpc::Return::Builder returnMessage;
    ExtendedSendResultsTo sendResultsTo;
    bool responseSent = false;
    kj::Maybe<kj::Own<kj::PromiseFulfiller<AnyPointer::Pipeline>>> tailCallPipelineFulfiller;

    // Cancellation state ----------------------------------

    bool receivedFinish = false;
    // True if a `Finish` message has been received OR we sent a `Return` with `noFinishNeeded`.
    // In either case, it is our responsibility to clean up the answer table.

    kj::UnwindDetector unwindDetector;

    // -----------------------------------------------------

    bool isFirstResponder() {
      if (responseSent) {
        return false;
      } else {
        responseSent = true;
        return true;
      }
    }

    void acceptThirdPartyRedirectIfNeeded() {
      // Called when we know we're supposed to redirect to a third party, and we've decided that
      // we certainly want to make the connection from this vat -- we don't want to forward.
      //
      // In this function, we need to connect to the third party, send them the ThirdPartyAnswer to
      // take over the call, and then "transfer ownership" of this context away from the original
      // connection it was on over to the new one.

      // Ignore this call if:
      // - We weren't asked to three-party redirect.
      // - We're not connected anymore. The caller will receive a disconnected exception through
      //   the original path.
      // - We already sent a response.
      // - We already received `Finish`. The call has been canceled before the handoff could occur.
      if (!sendResultsTo.is<SendToThirdParty>() ||
          !connectionState->connection.is<Connected>() ||
          responseSent || receivedFinish) {
        return;
      }

      auto& oldState = connectionState->connection.get<Connected>();
      auto& oldConn = *oldState.connection;
      AnswerId oldAnswerId = answerId;
      Answer& oldAnswer = KJ_ASSERT_NONNULL(connectionState->answers.find(oldAnswerId));

      auto sendOldConnReturn = [&]() {
        // Send Return message on old connection.

        auto msg = oldConn.newOutgoingMessage(messageSizeHint<rpc::Return>());
        auto ret = msg->getBody().initAs<rpc::Message>().initReturn();
        ret.setAnswerId(oldAnswerId);
        ret.setReleaseParamCaps(false);
        ret.setResultsSentElsewhere();
        msg->send();
      };

      // Allocate temporary message to hold ThirdPartyCompletion. Unfortunately we need a temp
      // message here because we can't allocate the actual outgoing message until we have a
      // connection object.
      // TODO(perf): Maybe we can change the signature of connectToIntroduced() to fix this?
      capnp::word scratch[32];
      memset(scratch, 0, sizeof(scratch));
      MallocMessageBuilder message(scratch);
      auto completion = message.getRoot<ThirdPartyCompletion>();

      KJ_IF_SOME(ownNewConn, oldConn.connectToIntroduced(
          sendResultsTo.get<SendToThirdParty>(), completion)) {
        RpcConnectionState& newConnRpc = getConnectionState(oldState.rpcSystem, kj::mv(ownNewConn));

        newConnRpc.setNotIdle();
        auto& newConn = *KJ_ASSERT_NONNULL(newConnRpc.connection.tryGet<Connected>()).connection;

        AnswerId newAnswerId;
        Answer& newAnswer = newConnRpc.answers.nextReverseAllocated(newAnswerId);

        kj::Maybe<kj::Own<kj::PromiseFulfiller<void>>> embargo;

        if (!answerIsInitialized) {
          // `oldAnswer` isn't actually fully initialized yet, so we'd better tread carefully
          // here. This can happen if `startCall()` is actually still on the stack. Set a flag to
          // fix this later.
          acceptedThirdPartyRedirectBeforeInitialized = true;
        }

        // Clone the pipeline, if there is one.
        // Note that there might not be one in the case of a `noPromisePipelining` request.
        if (hints.noPromisePipelining) {
          newAnswer.pipeline = getDisabledPipeline();
        } else {
          KJ_IF_SOME(p, oldAnswer.pipeline) {
            newAnswer.pipeline = p->addRef();
          } else {
            // Probably, !answerIsInitialized, but in any case, leave `newAnswer.pipeline` null
            // for now, matching `oldAnswer.pipeline`.
          }

          // Since there is a pipeline, we could be asked to embargo it. Arrange to be informed
          // when the original question receives a `Finish`, which implicitly lifts the embargo.
          auto paf = kj::newPromiseAndFulfiller<void>();
          newAnswer.thirdPartyFinishPromise = kj::mv(paf.promise);
          embargo = kj::mv(paf.fulfiller);
        }

        // Transfer the context. (Since we're sending a Return on the old path, it's the correct
        // time to null out its context.)
        newAnswer.callContext = kj::mv(oldAnswer.callContext);

        // Transfer the task. Note that if !answerIsInitialized, `oldAnswer.task` is probably
        // null; we'll fix newAnswer.task later.
        newAnswer.task = kj::mv(oldAnswer.task);

        // Replace old task with a placeholder that drops the embargo when `Finish` is received.
        // If !answerIsInitialized, don't worry, we'll make sure this doesn't get overwritten in
        // `initializeAnswer()`.
        oldAnswer.task = Answer::RedirectedToThirdParty { kj::mv(embargo) };

        // Change our `connectionState` to be the new connection (whoa).
        auto oldConnectionState = kj::mv(connectionState);
        connectionState = kj::addRef(newConnRpc);

        answerId = newAnswerId;

        // We've effectively changed our "caller" to an entirely different connection, and now
        // we want to send the results to that caller!
        sendResultsTo = SendToCaller();

        // Send Return message on old connection.
        sendOldConnReturn();

        // Send ThirdPartyAnswer message.
        {
          auto msg = newConn.newOutgoingMessage(
              messageSizeHint<rpc::ThirdPartyAnswer>() + completion.targetSize().wordCount);
          auto tpa = msg->getBody().initAs<rpc::Message>().initThirdPartyAnswer();
          tpa.setAnswerId(newAnswerId);
          tpa.getCompletion().set(completion);
          msg->send();
        }

      } else {
        // We're being introduced to ourselves.

        // Send Return message on old connection.
        KJ_ASSERT(isFirstResponder());
        sendOldConnReturn();

        // sendReturn() or sendError() is still going to be called later (as part of our answer
        // task). Arrange that when they are, we'll get these fulfillers fulfilled with the
        // results. Also, if `setPipeline()` is called, the pipeline can be fulfilled sooner.
        auto responsePaf = kj::newPromiseAndFulfiller<kj::Own<RpcResponse>>();
        auto pipelinePaf = kj::newPromiseAndFulfiller<kj::Own<PipelineHook>>();
        sendResultsTo = SendToSelfIntroduced {
          .responseFulfiller = kj::mv(responsePaf.fulfiller),
          .pipelineFulfiller = kj::mv(pipelinePaf.fulfiller),
        };

        // Create a background task to wait for the introduction to complete, and hook everything
        // up there.
        connectionState->tasks.add(oldState.canceler->wrap(
            oldState.connection->completeThirdParty(completion))
            .then([responsePromise = kj::mv(responsePaf.promise),
                   pipelinePromise = kj::mv(pipelinePaf.promise)]
                  (kj::Rc<kj::Refcounted> holder) mutable {
          kj::Rc<ThirdPartyExchangeValue> xcghValue = holder.downcast<ThirdPartyExchangeValue>();

          auto& call = KJ_REQUIRE_NONNULL(
              xcghValue->value.tryGet<ThirdPartyExchangeValue::Call>(),
              "ThirdPartyCompletion given in ThirdPartyAnswer did not match a redirected Return.");

          KJ_IF_SOME(origQuestion, call.question) {
            // Calling redirect() might release all remaining references to `origQuestion`, so make
            // sure we have one of our own.
            kj::addRef(origQuestion)->redirect(kj::mv(responsePromise),
                newLocalPromisePipeline(kj::mv(pipelinePromise)));
          } else {
            // Original call was canceled, so... do nothing.
          }
        }));
      }
    }

    void cleanupAnswerTable(kj::Array<ExportId> resultExports, bool shouldFreePipeline) {
      // We need to remove the `callContext` pointer -- which points back to us -- from the
      // answer table.  Or we might even be responsible for removing the entire answer table
      // entry.

      if (receivedFinish) {
        // Already received `Finish` so it's our job to erase the table entry. We shouldn't have
        // sent results if canceled, so we shouldn't have an export list to deal with.
        KJ_ASSERT(resultExports.size() == 0);
        connectionState->answers.erase(answerId);
      } else {
        // We just have to null out callContext and set the exports.
        auto& answer = KJ_ASSERT_NONNULL(connectionState->answers.find(answerId));
        answer.callContext = kj::none;
        answer.resultExports = kj::mv(resultExports);

        if (shouldFreePipeline) {
          // We can free the pipeline early, because we know all pipeline calls are invalid (e.g.
          // because there are no caps in the result to receive pipeline requests).
          KJ_ASSERT(resultExports.size() == 0);
          answer.pipeline = kj::none;
        }
      }

      // Also, this is the right time to stop counting the call against the flow limit.
      connectionState->callWordsInFlight -= requestSize;
      connectionState->maybeUnblockFlow();

      // If we erased the answer table entry above, check for idleness now.
      if (receivedFinish) {
        connectionState->checkIfBecameIdle();
      }
    }
  };

  // =====================================================================================
  // Message handling

  void maybeUnblockFlow() {
    if (callWordsInFlight < flowLimit) {
      KJ_IF_SOME(w, flowWaiter) {
        w->fulfill();
        flowWaiter = kj::none;
      }
    }
  }

  kj::Promise<void> messageLoop() {
    for (;;) {
      if (!connection.is<Connected>()) {
        co_return;
      }

      if (callWordsInFlight > flowLimit) {
        auto paf = kj::newPromiseAndFulfiller<void>();
        flowWaiter = kj::mv(paf.fulfiller);
        co_await paf.promise;
        continue;
      }

      auto& connected = connection.get<Connected>();

      kj::Maybe<kj::Own<IncomingRpcMessage>> message;
      try {
        message = co_await connected.canceler->wrap(connected.connection->receiveIncomingMessage());
      } catch (...) {
        receiveIncomingMessageError = true;
        throw;
      }

      KJ_IF_SOME(m, message) {
        handleMessage(kj::mv(m));
      } else {
        // Other end disconnected!

        if (idle) {
          // There were no outstanding capabilities or anything, so we don't have to propagate any
          // exceptions anywhere. We can just remove ourselves from the connection map.

          if (!connection.is<Connected>()) {
            // Hmm, we're already in the Disconnected state. Odd that the message loop is still
            // running at all but we should just let it end.
            co_return;
          }

          Connected& state = connection.get<Connected>();

          // At this point, the last reference to this connection state *should* be the one in
          // the RpcSystem's map. The refcount should therefore be 1, and `isShared()`.
          if (isShared()) {
            // Oh, we still have references. We will need to set ourselves to the "disconnected"
            // state.
            // TODO(bug): Previously, I had a KJ_LOG(ERROR) here, and it did actually show up in
            //   production, but I couldn't tell what was holding the reference. Hopefully,
            //   propagating an explicit exception here will give us a stack trace that tells us
            //   what's holding onto the connection.
            tasks.add(KJ_EXCEPTION(FAILED,
                "RpcSystem bug: Connection shut down due to being idle, but if you're seeing "
                "this error then apparently something was still using the connection. Please "
                "take note of the stack and fix checkIfBecameIdle() to account for this kind of "
                "reference still existing."));
            co_return;
          }

          // Remove ourselves from the RpcSystem's table *now*. But have it create a shutdown
          // task which will keep this object alive until `tasks` is empty. Note that
          // `messageLoop()` itself is in `tasks`, so this will wait until `messageLoop()`
          // finishes.
          dropConnection(state.rpcSystem, *state.connection,
              tasks.onEmpty().attach(kj::addRef(*this)));

          // Shut down connection cleanly.
          co_await state.connection->shutdown();

          co_return;
        } else {
          // Need to raise an error in order to tear everything down.
          tasks.add(KJ_EXCEPTION(DISCONNECTED, "Peer disconnected."));
          co_return;
        }
      }

      // TODO(perf): We add a yield() here so that anything we needed to do in reaction to the
      //   previous message has a chance to complete before the next message is handled. In
      //   particular, without this, I observed an ordering problem: I saw a case where a `Return`
      //   message was followed by a `Resolve` message, but the `PromiseClient` associated with the
      //   `Resolve` had its `resolve()` method invoked _before_ any `PromiseClient`s associated
      //   with pipelined capabilities resolved by the `Return`. This could lead to an
      //   incorrectly-ordered interaction between `PromiseClient`s when they resolve to each
      //   other. This is probably really a bug in the way `Return`s are handled -- apparently,
      //   resolution of `PromiseClient`s based on returned capabilities does not occur in a
      //   depth-first way, when it should. If we could fix that then we can probably remove this
      //   `yield()`. However, the `yield()` is not that bad and solves the problem...
      //
      //   Another possible solution would be to make resolutions not be promise-based. That is,
      //   when a `Resolve` message is received, the appropriate RpcPromise::resolve() should be
      //   invoked synchronously from `handleResolve()`. But also, when a `Return` is received,
      //   the `RpcPipeline` needs to synchronously resolve everything in its `clientMap`. This
      //   would be a large-ish refactoring, but would probably be good for performance as there
      //   would be far fewer event loop tasks needed on each Return / Resolve (and a lot less
      //   allocation).
      co_await kj::yield();
    }
  }

  void handleMessage(kj::Own<IncomingRpcMessage> message) {
    // If we were idle before, receiving a message changes that. This is true even if we received
    // a message that makes no sense while idle, because we need to send an error in response, and
    // sending any message requires we are not idle.
    setNotIdle();

    auto reader = message->getBody().getAs<rpc::Message>();

    switch (reader.which()) {
      case rpc::Message::UNIMPLEMENTED:
        handleUnimplemented(reader.getUnimplemented());
        break;

      case rpc::Message::ABORT:
        handleAbort(reader.getAbort());
        break;

      case rpc::Message::BOOTSTRAP:
        handleBootstrap(kj::mv(message), reader.getBootstrap());
        break;

      case rpc::Message::CALL:
        handleCall(kj::mv(message), reader.getCall());
        break;

      case rpc::Message::RETURN:
        handleReturn(kj::mv(message), reader.getReturn());
        break;

      case rpc::Message::FINISH:
        handleFinish(reader.getFinish());
        break;

      case rpc::Message::RESOLVE:
        handleResolve(kj::mv(message), reader.getResolve());
        break;

      case rpc::Message::RELEASE:
        handleRelease(reader.getRelease());
        break;

      case rpc::Message::DISEMBARGO:
        handleDisembargo(reader.getDisembargo());
        break;

      case rpc::Message::PROVIDE:
        handleProvide(reader.getProvide());
        break;

      case rpc::Message::ACCEPT:
        handleAccept(reader.getAccept());
        break;

      case rpc::Message::THIRD_PARTY_ANSWER:
        handleThirdPartyAnswer(reader.getThirdPartyAnswer());
        break;

      case rpc::Message::THIRD_PARTY_ANSWER_EMBARGO:
        handleThirdPartyAnswerEmbargo(reader.getThirdPartyAnswerEmbargo());
        break;

      case rpc::Message::THIRD_PARTY_ANSWER_DISEMBARGO:
        handleThirdPartyAnswerDisembargo(reader.getThirdPartyAnswerDisembargo());
        break;

      default: {
        if (connection.is<Connected>()) {
          auto message = connection.get<Connected>().connection->newOutgoingMessage(
              firstSegmentSize(reader.totalSize(), messageSizeHint<void>()));
          message->getBody().initAs<rpc::Message>().setUnimplemented(reader);
          message->send();
        }
        break;
      }
    }

    // We need a checkIfBecameIdle() here mainly to handle the case that we were idle before
    // receiving this message, and the message itself didn't actually add anything to the tables,
    // so we are still idle.
    //
    // Note that most message types should inherently fail if they are received while idle (e.g.
    // handleCall() would fail because it targets a capability on the export table, but when idle
    // the export table is empty). In those cases, an exception will be thrown which will cause
    // us to abort the connection.
    checkIfBecameIdle();
  }

  void handleUnimplemented(const rpc::Message::Reader& message) {
    switch (message.which()) {
      case rpc::Message::RESOLVE: {
        auto resolve = message.getResolve();
        switch (resolve.which()) {
          case rpc::Resolve::CAP: {
            auto cap = resolve.getCap();
            switch (cap.which()) {
              case rpc::CapDescriptor::NONE:
                // Nothing to do (but this ought never to happen).
                break;
              case rpc::CapDescriptor::SENDER_HOSTED:
                releaseExport(cap.getSenderHosted(), 1);
                break;
              case rpc::CapDescriptor::SENDER_PROMISE:
                releaseExport(cap.getSenderPromise(), 1);
                break;
              case rpc::CapDescriptor::RECEIVER_ANSWER:
              case rpc::CapDescriptor::RECEIVER_HOSTED:
                // Nothing to do.
                break;
              case rpc::CapDescriptor::THIRD_PARTY_HOSTED:
                releaseExport(cap.getThirdPartyHosted().getVineId(), 1);
                break;
            }
            break;
          }
          case rpc::Resolve::EXCEPTION:
            // Nothing to do.
            break;
        }
        break;
      }

      default:
        KJ_FAIL_ASSERT("Peer did not implement required RPC message type.", (uint)message.which());
        break;
    }
  }

  void handleAbort(const rpc::Exception::Reader& exception) {
    kj::throwRecoverableException(toException(exception));
  }

  // ---------------------------------------------------------------------------
  // Level 0

  class SingleCapPipeline: public PipelineHook, public kj::Refcounted {
  public:
    SingleCapPipeline(kj::Own<ClientHook>&& cap)
        : cap(kj::mv(cap)) {}

    kj::Own<PipelineHook> addRef() override {
      return kj::addRef(*this);
    }

    kj::Own<ClientHook> getPipelinedCap(kj::ArrayPtr<const PipelineOp> ops) override {
      if (ops.size() == 0) {
        return cap->addRef();
      } else {
        return newBrokenCap("Invalid pipeline transform.");
      }
    }

  private:
    kj::Own<ClientHook> cap;
  };

  void handleBootstrap(kj::Own<IncomingRpcMessage>&& message,
                       const rpc::Bootstrap::Reader& bootstrap) {
    AnswerId answerId = bootstrap.getQuestionId();

    if (!connection.is<Connected>()) {
      // Disconnected; ignore.
      return;
    }

    VatNetworkBase::Connection& conn = *connection.get<Connected>().connection;
    auto response = conn.newOutgoingMessage(
        messageSizeHint<rpc::Return>() + sizeInWords<rpc::CapDescriptor>() + 32);

    rpc::Return::Builder ret = response->getBody().getAs<rpc::Message>().initReturn();
    ret.setAnswerId(answerId);

    kj::Own<ClientHook> capHook;
    kj::Array<ExportId> resultExports;
    KJ_DEFER(releaseExports(resultExports));  // in case something goes wrong

    // Call the restorer and initialize the answer.
    KJ_IF_SOME(exception, kj::runCatchingExceptions([&]() {
      Capability::Client cap = nullptr;

      if (bootstrap.hasDeprecatedObjectId()) {
        KJ_FAIL_REQUIRE("This vat only supports a bootstrap interface, not the old "
                        "Cap'n-Proto-0.4-style named exports.") { return; }
      } else {
        cap = bootstrapFactory.baseCreateFor(conn.baseGetPeerVatId());
      }

      BuilderCapabilityTable capTable;
      auto payload = ret.initResults();
      capTable.imbue(payload.getContent()).setAs<Capability>(kj::mv(cap));

      auto capTableArray = capTable.getTable();
      KJ_DASSERT(capTableArray.size() == 1);
      kj::Vector<int> fds;
      kj::HashMap<ClientHook*, kj::Own<ClientHook>> resolutionsAtReturnTime;
      resultExports = writeDescriptors(capTableArray, payload, fds, resolutionsAtReturnTime);
      response->setFds(fds.releaseAsArray());
      capHook = kj::mv(KJ_ASSERT_NONNULL(capTableArray[0]));

      // If we're returning a capability that turns out to be an PromiseClient pointing back on
      // this same network, it's important we remove the `PromiseClient` layer and use the inner
      // capability instead. This achieves the same effect that `PostReturnRpcPipeline` does for
      // regular call returns.
      //
      // This single line of code represents two hours of my life.
      KJ_IF_SOME(replacement, resolutionsAtReturnTime.find(capHook)) {
        capHook = replacement->addRef();
      }
    })) {
      fromException(exception, ret.initException());
      capHook = newBrokenCap(kj::mv(exception));
    }

    message = nullptr;

    // Add the answer to the answer table for pipelining and send the response.
    auto& answer = KJ_ASSERT_NONNULL(answers.create(answerId),
                                     "questionId is already in use", answerId);

    answer.resultExports = kj::mv(resultExports);
    answer.pipeline = kj::Own<PipelineHook>(kj::refcounted<SingleCapPipeline>(kj::mv(capHook)));

    response->send();
  }

  void handleCall(kj::Own<IncomingRpcMessage>&& message, const rpc::Call::Reader& call) {
    kj::Own<ClientHook> capability = getMessageTarget(call.getTarget());

    SendResultsTo sendResultsTo;
    auto srt = call.getSendResultsTo();
    switch (srt.which()) {
      case rpc::Call::SendResultsTo::CALLER:
        sendResultsTo.init<SendToCaller>();
        break;
      case rpc::Call::SendResultsTo::YOURSELF:
        sendResultsTo.init<SendToSelf>();
        break;
      case rpc::Call::SendResultsTo::THIRD_PARTY:
        sendResultsTo.init<SendToThirdParty>(srt.getThirdParty());
        break;
      default:
        KJ_FAIL_REQUIRE("Unsupported `Call.sendResultsTo`.");
    }

    auto payload = call.getParams();
    auto capTableArray = receiveCaps(payload.getCapTable(), message->getAttachedFds());

    AnswerId answerId = call.getQuestionId();

    auto hints = callHintsFromReader(call);

    // Don't honor onlyPromisePipeline if results are redirected, because this situation isn't
    // useful in practice and would be complicated to handle "correctly".
    if (sendResultsTo.is<SendToSelf>()) hints.onlyPromisePipeline = false;

    auto context = kj::refcounted<RpcCallContext>(
        *this, answerId, kj::mv(message), kj::mv(capTableArray), payload.getContent(),
        sendResultsTo, call.getInterfaceId(), call.getMethodId(), hints);

    // No more using `call` after this point, as it now belongs to the context.

    {
      auto& answer = KJ_ASSERT_NONNULL(answers.create(answerId),
                                       "questionId is already in use", answerId);

      answer.callContext = *context;
    }

    auto promiseAndPipeline = startCall(
        call.getInterfaceId(), call.getMethodId(), kj::mv(capability), context->addRef(), hints);

    // Things may have changed -- in particular if startCall() immediately called
    // context->directTailCall().

    {
      auto& answer = KJ_ASSERT_NONNULL(answers.find(answerId));
      RpcCallContext& contextRef = *context;

      decltype(answer.task) task;
      if (sendResultsTo.is<SendToSelf>()) {
        task = promiseAndPipeline.promise.then(
            [context=kj::mv(context)]() mutable {
              return context->consumeRedirectedToSelfResponse();
            });
      } else if (hints.onlyPromisePipeline) {
        // The promise is probably fake anyway, so don't bother adding a .then(). We do, however,
        // have to attach `context` to this, since we destroy `task` upon receiving a `Finish`
        // message, and we want `RpcCallContext` to be destroyed no earlier than that.
        task = promiseAndPipeline.promise.attach(kj::mv(context));
      } else {
        // Hack:  Both the success and error continuations need to use the context.  We could
        //   refcount, but both will be destroyed at the same time anyway.
        task = promiseAndPipeline.promise.then(
            [context = kj::mv(context)]() mutable {
              context->sendReturn();
            }, [&contextRef](kj::Exception&& exception) {
              contextRef.sendErrorReturn(kj::mv(exception));
            }).eagerlyEvaluate([&](kj::Exception&& exception) {
              // Handle exceptions that occur in sendReturn()/sendErrorReturn().
              tasks.add(kj::mv(exception));
            });
      }

      contextRef.initializeAnswer(answer, kj::mv(promiseAndPipeline.pipeline), kj::mv(task));
    }
  }

  ClientHook::VoidPromiseAndPipeline startCall(
      uint64_t interfaceId, uint64_t methodId,
      kj::Own<ClientHook>&& capability, kj::Own<CallContextHook>&& context,
      ClientHook::CallHints hints) {
    return capability->call(interfaceId, methodId, kj::mv(context), hints);
  }

  kj::Own<ClientHook> getMessageTarget(const rpc::MessageTarget::Reader& target) {
    switch (target.which()) {
      case rpc::MessageTarget::IMPORTED_CAP: {
        KJ_IF_SOME(exp, exports.find(target.getImportedCap())) {
          return exp.clientHook->addRef();
        } else {
          KJ_FAIL_REQUIRE("Message target is not a current export ID.");
        }
        break;
      }

      case rpc::MessageTarget::PROMISED_ANSWER: {
        auto promisedAnswer = target.getPromisedAnswer();
        kj::Own<PipelineHook> pipeline;

        KJ_IF_SOME(answer, answers.find(promisedAnswer.getQuestionId())) {
          KJ_IF_SOME(p, answer.pipeline) {
            pipeline = p->addRef();
          }
        }
        if (pipeline.get() == nullptr) {
          pipeline = newBrokenPipeline(KJ_EXCEPTION(FAILED,
              "Pipeline call on a request that returned no capabilities or was already closed."));
        }

        auto ops = toPipelineOps(promisedAnswer.getTransform());
        return pipeline->getPipelinedCap(ops);
      }

      default:
        KJ_FAIL_REQUIRE("Unknown message target type.", target);
    }

    KJ_UNREACHABLE;
  }

  void handleReturn(kj::Own<IncomingRpcMessage>&& message, const rpc::Return::Reader& ret) {
    // Transitive destructors can end up manipulating the question table and invalidating our
    // pointer into it, so make sure these destructors run later.
    kj::Array<ExportId> exportsToRelease;
    KJ_DEFER(releaseExports(exportsToRelease));
    kj::Maybe<decltype(Answer::task)> promiseToRelease;

    QuestionId questionId = ret.getAnswerId();
    if (questions.isOneWay(questionId)) {
      // We sent hints with this question saying we didn't want a `Return` but we got one anyway.
      // We cannot even look up the question on the question table because it's (remotely) possible
      // that we already removed it and re-allocated the ID to something else. So, we should ignore
      // the `Return`. But we might want to make note to stop using these hints, to protect against
      // the (again, remote) possibility of our ID space wrapping around and leading to confusion.
      if (ret.getReleaseParamCaps() && sentCapabilitiesInPipelineOnlyCall) {
        // Oh no, it appears the peer wants us to release any capabilities in the params, something
        // which only a level 0 peer would request (no version of the C++ RPC system has ever done
        // this). And it appears we did send capabilities in at least one pipeline-only call
        // previously. But we have no record of which capabilities were sent in *this* call, so
        // we cannot release them. Log an error about the leak.
        //
        // This scenario is unlikely to happen in practice, because sendForPipeline() is not useful
        // when talking to a peer that doesn't support capability-passing -- they couldn't possibly
        // return a capability to pipeline on! So, I'm not going to spend time to find a solution
        // for this corner case. We will log an error, though, just in case someone hits this
        // somehow.
        KJ_LOG(ERROR,
            "sendForPipeline() was used when sending an RPC to a peer, the parameters of that "
            "RPC included capabilities, but the peer seems to implement Cap'n Proto at level 0, "
            "meaning it does not support capability passing (or, at least, it sent a `Return` "
            "with `releaseParamCaps = true`). The capabilities that were sent may have been "
            "leaked (they won't be dropped until the connection closes).");

        sentCapabilitiesInPipelineOnlyCall = false;  // don't log again
      }
      gotReturnForHighQuestionId = true;
      return;
    }

    KJ_IF_SOME(question, questions.find(questionId)) {
      KJ_REQUIRE(question.isAwaitingReturn, "Duplicate Return.");
      question.isAwaitingReturn = false;

      if (ret.getReleaseParamCaps()) {
        exportsToRelease = kj::mv(question.paramExports);
      } else {
        question.paramExports = nullptr;
      }

      if (ret.getNoFinishNeeded()) {
        question.skipFinish = true;
      }

      KJ_IF_SOME(questionRef, question.selfRef) {
        // Fulfilling or rejecting the question may cause it to destroy itself. We must hold an
        // extra reference to prevent that.
        auto ownQuestionRef = kj::addRef(questionRef);

        switch (ret.which()) {
          case rpc::Return::RESULTS: {
            KJ_REQUIRE(!question.isTailCall,
                "Tail call `Return` must set `resultsSentElsewhere`, not `results`.");

            auto payload = ret.getResults();
            auto capTableArray = receiveCaps(payload.getCapTable(), message->getAttachedFds());
            questionRef.fulfill(kj::refcounted<RpcResponseImpl>(
                *this, kj::addRef(questionRef), kj::mv(message),
                kj::mv(capTableArray), payload.getContent()));
            break;
          }

          case rpc::Return::EXCEPTION:
            KJ_REQUIRE(!question.isTailCall,
                "Tail call `Return` must set `resultsSentElsewhere`, not `exception`.");

            questionRef.reject(toException(ret.getException()));
            break;

          case rpc::Return::CANCELED:
            KJ_FAIL_REQUIRE("Return message falsely claims call was canceled.");
            break;

          case rpc::Return::RESULTS_SENT_ELSEWHERE:
            KJ_REQUIRE(question.isTailCall,
                "`Return` had `resultsSentElsewhere` but this was not a tail call.");

            questionRef.fulfillTailCall();
            break;

          case rpc::Return::TAKE_FROM_OTHER_QUESTION:
            KJ_IF_SOME(answer, answers.find(ret.getTakeFromOtherQuestion())) {
              KJ_IF_SOME(response, answer.task.tryGet<Answer::RedirectedToSelf>()) {
                questionRef.fulfill(kj::mv(response));
                answer.task = Answer::Finished();

                KJ_IF_SOME(context, answer.callContext) {
                  // Send the `Return` message  for the call of which we're taking ownership, so
                  // that the peer knows it can now tear down the call state.
                  context.sendRedirectToSelfReturn();
                }
              } else {
                KJ_FAIL_REQUIRE("`Return.takeFromOtherQuestion` referenced a call that did not "
                                "use `sendResultsTo.yourself`.");
              }
            } else {
              KJ_FAIL_REQUIRE("`Return.takeFromOtherQuestion` had invalid answer ID.");
            }

            break;

          case rpc::Return::AWAIT_FROM_THIRD_PARTY: {
            if (!connection.is<Connected>()) {
              // Disconnected; ignore.
              return;
            }

            auto& conn = *connection.get<Connected>().connection;

            auto xchgValue = kj::rc<ThirdPartyExchangeValue>();
            xchgValue->value.init<ThirdPartyExchangeValue::Call>(questionRef);
            auto handle = conn.awaitThirdParty(ret.getAwaitFromThirdParty(), xchgValue.addRef());

            // Make sure if we're canceled, we null out `question` is `xchangeValue`, just in
            // case the value is in the process of being delivered (i.e. in the event loop) so
            // canceling the exchange doesn't actually halt the delivery.
            handle = handle.attach(kj::defer([xchgValue = kj::mv(xchgValue)]() mutable {
              xchgValue->value.get<ThirdPartyExchangeValue::Call>().question = kj::none;
            }));

            // If the question is canceled, we'll stop waiting.
            questionRef.setDropWhenDone(kj::mv(handle));
            break;
          }

          default:
            KJ_FAIL_REQUIRE("Unknown 'Return' type.");
        }
      } else {
        // This is a response to a question that we canceled earlier.

        if (ret.isTakeFromOtherQuestion()) {
          // This turned out to be a tail call back to us! We now take ownership of the tail call.
          // Since the caller canceled, we need to cancel out the tail call, if it still exists.

          KJ_IF_SOME(answer, answers.find(ret.getTakeFromOtherQuestion())) {
            // Indeed, it does still exist.

            // Throw away the result promise.
            promiseToRelease = kj::mv(answer.task);

            KJ_IF_SOME(context, answer.callContext) {
              // Send the `Return` message  for the call of which we're taking ownership, so
              // that the peer knows it can now tear down the call state.
              context.sendRedirectToSelfReturn();
            }
          }
        }

        // Looks like this question was canceled earlier, so `Finish` was already sent, with
        // `releaseResultCaps` set true so that we don't have to release them here.  We can go
        // ahead and delete it from the table.
        questions.erase(ret.getAnswerId(), question);
        checkIfBecameIdle();
      }

    } else {
      KJ_FAIL_REQUIRE("Invalid question ID in Return message.");
    }
  }

  void handleFinish(const rpc::Finish::Reader& finish) {
    // Delay release of these things until return so that transitive destructors don't accidentally
    // modify the answer table and invalidate our pointer into it.
    kj::Array<ExportId> exportsToRelease;
    KJ_DEFER(releaseExports(exportsToRelease));
    Answer answerToRelease;
    kj::Maybe<kj::Own<PipelineHook>> pipelineToRelease;
    kj::Maybe<decltype(Answer::task)> promiseToRelease;

    KJ_IF_SOME(answer, answers.find(finish.getQuestionId())) {
      if (finish.getReleaseResultCaps()) {
        exportsToRelease = kj::mv(answer.resultExports);
      } else {
        answer.resultExports = nullptr;
      }

      pipelineToRelease = kj::mv(answer.pipeline);

      // If this was a three-party redirected answer, signal that the Finish message was received,
      // thus lifting the embargo (if any).
      KJ_IF_SOME(redirect, answer.task.tryGet<Answer::RedirectedToThirdParty>()) {
        KJ_IF_SOME(e, redirect.embargo) {
          e->fulfill();
        }
      }

      KJ_IF_SOME(context, answer.callContext) {
        // Destroying answer.task will probably destroy the call context, but we can't prove that
        // since it's refcounted. Instead, inform the call context that it is now its job to
        // clean up the answer table. Then, cancel the task.
        promiseToRelease = kj::mv(answer.task);
        answer.task = Answer::Finished();
        context.finish();
      } else {
        // The call context is already gone so we can tear down the Answer here.
        answerToRelease = answers.erase(finish.getQuestionId());
        checkIfBecameIdle();
      }
    } else {
      // The `Finish` message targets a question ID that isn't present in our answer table.
      // Probably, we send a `Return` with `noFinishNeeded = true`, but the other side didn't
      // recognize this hint and sent a `Finish` anyway, or the `Finish` was already in-flight at
      // the time we sent the `Return`. We can silently ignore this.
      //
      // It would be nice to detect invalid finishes somehow, but to do so we would have to
      // remember past answer IDs somewhere even when we said `noFinishNeeded`. Assuming the other
      // side respects the hint and doesn't send a `Finish`, we'd only be able to clean up these
      // records when the other end reuses the question ID, which might never happen.
    }

    if (finish.getRequireEarlyCancellationWorkaround()) {
      // Defer actual cancellation of the call until the end of the event loop queue.
      //
      // This is needed for compatibility with older versions of Cap'n Proto (0.10 and prior) in
      // which the default was to prohibit cancellation until it was explicitly allowed. In newer
      // versions (1.0 and later) cancellation is allowed until explicitly prohibited, that is, if
      // we haven't actually delivered the call yet, it can be canceled. This requires less
      // bookkeeping and so improved performance.
      //
      // However, old clients might be inadvertently relying on the old behavior. For example, if
      // someone using and old version called `.send()` on a message and then promptly dropped the
      // returned Promise, the message would often be delivered. This was not intended to work, but
      // did, and could be relied upon by accident. Moreover, the original implementation of
      // streaming included a bug where streaming calls *always* sent an immediate Finish.
      //
      // By deferring cancellation until after a turn of the event loop, we provide an opportunity
      // for any `Call` messages we've received to actually be delivered, so that they can opt out
      // of cancellation if desired.
      KJ_IF_SOME(task, promiseToRelease) {
        KJ_IF_SOME(running, task.tryGet<Answer::Running>()) {
          tasks.add(kj::evalLast([running = kj::mv(running)]() {
            // Just drop `running` here to cancel the call.
          }));
        }
      }
    }
  }

  // ---------------------------------------------------------------------------
  // Level 1

  void handleResolve(kj::Own<IncomingRpcMessage>&& message, const rpc::Resolve::Reader& resolve) {
    kj::Own<ClientHook> replacement;
    kj::Maybe<kj::Exception> exception;

    // Extract the replacement capability.
    switch (resolve.which()) {
      case rpc::Resolve::CAP:
        KJ_IF_SOME(cap, receiveCap(resolve.getCap(), message->getAttachedFds())) {
          replacement = kj::mv(cap);
        } else {
          KJ_FAIL_REQUIRE("'Resolve' contained 'CapDescriptor.none'.");
        }
        break;

      case rpc::Resolve::EXCEPTION:
        // We can't set `replacement` to a new broken cap here because this will confuse
        // PromiseClient::Resolve() into thinking that the remote promise resolved to a local
        // capability and therefore a Disembargo is needed. We must actually reject the promise.
        exception = toException(resolve.getException());
        break;

      default:
        KJ_FAIL_REQUIRE("Unknown 'Resolve' type.");
    }

    // If the import is on the table, fulfill it.
    KJ_IF_SOME(import, imports.find(resolve.getPromiseId())) {
      KJ_IF_SOME(promise, import.promiseClient) {
        // OK, this is in fact an unfulfilled promise!
        KJ_IF_SOME(e, exception) {
          promise.reject(kj::mv(e));
        } else {
          promise.resolveImport(kj::mv(replacement));
        }
      } else {
        // This import table entry does not appear to have a promise attached. Probably, it
        // actually was a promise in the past, but the `PromiseClient` wrapper has been discarded,
        // while the underlying `ImportClient` has not. In this case, we can just ignore the
        // resolution.
      }
    }
  }

  void handleRelease(const rpc::Release::Reader& release) {
    releaseExport(release.getId(), release.getReferenceCount());
  }

  void releaseExport(ExportId id, uint refcount) {
    KJ_IF_SOME(exp, exports.find(id)) {
      KJ_REQUIRE(refcount <= exp.refcount, "Tried to drop export's refcount below zero.");

      exp.refcount -= refcount;
      if (exp.refcount == 0) {
        if (exp.canonical) {
          KJ_ASSERT(exportsByCap.erase(exp.clientHook));
        }
        exports.erase(id, exp);
        checkIfBecameIdle();
      }
    } else {
      KJ_FAIL_REQUIRE("Tried to release invalid export ID.");
    }
  }

  void releaseExports(kj::ArrayPtr<ExportId> exports) {
    for (auto exportId: exports) {
      releaseExport(exportId, 1);
    }
  }

  void handleDisembargo(const rpc::Disembargo::Reader& disembargo) {
    auto context = disembargo.getContext();
    switch (context.which()) {
      case rpc::Disembargo::Context::SENDER_LOOPBACK: {
        kj::Own<ClientHook> target = getMessageTarget(disembargo.getTarget());

        EmbargoId embargoId = context.getSenderLoopback();

        // It's possible that `target` is a promise capability that hasn't resolved yet, in which
        // case we must wait for the resolution. In particular this can happen in the case where
        // we have Alice -> Bob -> Carol, Alice makes a call that proxies from Bob to Carol, and
        // Carol returns a capability from this call that points all the way back though Bob to
        // Alice. When this return capability passes through Bob, Bob will resolve the previous
        // promise-pipeline capability to it. However, Bob has to send a Disembargo to Carol before
        // completing this resolution. In the meantime, though, Bob returns the final response to
        // Alice. Alice then *also* sends a Disembargo to Bob. The Alice -> Bob Disembargo might
        // arrive at Bob before the Bob -> Carol Disembargo has resolved, in which case the
        // Disembargo is delivered to a promise capability.
        auto promise = target->whenResolved()
            .then([]() {
          // We also need to insert yieldUntilQueueEmpty() here to make sure that any pending calls
          // towards this cap have had time to find their way through the event loop.
          return kj::yieldUntilQueueEmpty();
        });

        tasks.add(promise.then([this, embargoId, target = kj::mv(target)]() mutable {
          for (;;) {
            KJ_IF_SOME(r, target->getResolved()) {
              target = r.addRef();
            } else {
              break;
            }
          }

          KJ_REQUIRE(unwrapIfSameConnection(*target) != kj::none,
                    "'Disembargo' of type 'senderLoopback' sent to an object that does not point "
                    "back to the sender.") {
            return;
          }

          if (!connection.is<Connected>()) {
            return;
          }

          RpcClient& downcasted = kj::downcast<RpcClient>(*target);

          auto message = connection.get<Connected>().connection->newOutgoingMessage(
              messageSizeHint<rpc::Disembargo>() + MESSAGE_TARGET_SIZE_HINT);
          auto builder = message->getBody().initAs<rpc::Message>().initDisembargo();

          {
            auto redirect = downcasted.writeTarget(builder.initTarget());

            // Disembargoes should only be sent to capabilities that were previously the subject of
            // a `Resolve` message.  But `writeTarget` only ever returns non-null when called on
            // a PromiseClient.  The code which sends `Resolve` and `Return` should have replaced
            // any promise with a direct node in order to solve the Tribble 4-way race condition.
            // See the documentation of Disembargo in rpc.capnp for more.
            KJ_REQUIRE(redirect == kj::none,
                      "'Disembargo' of type 'senderLoopback' sent to an object that does not "
                      "appear to have been the subject of a previous 'Resolve' message.") {
              return;
            }
          }

          builder.getContext().setReceiverLoopback(embargoId);

          message->send();
        }));

        break;
      }

      case rpc::Disembargo::Context::RECEIVER_LOOPBACK: {
        KJ_IF_SOME(embargo, embargoes.find(context.getReceiverLoopback())) {
          KJ_ASSERT_NONNULL(embargo.fulfiller)->fulfill();
          embargoes.erase(context.getReceiverLoopback(), embargo);
          checkIfBecameIdle();
        } else {
          KJ_FAIL_REQUIRE("Invalid embargo ID in 'Disembargo.context.receiverLoopback'.");
        }
        break;
      }

      case rpc::Disembargo::Context::ACCEPT: {
        kj::ArrayPtr<const byte> embargoId = context.getAccept();
        auto target = disembargo.getTarget();

        switch (target.which()) {
          case rpc::MessageTarget::IMPORTED_CAP: {
            auto& exp = KJ_REQUIRE_NONNULL(exports.find(target.getImportedCap()),
                "Message target is not a current export ID.");

            auto& vineInfo = KJ_REQUIRE_NONNULL(exp.vineInfo, "Disembargo target is not a vine.");

            kj::Maybe<VatNetworkBase::Connection&> nextConn;
            kj::OneOf<QuestionRef*, RpcClient*> nextTarget;

            KJ_SWITCH_ONEOF(vineInfo->info) {
              KJ_CASE_ONEOF(provision, VineInfo::Provision) {
                nextConn = provision->tryGetConnection();
                nextTarget = provision.get();
              }
              KJ_CASE_ONEOF(contact, VineInfo::Contact) {
                // This is a forwarded vine.
                RpcClient& client = KJ_ASSERT_NONNULL(unwrapIfSameNetwork(*exp.clientHook));
                nextConn = client.connectionState->tryGetConnection();
                nextTarget = &client;
              }
            }

            KJ_IF_SOME(c, nextConn) {
              auto nextMsg = c.newOutgoingMessage(messageSizeHint<rpc::Disembargo>() +
                  embargoId.size() / sizeof(word) + MESSAGE_TARGET_SIZE_HINT + 1);

              auto nextDisembargo = nextMsg->getBody().initAs<rpc::Message>().initDisembargo();
              nextDisembargo.getContext().setAccept(embargoId);

              KJ_SWITCH_ONEOF(nextTarget) {
                KJ_CASE_ONEOF(question, QuestionRef*) {
                  nextDisembargo.initTarget().initPromisedAnswer()
                      .setQuestionId(question->getId());
                }
                KJ_CASE_ONEOF(import, RpcClient*) {
                  KJ_ASSERT(import->writeTarget(nextDisembargo.initTarget()) == kj::none);
                }
              }

              nextMsg->send();
            } else {
              // Ugh the connection was lost. Hopefully the vine will be dropped causing the
              // disembargo to fail, at least.
            }

            break;
          }

          case rpc::MessageTarget::PROMISED_ANSWER: {
            auto promisedAnswer = target.getPromisedAnswer();
            KJ_REQUIRE(!promisedAnswer.hasTransform(),
                "Disembargo sent to Provide operation cannot include a transformation.");

            auto& answer = KJ_REQUIRE_NONNULL(answers.find(promisedAnswer.getQuestionId()),
                "Disembargo specified a provision ID (question ID) that doesn't exist.");

            auto& provision = KJ_REQUIRE_NONNULL(answer.task.tryGet<Answer::Provide>(),
                "Disembargo specified a question ID that didn't belong to a Provide operation.");

            auto& embargo =
                KJ_ASSERT_NONNULL(provision->value.tryGet<ThirdPartyExchangeValue::Provide>())
                .findOrCreateThirdPartyEmbargo(embargoId);

            embargo.fulfiller->fulfill();

            break;
          }

          default:
            KJ_FAIL_REQUIRE("Unknown message target type.", target);
        }

        break;
      }

      default:
        KJ_FAIL_REQUIRE("Unimplemented Disembargo type.", disembargo);
    }
  }

  // ---------------------------------------------------------------------------
  // Level 3

  struct ThirdPartyEmbargo {
    // For handling the `Disembargo` message for three-party handoff.

    kj::Maybe<kj::Promise<void>> promise;
    kj::Own<kj::PromiseFulfiller<void>> fulfiller;
    // Both are filled in when the embargo is created, and then each of the Accept and Disembargo
    // messages consume their respective side of the pair.
  };

  struct ThirdPartyExchangeValue: public kj::Refcounted {
    struct Provide {
      // Exchanged content for a Provide/Accept operation.

      kj::Own<ClientHook> client;
      kj::HashMap<kj::Array<byte>, ThirdPartyEmbargo> embargoes;

      explicit Provide(kj::Own<ClientHook> client): client(kj::mv(client)) {}
      ~Provide() noexcept(false) {
        for (auto& e: embargoes) {
          if (e.value.fulfiller->isWaiting()) {
            e.value.fulfiller->reject(KJ_EXCEPTION(DISCONNECTED,
                "Three-party handoff failed because a connection was lost along the introducing "
                "route before the embargo could clear."));
          }
        }
      }

      ThirdPartyEmbargo& findOrCreateThirdPartyEmbargo(kj::ArrayPtr<const byte> id) {
        return embargoes.findOrCreate(id, [&]() -> decltype(embargoes)::Entry {
          auto paf = kj::newPromiseAndFulfiller<void>();
          return {
            .key = kj::heapArray<byte>(id),
            .value = {
              .promise = kj::mv(paf.promise),
              .fulfiller = kj::mv(paf.fulfiller),
            }
          };
        });
      }
    };

    struct Call {
      // Exchanged content for three-party call forwarding.

      kj::Maybe<QuestionRef&> question;
      // The original question that was redirected.

      Call(QuestionRef& question): question(kj::addRef(question)) {}
    };

    kj::OneOf<Provide, Call> value;
    // (Eventually this will be extended with another type for ThirdPartyAnswer.)
  };

  void handleProvide(const rpc::Provide::Reader& provide) {
    if (!connection.is<Connected>()) {
      // Disconnected; ignore.
      return;
    }

    kj::Own<ClientHook> target = getMessageTarget(provide.getTarget());
    auto xchgValue = kj::rc<ThirdPartyExchangeValue>();
    xchgValue->value.init<ThirdPartyExchangeValue::Provide>(kj::mv(target));

    AnswerId answerId = provide.getQuestionId();
    auto& answer = KJ_ASSERT_NONNULL(answers.create(answerId),
                                     "questionId is already in use", answerId);

    answer.task = xchgValue.addRef();

    // NOTE: We don't need an RpcCallContext since we don't need to send a `Return`. We can just
    // leave `answer.context` null.

    // Register that we're awaiting an Accept.
    auto awaiter = connection.get<Connected>().connection
        ->awaitThirdParty(provide.getRecipient(), kj::mv(xchgValue));

    // Use a `pipeline` that'll throw exceptions if anyone actually tries to use it.
    //
    // It also holds onto `awaiter`, so that `awaiter` is dropped when `Finish` is received.
    class ProvidePipelineHook final: public PipelineHook, public kj::Refcounted {
    public:
      ProvidePipelineHook(kj::Own<void> awaiter): awaiter(kj::mv(awaiter)) {}

      kj::Own<PipelineHook> addRef() override {
        return kj::addRef(*this);
      }

      kj::Own<ClientHook> getPipelinedCap(kj::ArrayPtr<const PipelineOp> ops) override {
        return newBrokenCap(KJ_EXCEPTION(FAILED, "can't pipeline on a Provide operation"));
      }

      kj::Own<ClientHook> getPipelinedCap(kj::Array<PipelineOp>&& ops) override {
        return newBrokenCap(KJ_EXCEPTION(FAILED, "can't pipeline on a Provide operation"));
      }

    private:
      kj::Own<void> awaiter;
    };

    answer.pipeline = kj::refcounted<ProvidePipelineHook>(kj::mv(awaiter));
  }

  void handleAccept(const rpc::Accept::Reader& accept) {
    if (!connection.is<Connected>()) {
      // Disconnected; ignore.
      return;
    }

    AnswerId answerId = accept.getQuestionId();
    auto& answer = KJ_ASSERT_NONNULL(answers.create(answerId),
                                     "questionId is already in use", answerId);

    auto context = kj::refcounted<RpcCallContext>(*this, answerId);
    answer.callContext = context;

    // Hack:  Both the success and error continuations need to use the context.  We could
    //   refcount, but both will be destroyed at the same time anyway.
    RpcCallContext& contextRef = *context;

    kj::Maybe<kj::Array<byte>> embargoId;
    if (accept.hasEmbargo()) {
      embargoId = kj::heapArray<byte>(accept.getEmbargo());
    }

    kj::ForkedPromise<kj::Own<ClientHook>> promise =
        doAccept(connection.get<Connected>(), accept.getProvision(), kj::mv(embargoId)).fork();

    // Set `answer.task` to a promise that eventually sends `Return`.
    answer.task = promise.addBranch()
        .then([context = kj::mv(context)](kj::Own<ClientHook> cap) mutable {
      context->getResults(MessageSize {4, 1}).setAs<Capability>(Capability::Client(kj::mv(cap)));
      context->sendReturn();
    }, [&contextRef](kj::Exception&& exception) {
      contextRef.sendErrorReturn(kj::mv(exception));
    }).eagerlyEvaluate([&](kj::Exception&& exception) {
      // Handle exceptions that occur in sendReturn()/sendErrorReturn().
      tasks.add(kj::mv(exception));
    });

    // Set `answer.pipeline` to a single-cap pipeline.
    answer.pipeline = kj::refcounted<SingleCapPipeline>(
        newLocalPromiseClient(promise.addBranch()));
  }

  kj::Promise<kj::Own<ClientHook>> doAccept(
      Connected& connectedState, ThirdPartyCompletion::Reader thirdPartyCompletion,
      kj::Maybe<kj::Array<byte>> embargoId) {
    // `connectedState` and `thirdPartyCompletion` are temporary, can't pass them to a coroutine.
    // But we can consume them here and pass on the rest. We should also wrap this promise in the
    // canceler in case we get disconnected.
    auto completionPromise = connectedState.canceler->wrap(
        connectedState.connection->completeThirdParty(thirdPartyCompletion));
    return doAccept(kj::mv(completionPromise), kj::mv(embargoId));
  }

  kj::Promise<kj::Own<ClientHook>> doAccept(
      kj::Promise<kj::Rc<kj::Refcounted>> completionPromise,
      kj::Maybe<kj::Array<byte>> embargoId) {
    // Part of handleAccept() implementation that benefits greatly from coroutines.

    kj::Rc<ThirdPartyExchangeValue> xcghValue =
        (co_await completionPromise).downcast<ThirdPartyExchangeValue>();

    auto& provide = KJ_REQUIRE_NONNULL(
        xcghValue->value.tryGet<ThirdPartyExchangeValue::Provide>(),
        "ThirdPartyCompletion given in Accept did not match a Provide operation.");

    KJ_IF_SOME(e, embargoId) {
      auto& embargo = provide.findOrCreateThirdPartyEmbargo(e);
      auto embargoPromise = kj::mv(KJ_REQUIRE_NONNULL(embargo.promise,
          "Duplicate embargo ID in Accept message."));
      embargo.promise = kj::none;
      co_await embargoPromise;

      // This embargo is complete, so we can erase it.
      provide.embargoes.erase(e);
    }

    co_return provide.client->addRef();
  }

  void handleThirdPartyAnswer(const rpc::ThirdPartyAnswer::Reader& tpa) {
    if (!connection.is<Connected>()) {
      // Disconnected; ignore.
      return;
    }

    setNotIdle();

    QuestionId questionId = tpa.getAnswerId();
    auto& question = questions.injectReverseAllocated(questionId);
    question.isAwaitingReturn = true;

    auto questionRef = kj::refcounted<QuestionRef>(*this, questionId);
    question.selfRef = *questionRef;

    auto& connectedState = connection.get<Connected>();
    auto completionPromise = connectedState.canceler->wrap(
        connectedState.connection->completeThirdParty(tpa.getCompletion()));

    // We have to allocate a response promise now, before there's any opportunity for a return
    // message to arrive.
    auto responsePromise = toPromise(kj::addRef(*questionRef));

    tasks.add(doThirdPartyAnswer(
        kj::mv(completionPromise), kj::mv(questionRef), kj::mv(responsePromise)));
  }

  kj::Promise<void> doThirdPartyAnswer(
        kj::Promise<kj::Rc<kj::Refcounted>> completionPromise,
        kj::Own<QuestionRef> questionRef,
        kj::Promise<kj::Own<RpcResponse>> responsePromise) {
    kj::Rc<ThirdPartyExchangeValue> xcghValue =
        (co_await completionPromise).downcast<ThirdPartyExchangeValue>();

    auto& call = KJ_REQUIRE_NONNULL(
        xcghValue->value.tryGet<ThirdPartyExchangeValue::Call>(),
        "ThirdPartyCompletion given in ThirdPartyAnswer did not match a redirected Return.");

    if (questionRef->isDone()) {
      // `responsePromise` is actually already resolved. Let's just extract it and deliver.
      auto response = co_await responsePromise;
      KJ_IF_SOME(origQuestion, call.question) {
        origQuestion.fulfill(kj::mv(response));
      } else {
        // Original call was canceled, so... do nothing.
      }
    } else {
      // No response yet, so we should redirect the pipeline.
      auto newPipeline = kj::refcounted<RpcPipeline>(
          *this, kj::addRef(*questionRef), /*willResolve =*/ true);
      KJ_IF_SOME(origQuestion, call.question) {
        if (origQuestion.hasReceivedPipeliendCall()) {
          // Some pipelined calls were sent before the redirect. We will need to embargo new calls
          // that go over the shorter path.

          // Send embargo message.
          KJ_IF_SOME(c, connection.tryGet<Connected>()) {
            auto msg = c.connection->newOutgoingMessage(
                messageSizeHint<rpc::ThirdPartyAnswerEmbargo>());
            auto embargo = msg->getBody().initAs<rpc::Message>().initThirdPartyAnswerEmbargo();
            embargo.setQuestionId(questionRef->getId());
            msg->send();
          }

          // Note that we should NOT implement the embargo by wrapping `newPipeline` in
          // `newLocalPromisePipeline()` that waits for the disembargo. This would cause new
          // pipelined calls made after this point to be blocked *locally*, which wastes up to
          // a whole network round trip. We should let the calls be sent to the remote end and
          // embargoed there. What we do have to make sure is that the pipeline is not actually
          // resolved to the final response until the disembargo is received on our end.
          questionRef->expectThirdPartyAnswerDisembargo();
        }

        origQuestion.redirect(kj::mv(responsePromise), kj::addRef(*newPipeline));
      } else {
        // Original call was canceled, so... do nothing.
      }
    }
  }

  void handleThirdPartyAnswerEmbargo(const rpc::ThirdPartyAnswerEmbargo::Reader& embargo) {
    AnswerId answerId = embargo.getQuestionId();
    auto& answer = KJ_REQUIRE_NONNULL(answers.find(answerId),
        "No such question for ThirdPartyAnswerEmbargo.", answerId);

    auto sendReply = [this, answerId]() {
      KJ_IF_SOME(c, connection.tryGet<Connected>()) {
        auto msg = c.connection->newOutgoingMessage(
            messageSizeHint<rpc::ThirdPartyAnswerDisembargo>());
        msg->getBody().initAs<rpc::Message>().initThirdPartyAnswerDisembargo()
            .setAnswerId(answerId);
        msg->send();
      }
    };

    KJ_IF_SOME(pipeline, answer.pipeline) {
      auto disembargoPromise = kj::mv(KJ_ASSERT_NONNULL(answer.thirdPartyFinishPromise,
          "Question was not introduced by ThirdPartyAnswer, can't use ThirdPartyAnswerEmbargo."));
      answer.thirdPartyFinishPromise = kj::none;

      auto promise = disembargoPromise.then([sendReply, pipeline = kj::mv(pipeline)]() mutable {
        sendReply();
        return kj::mv(pipeline);
      });

      pipeline = newLocalPromisePipeline(kj::mv(promise));
    } else  {
      // No pipeline present on answer, suggesting that the call was made using
      // `noPromisePipelining`. It's weird that the peer is trying to embargo anything here, as
      // all pipelined calls will just error out. That said, we can just send the reply message
      // here and let those errors happen naturally.

      sendReply();
    }
  }

  void handleThirdPartyAnswerDisembargo(const rpc::ThirdPartyAnswerDisembargo::Reader& disembargo) {
    QuestionId questionId = disembargo.getAnswerId();

    KJ_IF_SOME(question, questions.find(questionId)) {
      KJ_IF_SOME(questionRef, question.selfRef) {
        // Be careful that this object doesn't destroy itself in the process of fulfilling the
        // last promises.
        auto q = kj::addRef(questionRef);
        q->gotThirdPartyAnswerDisembargo();
      }
    }

    // If either of the above `KJ_IF_SOME` failed, then we must have sent the `Finish` for this
    // qeustion already. In this case there couldn't be anything waiting on a disembargo, or it
    // would have held a reference on the QuestionRef preventing the finish. We can ignore the
    // ThirdPartyAnswerDisembargo message in this case.
  }
};

// =======================================================================================

class RpcSystemBase::Impl final: private BootstrapFactoryBase, private kj::TaskSet::ErrorHandler {
public:
  Impl(VatNetworkBase& network, kj::Maybe<Capability::Client> bootstrapInterface)
      : network(network), bootstrapInterface(kj::mv(bootstrapInterface)),
        bootstrapFactory(*this), tasks(*this) {
    acceptLoopPromise = acceptLoop().eagerlyEvaluate([](kj::Exception&& e) { KJ_LOG(ERROR, e); });
  }
  Impl(VatNetworkBase& network, BootstrapFactoryBase& bootstrapFactory)
      : network(network), bootstrapFactory(bootstrapFactory), tasks(*this) {
    acceptLoopPromise = acceptLoop().eagerlyEvaluate([](kj::Exception&& e) { KJ_LOG(ERROR, e); });
  }

  void disconnectAll() {
    unwindDetector.catchExceptionsIfUnwinding([&]() {
      // We need to pull all the connections out of the map first, before calling disconnect on
      // them, since they will each remove themselves from the map which invalidates iterators.
      if (connections.size() > 0) {
        kj::Vector<kj::Own<RpcConnectionState>> deleteMe(connections.size());
        kj::Exception shutdownException = KJ_EXCEPTION(DISCONNECTED, "RpcSystem was destroyed.");
        for (auto& entry: connections) {
          deleteMe.add(kj::mv(entry.value));
        }
        for (auto& entry: deleteMe) {
          entry->disconnect(kj::cp(shutdownException));
        }
        KJ_ASSERT(connections.size() == 0);
      }
    });
  }

  Capability::Client bootstrap(AnyStruct::Reader vatId) {
    KJ_IF_SOME(connection, network.baseConnect(vatId)) {
      auto& state = getConnectionState(kj::mv(connection));
      return Capability::Client(state.bootstrap());
    } else {
      // Turns out `vatId` refers to ourselves, so we can also pass it as the client ID for
      // baseCreateFor().
      return bootstrapFactory.baseCreateFor(vatId);
    }
  }

  void setFlowLimit(size_t words) {
    flowLimit = words;

    for (auto& conn: connections) {
      conn.value->setFlowLimit(words);
    }
  }

  void setTraceEncoder(kj::Function<kj::String(const kj::Exception&)> func) {
    traceEncoder = kj::mv(func);
  }

  kj::Promise<void> run() { return kj::mv(acceptLoopPromise); }

  void dropConnection(VatNetworkBase::Connection& connection, kj::Promise<void> shutdownTask) {
    connections.erase(&connection);
    tasks.add(kj::mv(shutdownTask));
  }

  RpcConnectionState& getConnectionState(kj::Own<VatNetworkBase::Connection>&& connection) {
    return *connections.findOrCreate(connection, [&]() -> ConnectionMap::Entry {
      VatNetworkBase::Connection* connectionPtr = connection;
      auto newState = kj::refcounted<RpcConnectionState>(
          *this, bootstrapFactory, kj::mv(connection), flowLimit, traceEncoder, *brand);
      return {connectionPtr, kj::mv(newState)};
    });
  }

private:
  VatNetworkBase& network;
  kj::Maybe<Capability::Client> bootstrapInterface;
  BootstrapFactoryBase& bootstrapFactory;
  size_t flowLimit = kj::maxValue;
  kj::Maybe<kj::Function<kj::String(const kj::Exception&)>> traceEncoder;
  kj::Promise<void> acceptLoopPromise = nullptr;
  kj::Own<RpcSystemBrand> brand = kj::refcounted<RpcSystemBrand>();
  kj::TaskSet tasks;

  typedef kj::HashMap<VatNetworkBase::Connection*, kj::Own<RpcConnectionState>>
      ConnectionMap;
  ConnectionMap connections;

  kj::UnwindDetector unwindDetector;

  kj::Promise<void> acceptLoop() {
    for (;;) {
      getConnectionState(co_await network.baseAccept());
    }
  }

  Capability::Client baseCreateFor(AnyStruct::Reader clientId) override {
    // Implements BootstrapFactory::baseCreateFor() in terms of `bootstrapInterface`,
    // for use when we were given one of those instead of an actual `bootstrapFactory`.

    KJ_IF_SOME(cap, bootstrapInterface) {
      return cap;
    } else {
      return KJ_EXCEPTION(FAILED, "This vat does not expose any public/bootstrap interfaces.");
    }
  }

  void taskFailed(kj::Exception&& exception) override {
    KJ_LOG(ERROR, exception);
  }
};

RpcSystemBase::RpcSystemBase(VatNetworkBase& network,
                             kj::Maybe<Capability::Client> bootstrapInterface)
    : impl(kj::heap<Impl>(network, kj::mv(bootstrapInterface))) {}
RpcSystemBase::RpcSystemBase(VatNetworkBase& network,
                             BootstrapFactoryBase& bootstrapFactory)
    : impl(kj::heap<Impl>(network, bootstrapFactory)) {}
RpcSystemBase::RpcSystemBase(RpcSystemBase&& other) noexcept = default;
RpcSystemBase::~RpcSystemBase() noexcept(false) {
  if (impl.get() != nullptr) {  // could have been moved away
    impl->disconnectAll();
  }
}

Capability::Client RpcSystemBase::baseBootstrap(AnyStruct::Reader vatId) {
  return impl->bootstrap(vatId);
}

void RpcSystemBase::baseSetFlowLimit(size_t words) {
  return impl->setFlowLimit(words);
}

void RpcSystemBase::setTraceEncoder(kj::Function<kj::String(const kj::Exception&)> func) {
  impl->setTraceEncoder(kj::mv(func));
}

kj::Promise<void> RpcSystemBase::run() {
  return impl->run();
}

void RpcSystemBase::dropConnection(RpcSystemBase::Impl& impl,
    VatNetworkBase::Connection& connection, kj::Promise<void> shutdownTask) {
  impl.dropConnection(connection, kj::mv(shutdownTask));
}

RpcSystemBase::RpcConnectionState& RpcSystemBase::getConnectionState(Impl& impl,
    kj::Own<VatNetworkBase::Connection> connection) {
  return impl.getConnectionState(kj::mv(connection));
}

}  // namespace _ (private)

// =======================================================================================

namespace {

class WindowFlowController final: public RpcFlowController, private kj::TaskSet::ErrorHandler {
public:
  WindowFlowController(RpcFlowController::WindowGetter& windowGetter)
      : windowGetter(windowGetter), tasks(*this) {
    state.init<Running>();
  }

  kj::Promise<void> send(kj::Own<OutgoingRpcMessage> message, kj::Promise<void> ack) override {
    auto size = message->sizeInWords() * sizeof(capnp::word);
    maxMessageSize = kj::max(size, maxMessageSize);

    // We are REQUIRED to send the message NOW to maintain correct ordering.
    message->send();

    inFlight += size;
    tasks.add(ack.then([this, size]() {
      inFlight -= size;
      KJ_SWITCH_ONEOF(state) {
        KJ_CASE_ONEOF(blockedSends, Running) {
          if (isReady()) {
            // Release all fulfillers.
            for (auto& fulfiller: blockedSends) {
              fulfiller->fulfill();
            }
            blockedSends.clear();

          }

          KJ_IF_SOME(f, emptyFulfiller) {
            if (inFlight == 0) {
              f->fulfill(tasks.onEmpty());
            }
          }
        }
        KJ_CASE_ONEOF(exception, kj::Exception) {
          // A previous call failed, but this one -- which was already in-flight at the time --
          // ended up succeeding. That may indicate that the server side is not properly
          // handling streaming error propagation. Nothing much we can do about it here though.
        }
      }
    }));

    KJ_SWITCH_ONEOF(state) {
      KJ_CASE_ONEOF(blockedSends, Running) {
        if (isReady()) {
          return kj::READY_NOW;
        } else {
          auto paf = kj::newPromiseAndFulfiller<void>();
          blockedSends.add(kj::mv(paf.fulfiller));
          return kj::mv(paf.promise);
        }
      }
      KJ_CASE_ONEOF(exception, kj::Exception) {
        return kj::cp(exception);
      }
    }
    KJ_UNREACHABLE;
  }

  kj::Promise<void> waitAllAcked() override {
    KJ_IF_SOME(q, state.tryGet<Running>()) {
      if (!q.empty()) {
        auto paf = kj::newPromiseAndFulfiller<kj::Promise<void>>();
        emptyFulfiller = kj::mv(paf.fulfiller);
        return kj::mv(paf.promise);
      }
    }
    return tasks.onEmpty();
  }

private:
  RpcFlowController::WindowGetter& windowGetter;
  size_t inFlight = 0;
  size_t maxMessageSize = 0;

  typedef kj::Vector<kj::Own<kj::PromiseFulfiller<void>>> Running;
  kj::OneOf<Running, kj::Exception> state;

  kj::Maybe<kj::Own<kj::PromiseFulfiller<kj::Promise<void>>>> emptyFulfiller;

  kj::TaskSet tasks;

  void taskFailed(kj::Exception&& exception) override {
    KJ_SWITCH_ONEOF(state) {
      KJ_CASE_ONEOF(blockedSends, Running) {
        // Fail out all pending sends.
        for (auto& fulfiller: blockedSends) {
          fulfiller->reject(kj::cp(exception));
        }
        // Fail out all future sends.
        state = kj::mv(exception);
      }
      KJ_CASE_ONEOF(exception, kj::Exception) {
        // ignore redundant exception
      }
    }
  }

  bool isReady() {
    // We extend the window by maxMessageSize to avoid a pathological situation when a message
    // is larger than the window size. Otherwise, after sending that message, we would end up
    // not sending any others until the ack was received, wasting a round trip's worth of
    // bandwidth.
    return inFlight <= maxMessageSize  // avoid getWindow() call if unnecessary
        || inFlight < windowGetter.getWindow() + maxMessageSize;
  }
};

class FixedWindowFlowController final
    : public RpcFlowController, public RpcFlowController::WindowGetter {
public:
  FixedWindowFlowController(size_t windowSize): windowSize(windowSize), inner(*this) {}

  kj::Promise<void> send(kj::Own<OutgoingRpcMessage> message, kj::Promise<void> ack) override {
    return inner.send(kj::mv(message), kj::mv(ack));
  }

  kj::Promise<void> waitAllAcked() override {
    return inner.waitAllAcked();
  }

  size_t getWindow() override { return windowSize; }

private:
  size_t windowSize;
  WindowFlowController inner;
};

}  // namespace

kj::Own<RpcFlowController> RpcFlowController::newFixedWindowController(size_t windowSize) {
  return kj::heap<FixedWindowFlowController>(windowSize);
}
kj::Own<RpcFlowController> RpcFlowController::newVariableWindowController(WindowGetter& getter) {
  return kj::heap<WindowFlowController>(getter);
}

bool IncomingRpcMessage::isShortLivedRpcMessage(AnyPointer::Reader body) {
  switch (body.getAs<rpc::Message>().which()) {
    case rpc::Message::CALL:
    case rpc::Message::RETURN:
      return false;
    default:
      return true;
  }
}

kj::Function<bool(MessageReader&)> IncomingRpcMessage::getShortLivedCallback() {
  return [](MessageReader& reader) {
    return IncomingRpcMessage::isShortLivedRpcMessage(reader.getRoot<AnyPointer>());
  };
}

}  // namespace capnp
