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
#include <functional>  // std::greater
#include <unordered_map>
#include <map>
#include <queue>
#include <capnp/rpc.capnp.h>
#include <kj/io.h>
#include <kj/map.h>

namespace capnp {
namespace _ {  // private

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
  KJ_IF_MAYBE(s, sizeHint) {
    return copySizeHint(*s) + additional;
  } else {
    return 0;
  }
}

kj::Maybe<kj::Array<PipelineOp>> toPipelineOps(List<rpc::PromisedAnswer::Op>::Reader ops) {
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
        KJ_FAIL_REQUIRE("Unsupported pipeline op.", (uint)opReader.which()) {
          return nullptr;
        }
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
  return result;
}

void fromException(const kj::Exception& exception, rpc::Exception::Builder builder,
                   kj::Maybe<kj::Function<kj::String(const kj::Exception&)>&> traceEncoder) {
  kj::StringPtr description = exception.getDescription();

  // Include context, if any.
  kj::Vector<kj::String> contextLines;
  for (auto context = exception.getContext();;) {
    KJ_IF_MAYBE(c, context) {
      contextLines.add(kj::str("context: ", c->file, ": ", c->line, ": ", c->description));
      context = c->next;
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

  KJ_IF_MAYBE(t, traceEncoder) {
    builder.setTrace((*t)(exception));
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

// =======================================================================================

template <typename Id>
static constexpr Id highBit() {
  return 1u << (sizeof(Id) * 8 - 1);
}

template <typename Id, typename T>
class ExportTable {
  // Table mapping integers to T, where the integers are chosen locally.

public:
  bool isHigh(Id& id) {
    return (id & highBit<Id>()) != 0;
  }

  kj::Maybe<T&> find(Id id) {
    if (isHigh(id)) {
      return highSlots.find(id);
    } else if (id < slots.size() && slots[id] != nullptr) {
      return slots[id];
    } else {
      return nullptr;
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
      KJ_ASSERT(!isHigh(id), "2^31 concurrent questions?!!?!");
      return slots.add();
    } else {
      id = freeIds.top();
      freeIds.pop();
      return slots[id];
    }
  }

  T& nextHigh(Id& id) {
    // Choose an ID with the top bit set in round-robin fashion, but don't choose an ID that
    // is still in use.

    KJ_ASSERT(highSlots.size() < Id(kj::maxValue) / 2);  // avoid infinite loop below.

    bool created = false;
    T* slot;
    while (!created) {
      id = highCounter++ | highBit<Id>();
      slot = &highSlots.findOrCreate(id, [&]() {
        created = true;
        return typename kj::HashMap<Id, T>::Entry { id, T() };
      });
    }

    return *slot;
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
  Id highCounter = 0;
};

template <typename Id, typename T>
class ImportTable {
  // Table mapping integers to T, where the integers are chosen remotely.

public:
  T& operator[](Id id) {
    if (id < kj::size(low)) {
      return low[id];
    } else {
      return high[id];
    }
  }

  kj::Maybe<T&> find(Id id) {
    if (id < kj::size(low)) {
      return low[id];
    } else {
      auto iter = high.find(id);
      if (iter == high.end()) {
        return nullptr;
      } else {
        return iter->second;
      }
    }
  }

  T erase(Id id) {
    // Remove an entry from the table and return it.  We return it so that the caller can be
    // careful to release it (possibly invoking arbitrary destructors) at a time that makes sense.
    if (id < kj::size(low)) {
      T toRelease = kj::mv(low[id]);
      low[id] = T();
      return toRelease;
    } else {
      T toRelease = kj::mv(high[id]);
      high.erase(id);
      return toRelease;
    }
  }

  template <typename Func>
  void forEach(Func&& func) {
    for (Id i: kj::indices(low)) {
      func(i, low[i]);
    }
    for (auto& entry: high) {
      func(entry.first, entry.second);
    }
  }

private:
  T low[16];
  std::unordered_map<Id, T> high;
};

// =======================================================================================

class RpcConnectionState final: public kj::TaskSet::ErrorHandler, public kj::Refcounted {
public:
  struct DisconnectInfo {
    kj::Promise<void> shutdownPromise;
    // Task which is working on sending an abort message and cleanly ending the connection.
  };

  RpcConnectionState(BootstrapFactoryBase& bootstrapFactory,
                     kj::Maybe<SturdyRefRestorerBase&> restorer,
                     kj::Own<VatNetworkBase::Connection>&& connectionParam,
                     kj::Own<kj::PromiseFulfiller<DisconnectInfo>>&& disconnectFulfiller,
                     size_t flowLimit,
                     kj::Maybe<kj::Function<kj::String(const kj::Exception&)>&> traceEncoder)
      : bootstrapFactory(bootstrapFactory),
        restorer(restorer), disconnectFulfiller(kj::mv(disconnectFulfiller)), flowLimit(flowLimit),
        traceEncoder(traceEncoder), tasks(*this) {
    connection.init<Connected>(kj::mv(connectionParam));
    tasks.add(messageLoop());
  }

  kj::Own<ClientHook> restore(AnyPointer::Reader objectId) {
    if (connection.is<Disconnected>()) {
      return newBrokenCap(kj::cp(connection.get<Disconnected>()));
    }

    QuestionId questionId;
    auto& question = questions.next(questionId);

    question.isAwaitingReturn = true;

    auto paf = kj::newPromiseAndFulfiller<kj::Promise<kj::Own<RpcResponse>>>();

    auto questionRef = kj::refcounted<QuestionRef>(*this, questionId, kj::mv(paf.fulfiller));
    question.selfRef = *questionRef;

    paf.promise = paf.promise.attach(kj::addRef(*questionRef));

    {
      auto message = connection.get<Connected>()->newOutgoingMessage(
          objectId.targetSize().wordCount + messageSizeHint<rpc::Bootstrap>());

      auto builder = message->getBody().initAs<rpc::Message>().initBootstrap();
      builder.setQuestionId(questionId);
      builder.getDeprecatedObjectId().set(objectId);

      message->send();
    }

    auto pipeline = kj::refcounted<RpcPipeline>(*this, kj::mv(questionRef), kj::mv(paf.promise));

    return pipeline->getPipelinedCap(kj::Array<const PipelineOp>(nullptr));
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
    KJ_DEFER(traceEncoder = nullptr);

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
    auto dyingConnection = kj::mv(connection.get<Connected>());
    connection.init<Disconnected>(kj::cp(networkException));

    KJ_IF_MAYBE(newException, kj::runCatchingExceptions([&]() {
      // Carefully pull all the objects out of the tables prior to releasing them because their
      // destructors could come back and mess with the tables.
      kj::Vector<kj::Own<PipelineHook>> pipelinesToRelease;
      kj::Vector<kj::Own<ClientHook>> clientsToRelease;
      kj::Vector<decltype(Answer::task)> tasksToRelease;
      kj::Vector<kj::Promise<void>> resolveOpsToRelease;
      KJ_DEFER(tasks.clear());

      // All current questions complete with exceptions.
      questions.forEach([&](QuestionId id, Question& question) {
        KJ_IF_MAYBE(questionRef, question.selfRef) {
          // QuestionRef still present.
          questionRef->reject(kj::cp(networkException));

          // We need to fully disconnect each QuestionRef otherwise it holds a reference back to
          // the connection state. Meanwhile `tasks` may hold streaming calls that end up holding
          // these QuestionRefs. Technically this is a cyclic reference, but as long as the cycle
          // is broken on disconnect (which happens when the RpcSystem itself is destroyed), then
          // we're OK.
          questionRef->disconnect();
        }
      });
      // Since we've disconnected the QuestionRefs, they won't clean up the questions table for
      // us, so do that here.
      questions.release();

      answers.forEach([&](AnswerId id, Answer& answer) {
        KJ_IF_MAYBE(p, answer.pipeline) {
          pipelinesToRelease.add(kj::mv(*p));
        }

        tasksToRelease.add(kj::mv(answer.task));

        KJ_IF_MAYBE(context, answer.callContext) {
          context->finish();
        }
      });

      exports.forEach([&](ExportId id, Export& exp) {
        clientsToRelease.add(kj::mv(exp.clientHook));
        KJ_IF_MAYBE(op, exp.resolveOp) {
          resolveOpsToRelease.add(kj::mv(*op));
        }
        exp = Export();
      });

      imports.forEach([&](ImportId id, Import& import) {
        KJ_IF_MAYBE(f, import.promiseFulfiller) {
          f->get()->reject(kj::cp(networkException));
        }
      });

      embargoes.forEach([&](EmbargoId id, Embargo& embargo) {
        KJ_IF_MAYBE(f, embargo.fulfiller) {
          f->get()->reject(kj::cp(networkException));
        }
      });
    })) {
      // Some destructor must have thrown an exception.  There is no appropriate place to report
      // these errors.
      KJ_LOG(ERROR, "Uncaught exception when destroying capabilities dropped by disconnect.",
             *newException);
    }

    // Send an abort message, but ignore failure.
    kj::runCatchingExceptions([&]() {
      auto message = dyingConnection->newOutgoingMessage(
          messageSizeHint<void>() + exceptionSizeHint(exception));
      fromException(exception, message->getBody().getAs<rpc::Message>().initAbort());
      message->send();
    });

    // Indicate disconnect.
    auto shutdownPromise = dyingConnection->shutdown()
        .attach(kj::mv(dyingConnection))
        .then([]() -> kj::Promise<void> { return kj::READY_NOW; },
              [self = kj::addRef(*this), origException = kj::mv(exception)](
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
    disconnectFulfiller->fulfill(DisconnectInfo { kj::mv(shutdownPromise) });
    canceler.cancel(networkException);
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
      return !isAwaitingReturn && selfRef == nullptr;
    }
    inline bool operator!=(decltype(nullptr)) const { return !operator==(nullptr); }
  };

  struct Answer {
    Answer() = default;
    Answer(const Answer&) = delete;
    Answer(Answer&&) = default;
    Answer& operator=(Answer&&) = default;
    // If we don't explicitly write all this, we get some stupid error deep in STL.

    bool active = false;
    // True from the point when the Call message is received to the point when both the `Finish`
    // message has been received and the `Return` has been sent.

    kj::Maybe<kj::Own<PipelineHook>> pipeline;
    // Send pipelined calls here.  Becomes null as soon as a `Finish` is received.

    using Running = kj::Promise<void>;
    struct Finished {};
    using Redirected = kj::Promise<kj::Own<RpcResponse>>;

    kj::OneOf<Running, Finished, Redirected> task;
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
  };

  struct Export {
    uint refcount = 0;
    // When this reaches 0, drop `clientHook` and free this export.

    kj::Own<ClientHook> clientHook;

    kj::Maybe<kj::Promise<void>> resolveOp = nullptr;
    // If this export is a promise (not a settled capability), the `resolveOp` represents the
    // ongoing operation to wait for that promise to resolve and then send a `Resolve` message.

    inline bool operator==(decltype(nullptr)) const { return refcount == 0; }
    inline bool operator!=(decltype(nullptr)) const { return refcount != 0; }
  };

  struct Import {
    Import() = default;
    Import(const Import&) = delete;
    Import(Import&&) = default;
    Import& operator=(Import&&) = default;
    // If we don't explicitly write all this, we get some stupid error deep in STL.

    kj::Maybe<ImportClient&> importClient;
    // Becomes null when the import is destroyed.

    kj::Maybe<RpcClient&> appClient;
    // Either a copy of importClient, or, in the case of promises, the wrapping PromiseClient.
    // Becomes null when it is discarded *or* when the import is destroyed (e.g. the promise is
    // resolved and the import is no longer needed).

    kj::Maybe<kj::Own<kj::PromiseFulfiller<kj::Own<ClientHook>>>> promiseFulfiller;
    // If non-null, the import is a promise.
  };

  typedef uint32_t EmbargoId;

  struct Embargo {
    // For handling the `Disembargo` message when looping back to self.

    kj::Maybe<kj::Own<kj::PromiseFulfiller<void>>> fulfiller;
    // Fulfill this when the Disembargo arrives.

    inline bool operator==(decltype(nullptr)) const { return fulfiller == nullptr; }
    inline bool operator!=(decltype(nullptr)) const { return fulfiller != nullptr; }
  };

  // =======================================================================================
  // OK, now we can define RpcConnectionState's member data.

  BootstrapFactoryBase& bootstrapFactory;
  kj::Maybe<SturdyRefRestorerBase&> restorer;

  typedef kj::Own<VatNetworkBase::Connection> Connected;
  typedef kj::Exception Disconnected;
  kj::OneOf<Connected, Disconnected> connection;
  // Once the connection has failed, we drop it and replace it with an exception, which will be
  // thrown from all further calls.

  kj::Canceler canceler;
  // Will be canceled if and when `connection` is changed from `Connected` to `Disconnected`.
  // TODO(cleanup): `Connected` should be a struct that contains the connection and the Canceler,
  //   but that's more refactoring than I want to do right now.

  kj::Own<kj::PromiseFulfiller<DisconnectInfo>> disconnectFulfiller;

  ExportTable<ExportId, Export> exports;
  ExportTable<QuestionId, Question> questions;
  ImportTable<AnswerId, Answer> answers;
  ImportTable<ImportId, Import> imports;
  // The Four Tables!
  // The order of the tables is important for correct destruction.

  std::unordered_map<ClientHook*, ExportId> exportsByCap;
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

  kj::TaskSet tasks;

  bool gotReturnForHighQuestionId = false;
  // Becomes true if we ever get a `Return` message for a high question ID (with top bit set),
  // which we use in cases where we've hinted to the peer that we don't want a `Return`. If the
  // peer sends us one anyway then it seemingly doesn't not implement our hints. We need to stop
  // using the hints in this case before the high question ID space wraps around since otherwise
  // we might reuse an ID that the peer thinks is still in use.

  bool sentCapabilitiesInPipelineOnlyCall = false;
  // Becomes true if `sendPipelineOnly()` is ever called with parameters that include capabilities.

  bool receiveIncomingMessageError = false;
  // Becomes true when receiveIncomingMessage resulted in exception.

  // =====================================================================================
  // ClientHook implementations

  class RpcClient: public ClientHook, public kj::Refcounted {
  public:
    RpcClient(RpcConnectionState& connectionState)
        : connectionState(kj::addRef(connectionState)) {}

    ~RpcClient() noexcept(false) {
      KJ_IF_MAYBE(f, this->flowController) {
        // Destroying the client should not cancel outstanding streaming calls.
        connectionState->tasks.add(f->get()->waitAllAcked().attach(kj::mv(*f)));
      }
    }

    virtual kj::Maybe<ExportId> writeDescriptor(rpc::CapDescriptor::Builder descriptor,
                                                kj::Vector<int>& fds) = 0;
    // Writes a CapDescriptor referencing this client.  The CapDescriptor must be sent as part of
    // the very next message sent on the connection, as it may become invalid if other things
    // happen.
    //
    // If writing the descriptor adds a new export to the export table, or increments the refcount
    // on an existing one, then the ID is returned and the caller is responsible for removing it
    // later.

    virtual kj::Maybe<kj::Own<ClientHook>> writeTarget(
        rpc::MessageTarget::Builder target) = 0;
    // Writes the appropriate call target for calls to this capability and returns null.
    //
    // - OR -
    //
    // If calls have been redirected to some other local ClientHook, returns that hook instead.
    // This can happen if the capability represents a promise that has been resolved.

    virtual kj::Own<ClientHook> getInnermostClient() = 0;
    // If this client just wraps some other client -- even if it is only *temporarily* wrapping
    // that other client -- return a reference to the other client, transitively.  Otherwise,
    // return a new reference to *this.

    virtual void adoptFlowController(kj::Own<RpcFlowController> flowController) {
      // Called when a PromiseClient resolves to another RpcClient. If streaming calls were
      // outstanding on the old client, we'd like to keep using the same FlowController on the new
      // client, so as to keep the flow steady.

      if (this->flowController == nullptr) {
        // We don't have any existing flowController so we can adopt this one, yay!
        this->flowController = kj::mv(flowController);
      } else {
        // Apparently, there is an existing flowController. This is an unusual scenario: Apparently
        // we had two stream capabilities, we were streaming to both of them, and they later
        // resolved to the same capability. This probably never happens because streaming use cases
        // normally call for there to be only one client. But, it's certainly possible, and we need
        // to handle it. We'll do the conservative thing and just make sure that all the calls
        // finish. This may mean we'll over-buffer temporarily; oh well.
        connectionState->tasks.add(flowController->waitAllAcked().attach(kj::mv(flowController)));
      }
    }

    // implements ClientHook -----------------------------------------

    Request<AnyPointer, AnyPointer> newCall(
        uint64_t interfaceId, uint16_t methodId, kj::Maybe<MessageSize> sizeHint,
        CallHints hints) override {
      return newCallNoIntercept(interfaceId, methodId, sizeHint, hints);
    }

    Request<AnyPointer, AnyPointer> newCallNoIntercept(
        uint64_t interfaceId, uint16_t methodId, kj::Maybe<MessageSize> sizeHint,
        CallHints hints) {
      if (!connectionState->connection.is<Connected>()) {
        return newBrokenRequest(kj::cp(connectionState->connection.get<Disconnected>()), sizeHint);
      }

      auto request = kj::heap<RpcRequest>(
          *connectionState, *connectionState->connection.get<Connected>(),
          sizeHint, kj::addRef(*this));
      auto callBuilder = request->getCall();

      callBuilder.setInterfaceId(interfaceId);
      callBuilder.setMethodId(methodId);
      callBuilder.setNoPromisePipelining(hints.noPromisePipelining);
      callBuilder.setOnlyPromisePipeline(hints.onlyPromisePipeline);

      auto root = request->getRoot();
      return Request<AnyPointer, AnyPointer>(root, kj::mv(request));
    }

    VoidPromiseAndPipeline call(uint64_t interfaceId, uint16_t methodId,
                                kj::Own<CallContextHook>&& context, CallHints hints) override {
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
    const void* getBrand() override {
      return connectionState.get();
    }

    kj::Own<RpcConnectionState> connectionState;

    kj::Maybe<kj::Own<RpcFlowController>> flowController;
    // Becomes non-null the first time a streaming call is made on this capability.
  };

  class ImportClient final: public RpcClient {
    // A ClientHook that wraps an entry in the import table.

  public:
    ImportClient(RpcConnectionState& connectionState, ImportId importId,
                 kj::Maybe<kj::AutoCloseFd> fd)
        : RpcClient(connectionState), importId(importId), fd(kj::mv(fd)) {}

    ~ImportClient() noexcept(false) {
      unwindDetector.catchExceptionsIfUnwinding([&]() {
        // Remove self from the import table, if the table is still pointing at us.
        KJ_IF_MAYBE(import, connectionState->imports.find(importId)) {
          KJ_IF_MAYBE(i, import->importClient) {
            if (i == this) {
              connectionState->imports.erase(importId);
            }
          }
        }

        // Send a message releasing our remote references.
        if (remoteRefcount > 0 && connectionState->connection.is<Connected>()) {
          auto message = connectionState->connection.get<Connected>()->newOutgoingMessage(
              messageSizeHint<rpc::Release>());
          rpc::Release::Builder builder = message->getBody().initAs<rpc::Message>().initRelease();
          builder.setId(importId);
          builder.setReferenceCount(remoteRefcount);
          message->send();
        }
      });
    }

    void setFdIfMissing(kj::Maybe<kj::AutoCloseFd> newFd) {
      if (fd == nullptr) {
        fd = kj::mv(newFd);
      }
    }

    void addRemoteRef() {
      // Add a new RemoteRef and return a new ref to this client representing it.
      ++remoteRefcount;
    }

    kj::Maybe<ExportId> writeDescriptor(rpc::CapDescriptor::Builder descriptor,
                                        kj::Vector<int>& fds) override {
      descriptor.setReceiverHosted(importId);
      return nullptr;
    }

    kj::Maybe<kj::Own<ClientHook>> writeTarget(
        rpc::MessageTarget::Builder target) override {
      target.setImportedCap(importId);
      return nullptr;
    }

    kj::Own<ClientHook> getInnermostClient() override {
      return kj::addRef(*this);
    }

    // implements ClientHook -----------------------------------------

    kj::Maybe<ClientHook&> getResolved() override {
      return nullptr;
    }

    kj::Maybe<kj::Promise<kj::Own<ClientHook>>> whenMoreResolved() override {
      return nullptr;
    }

    kj::Maybe<int> getFd() override {
      return fd.map([](auto& f) { return f.get(); });
    }

  private:
    ImportId importId;
    kj::Maybe<kj::AutoCloseFd> fd;

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

   kj::Maybe<ExportId> writeDescriptor(rpc::CapDescriptor::Builder descriptor,
                                       kj::Vector<int>& fds) override {
      auto promisedAnswer = descriptor.initReceiverAnswer();
      promisedAnswer.setQuestionId(questionRef->getId());
      promisedAnswer.adoptTransform(fromPipelineOps(
          Orphanage::getForMessageContaining(descriptor), ops));
      return nullptr;
    }

    kj::Maybe<kj::Own<ClientHook>> writeTarget(
        rpc::MessageTarget::Builder target) override {
      auto builder = target.initPromisedAnswer();
      builder.setQuestionId(questionRef->getId());
      builder.adoptTransform(fromPipelineOps(Orphanage::getForMessageContaining(builder), ops));
      return nullptr;
    }

    kj::Own<ClientHook> getInnermostClient() override {
      return kj::addRef(*this);
    }

    // implements ClientHook -----------------------------------------

    kj::Maybe<ClientHook&> getResolved() override {
      return nullptr;
    }

    kj::Maybe<kj::Promise<kj::Own<ClientHook>>> whenMoreResolved() override {
      return nullptr;
    }

    kj::Maybe<int> getFd() override {
      return nullptr;
    }

  private:
    kj::Own<QuestionRef> questionRef;
    kj::Array<PipelineOp> ops;
  };

  class PromiseClient final: public RpcClient {
    // A ClientHook that initially wraps one client (in practice, an ImportClient or a
    // PipelineClient) and then, later on, redirects to some other client.

  public:
    PromiseClient(RpcConnectionState& connectionState,
                  kj::Own<RpcClient> initial,
                  kj::Promise<kj::Own<ClientHook>> eventual,
                  kj::Maybe<ImportId> importId)
        : RpcClient(connectionState),
          cap(kj::mv(initial)),
          importId(importId),
          fork(eventual.then(
              [this](kj::Own<ClientHook>&& resolution) {
                return resolve(kj::mv(resolution));
              }, [this](kj::Exception&& exception) {
                return resolve(newBrokenCap(kj::mv(exception)));
              }).catch_([&](kj::Exception&& e) {
                // Make any exceptions thrown from resolve() go to the connection's TaskSet which
                // will cause the connection to be terminated.
                connectionState.tasks.add(kj::cp(e));
                return newBrokenCap(kj::mv(e));
              }).fork()) {}
    // Create a client that starts out forwarding all calls to `initial` but, once `eventual`
    // resolves, will forward there instead.

    ~PromiseClient() noexcept(false) {
      KJ_IF_MAYBE(id, importId) {
        // This object is representing an import promise.  That means the import table may still
        // contain a pointer back to it.  Remove that pointer.  Note that we have to verify that
        // the import still exists and the pointer still points back to this object because this
        // object may actually outlive the import.
        KJ_IF_MAYBE(import, connectionState->imports.find(*id)) {
          KJ_IF_MAYBE(c, import->appClient) {
            if (c == this) {
              import->appClient = nullptr;
            }
          }
        }
      }
    }

    kj::Maybe<ExportId> writeDescriptor(rpc::CapDescriptor::Builder descriptor,
                                        kj::Vector<int>& fds) override {
      receivedCall = true;
      return connectionState->writeDescriptor(*cap, descriptor, fds);
    }

    kj::Maybe<kj::Own<ClientHook>> writeTarget(
        rpc::MessageTarget::Builder target) override {
      receivedCall = true;
      return connectionState->writeTarget(*cap, target);
    }

    kj::Own<ClientHook> getInnermostClient() override {
      receivedCall = true;
      return connectionState->getInnermostClient(*cap);
    }

    void adoptFlowController(kj::Own<RpcFlowController> flowController) override {
      if (cap->getBrand() == connectionState.get()) {
        // Pass the flow controller on to our inner cap.
        kj::downcast<RpcClient>(*cap).adoptFlowController(kj::mv(flowController));
      } else {
        // We resolved to a capability that isn't another RPC capability. We should simply make
        // sure that all the calls complete.
        connectionState->tasks.add(flowController->waitAllAcked().attach(kj::mv(flowController)));
      }
    }

    // implements ClientHook -----------------------------------------

    Request<AnyPointer, AnyPointer> newCall(
        uint64_t interfaceId, uint16_t methodId, kj::Maybe<MessageSize> sizeHint,
        CallHints hints) override {
      receivedCall = true;

      // IMPORTANT: We must call our superclass's version of newCall(), NOT cap->newCall(), because
      //   the Request object we create needs to check at send() time whether the promise has
      //   resolved and, if so, redirect to the new target.
      return RpcClient::newCall(interfaceId, methodId, sizeHint, hints);
    }

    VoidPromiseAndPipeline call(uint64_t interfaceId, uint16_t methodId,
                                kj::Own<CallContextHook>&& context, CallHints hints) override {
      receivedCall = true;
      return cap->call(interfaceId, methodId, kj::mv(context), hints);
    }

    kj::Maybe<ClientHook&> getResolved() override {
      if (isResolved()) {
        return *cap;
      } else {
        return nullptr;
      }
    }

    kj::Maybe<kj::Promise<kj::Own<ClientHook>>> whenMoreResolved() override {
      return fork.addBranch();
    }

    kj::Maybe<int> getFd() override {
      if (isResolved()) {
        return cap->getFd();
      } else {
        // In theory, before resolution, the ImportClient for the promise could have an FD
        // attached, if the promise itself was presented with an attached FD. However, we can't
        // really return that one here because it may be closed when we get the Resolve message
        // later. In theory we could have the PromiseClient itself take ownership of an FD that
        // arrived attached to a promise cap, but the use case for that is questionable. I'm
        // keeping it simple for now.
        return nullptr;
      }
    }

  private:
    kj::Own<ClientHook> cap;

    kj::Maybe<ImportId> importId;
    kj::ForkedPromise<kj::Own<ClientHook>> fork;

    bool receivedCall = false;

    enum {
      UNRESOLVED,
      // Not resolved at all yet.

      REMOTE,
      // Remote promise resolved to a remote settled capability (or null/error).

      REFLECTED,
      // Remote promise resolved to one of our own exports.

      MERGED,
      // Remote promise resolved to another remote promise which itself wasn't resolved yet, so we
      // merged them. In this case, `cap` is guaranteed to point to another PromiseClient.

      BROKEN
      // Resolved to null or error.
    } resolutionType = UNRESOLVED;

    inline bool isResolved() {
      return resolutionType != UNRESOLVED;
    }

    kj::Promise<kj::Own<ClientHook>> resolve(kj::Own<ClientHook> replacement) {
      KJ_DASSERT(!isResolved());

      const void* replacementBrand = replacement->getBrand();
      bool isSameConnection = replacementBrand == connectionState.get();
      if (isSameConnection) {
        // We resolved to some other RPC capability hosted by the same peer.
        KJ_IF_MAYBE(promise, replacement->whenMoreResolved()) {
          // We resolved to another remote promise. If *that* promise eventually resolves back
          // to us, we'll need a disembargo. Possibilities:
          // 1. The other promise hasn't resolved at all yet. In that case we can simply set its
          //    `receivedCall` flag and let it handle the disembargo later.
          // 2. The other promise has received a Resolve message and decided to initiate a
          //    disembargo which it is still waiting for. In that case we will certainly also need
          //    a disembargo for the same reason that the other promise did. And, we can't simply
          //    wait for their disembargo; we need to start a new one of our own.
          // 3. The other promise has resolved already (with or without a disembargo). In this
          //    case we should treat it as if we resolved directly to the other promise's result,
          //    possibly requiring a disembargo under the same conditions.

          // We know the other object is a PromiseClient because it's the only ClientHook
          // type in the RPC implementation which returns non-null for `whenMoreResolved()`.
          PromiseClient* other = &kj::downcast<PromiseClient>(*replacement);
          while (other->resolutionType == MERGED) {
            // There's no need to resolve to a thing that's just going to resolve to another thing.
            replacement = other->cap->addRef();
            other = &kj::downcast<PromiseClient>(*replacement);

            // Note that replacementBrand is unchanged since we'd only merge with other
            // PromiseClients on the same connection.
            KJ_DASSERT(replacement->getBrand() == replacementBrand);
          }

          if (other->isResolved()) {
            // The other capability resolved already. If it determined that it resolved as
            // reflected, then we determine the same.
            resolutionType = other->resolutionType;
          } else {
            // The other capability hasn't resolved yet, so we can safely merge with it and do a
            // single combined disembargo if needed later.
            other->receivedCall = other->receivedCall || receivedCall;
            resolutionType = MERGED;
          }
        } else {
          resolutionType = REMOTE;
        }
      } else {
        if (replacementBrand == &ClientHook::NULL_CAPABILITY_BRAND ||
            replacementBrand == &ClientHook::BROKEN_CAPABILITY_BRAND) {
          // We don't consider null or broken capabilities as "reflected" because they may have
          // been communicated to us literally as a null pointer or an exception on the wire,
          // rather than as a reference to one of our exports, in which case a disembargo won't
          // work. But also, call ordering is completely irrelevant with these so there's no need
          // to disembargo anyway.
          resolutionType = BROKEN;
        } else {
          resolutionType = REFLECTED;
        }
      }

      // Every branch above ends by setting resolutionType to something other than UNRESOLVED.
      KJ_DASSERT(isResolved());

      // If the original capability was used for streaming calls, it will have a
      // `flowController` that might still be shepherding those calls. We'll need make sure that
      // it doesn't get thrown away. Note that we know that *cap is an RpcClient because resolve()
      // is only called once and our constructor required that the initial capability is an
      // RpcClient.
      KJ_IF_MAYBE(f, kj::downcast<RpcClient>(*cap).flowController) {
        if (isSameConnection) {
          // The new target is on the same connection. It would make a lot of sense to keep using
          // the same flow controller if possible.
          kj::downcast<RpcClient>(*replacement).adoptFlowController(kj::mv(*f));
        } else {
          // The new target is something else. The best we can do is wait for the controller to
          // drain. New calls will be flow-controlled in a new way without knowing about the old
          // controller.
          connectionState->tasks.add(f->get()->waitAllAcked().attach(kj::mv(*f)));
        }
      }

      if (resolutionType == REFLECTED && receivedCall &&
          connectionState->connection.is<Connected>()) {
        // The new capability is hosted locally, not on the remote machine.  And, we had made calls
        // to the promise.  We need to make sure those calls echo back to us before we allow new
        // calls to go directly to the local capability, so we need to set a local embargo and send
        // a `Disembargo` to echo through the peer.

        auto message = connectionState->connection.get<Connected>()->newOutgoingMessage(
            messageSizeHint<rpc::Disembargo>() + MESSAGE_TARGET_SIZE_HINT);

        auto disembargo = message->getBody().initAs<rpc::Message>().initDisembargo();

        {
          auto redirect = connectionState->writeTarget(*cap, disembargo.initTarget());
          KJ_ASSERT(redirect == nullptr,
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

      cap = replacement->addRef();

      return kj::mv(replacement);
    }
  };

  kj::Maybe<ExportId> writeDescriptor(ClientHook& cap, rpc::CapDescriptor::Builder descriptor,
                                      kj::Vector<int>& fds) {
    // Write a descriptor for the given capability.

    // Find the innermost wrapped capability.
    ClientHook* inner = &cap;
    for (;;) {
      KJ_IF_MAYBE(resolved, inner->getResolved()) {
        inner = resolved;
      } else {
        break;
      }
    }

    KJ_IF_MAYBE(fd, inner->getFd()) {
      descriptor.setAttachedFd(fds.size());
      fds.add(kj::mv(*fd));
    }

    if (inner->getBrand() == this) {
      return kj::downcast<RpcClient>(*inner).writeDescriptor(descriptor, fds);
    } else {
      auto iter = exportsByCap.find(inner);
      if (iter != exportsByCap.end()) {
        // We've already seen and exported this capability before.  Just up the refcount.
        auto& exp = KJ_ASSERT_NONNULL(exports.find(iter->second));
        ++exp.refcount;
        if (exp.resolveOp == nullptr) {
          descriptor.setSenderHosted(iter->second);
        } else {
          descriptor.setSenderPromise(iter->second);
        }
        return iter->second;
      } else {
        // This is the first time we've seen this capability.
        ExportId exportId;
        auto& exp = exports.next(exportId);
        exportsByCap[inner] = exportId;
        exp.refcount = 1;
        exp.clientHook = inner->addRef();

        KJ_IF_MAYBE(wrapped, inner->whenMoreResolved()) {
          // This is a promise.  Arrange for the `Resolve` message to be sent later.
          exp.resolveOp = resolveExportedPromise(exportId, kj::mv(*wrapped));
          descriptor.setSenderPromise(exportId);
        } else {
          descriptor.setSenderHosted(exportId);
        }

        return exportId;
      }
    }
  }

  kj::Array<ExportId> writeDescriptors(kj::ArrayPtr<kj::Maybe<kj::Own<ClientHook>>> capTable,
                                       rpc::Payload::Builder payload, kj::Vector<int>& fds) {
    if (capTable.size() == 0) {
      // Calling initCapTable(0) will still allocate a 1-word tag, which we'd like to avoid...
      return nullptr;
    }

    auto capTableBuilder = payload.initCapTable(capTable.size());
    kj::Vector<ExportId> exports(capTable.size());
    for (uint i: kj::indices(capTable)) {
      KJ_IF_MAYBE(cap, capTable[i]) {
        KJ_IF_MAYBE(exportId, writeDescriptor(**cap, capTableBuilder[i], fds)) {
          exports.add(*exportId);
        }
      } else {
        capTableBuilder[i].setNone();
      }
    }
    return exports.releaseAsArray();
  }

  kj::Maybe<kj::Own<ClientHook>> writeTarget(ClientHook& cap, rpc::MessageTarget::Builder target) {
    // If calls to the given capability should pass over this connection, fill in `target`
    // appropriately for such a call and return nullptr.  Otherwise, return a `ClientHook` to which
    // the call should be forwarded; the caller should then delegate the call to that `ClientHook`.
    //
    // The main case where this ends up returning non-null is if `cap` is a promise that has
    // recently resolved.  The application might have started building a request before the promise
    // resolved, and so the request may have been built on the assumption that it would be sent over
    // this network connection, but then the promise resolved to point somewhere else before the
    // request was sent.  Now the request has to be redirected to the new target instead.

    if (cap.getBrand() == this) {
      return kj::downcast<RpcClient>(cap).writeTarget(target);
    } else {
      return cap.addRef();
    }
  }

  kj::Own<ClientHook> getInnermostClient(ClientHook& client) {
    ClientHook* ptr = &client;
    for (;;) {
      KJ_IF_MAYBE(inner, ptr->getResolved()) {
        ptr = inner;
      } else {
        break;
      }
    }

    if (ptr->getBrand() == this) {
      return kj::downcast<RpcClient>(*ptr).getInnermostClient();
    } else {
      return ptr->addRef();
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

      // Get the innermost ClientHook backing the resolved client.  This includes traversing
      // PromiseClients that haven't resolved yet to their underlying ImportClient or
      // PipelineClient, so that we get a remote promise that might resolve later.  This is
      // important to make sure that if the peer sends a `Disembargo` back to us, it bounces back
      // correctly instead of going to the result of some future resolution.  See the documentation
      // for `Disembargo` in `rpc.capnp`.
      resolution = getInnermostClient(*resolution);

      // Update the export table to point at this object instead.  We know that our entry in the
      // export table is still live because when it is destroyed the asynchronous resolution task
      // (i.e. this code) is canceled.
      auto& exp = KJ_ASSERT_NONNULL(exports.find(exportId));
      exportsByCap.erase(exp.clientHook);
      exp.clientHook = kj::mv(resolution);

      if (exp.clientHook->getBrand() != this) {
        // We're resolving to a local capability.  If we're resolving to a promise, we might be
        // able to reuse our export table entry and avoid sending a message.

        KJ_IF_MAYBE(promise, exp.clientHook->whenMoreResolved()) {
          // We're replacing a promise with another local promise.  In this case, we might actually
          // be able to just reuse the existing export table entry to represent the new promise --
          // unless it already has an entry.  Let's check.

          auto insertResult = exportsByCap.insert(std::make_pair(exp.clientHook.get(), exportId));

          if (insertResult.second) {
            // The new promise was not already in the table, therefore the existing export table
            // entry has now been repurposed to represent it.  There is no need to send a resolve
            // message at all.  We do, however, have to start resolving the next promise.
            return resolveExportedPromise(exportId, kj::mv(*promise));
          }
        }
      }

      // OK, we have to send a `Resolve` message.
      auto message = connection.get<Connected>()->newOutgoingMessage(
          messageSizeHint<rpc::Resolve>() + sizeInWords<rpc::CapDescriptor>() + 16);
      auto resolve = message->getBody().initAs<rpc::Message>().initResolve();
      resolve.setPromiseId(exportId);
      kj::Vector<int> fds;
      writeDescriptor(*exp.clientHook, resolve.initCap(), fds);
      message->setFds(fds.releaseAsArray());
      message->send();

      return kj::READY_NOW;
    }, [this,exportId](kj::Exception&& exception) {
      // send error resolution
      auto message = connection.get<Connected>()->newOutgoingMessage(
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

  kj::Own<ClientHook> import(ImportId importId, bool isPromise, kj::Maybe<kj::AutoCloseFd> fd) {
    // Receive a new import.

    auto& import = imports[importId];
    kj::Own<ImportClient> importClient;

    // Create the ImportClient, or if one already exists, use it.
    KJ_IF_MAYBE(c, import.importClient) {
      importClient = kj::addRef(*c);

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
      KJ_IF_MAYBE(c, import.appClient) {
        // Use the existing one.
        return kj::addRef(*c);
      } else {
        // Create a promise for this import's resolution.
        auto paf = kj::newPromiseAndFulfiller<kj::Own<ClientHook>>();
        import.promiseFulfiller = kj::mv(paf.fulfiller);

        // Make sure the import is not destroyed while this promise exists.
        paf.promise = paf.promise.attach(kj::addRef(*importClient));

        // Create a PromiseClient around it and return it.
        auto result = kj::refcounted<PromiseClient>(
            *this, kj::mv(importClient), kj::mv(paf.promise), importId);
        import.appClient = *result;
        return kj::mv(result);
      }
    } else {
      import.appClient = *importClient;
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
      return nullptr;
    }
    kj::Maybe<kj::Promise<kj::Own<ClientHook>>> whenMoreResolved() override {
      // We always wrap either PipelineClient or ImportClient, both of which return null for this
      // anyway.
      return nullptr;
    }
    kj::Own<ClientHook> addRef() override {
      return kj::addRef(*this);
    }
    const void* getBrand() override {
      return nullptr;
    }
    kj::Maybe<int> getFd() override {
      return inner->getFd();
    }

  private:
    kj::Own<ClientHook> inner;
  };

  kj::Maybe<kj::Own<ClientHook>> receiveCap(rpc::CapDescriptor::Reader descriptor,
                                            kj::ArrayPtr<kj::AutoCloseFd> fds) {
    uint fdIndex = descriptor.getAttachedFd();
    kj::Maybe<kj::AutoCloseFd> fd;
    if (fdIndex < fds.size() && fds[fdIndex] != nullptr) {
      fd = kj::mv(fds[fdIndex]);
    }

    switch (descriptor.which()) {
      case rpc::CapDescriptor::NONE:
        return nullptr;

      case rpc::CapDescriptor::SENDER_HOSTED:
        return import(descriptor.getSenderHosted(), false, kj::mv(fd));
      case rpc::CapDescriptor::SENDER_PROMISE:
        return import(descriptor.getSenderPromise(), true, kj::mv(fd));

      case rpc::CapDescriptor::RECEIVER_HOSTED:
        KJ_IF_MAYBE(exp, exports.find(descriptor.getReceiverHosted())) {
          auto result = exp->clientHook->addRef();
          if (result->getBrand() == this) {
            result = kj::refcounted<TribbleRaceBlocker>(kj::mv(result));
          }
          return kj::mv(result);
        } else {
          return newBrokenCap("invalid 'receiverHosted' export ID");
        }

      case rpc::CapDescriptor::RECEIVER_ANSWER: {
        auto promisedAnswer = descriptor.getReceiverAnswer();

        KJ_IF_MAYBE(answer, answers.find(promisedAnswer.getQuestionId())) {
          if (answer->active) {
            KJ_IF_MAYBE(pipeline, answer->pipeline) {
              KJ_IF_MAYBE(ops, toPipelineOps(promisedAnswer.getTransform())) {
                auto result = pipeline->get()->getPipelinedCap(*ops);
                if (result->getBrand() == this) {
                  result = kj::refcounted<TribbleRaceBlocker>(kj::mv(result));
                }
                return kj::mv(result);
              } else {
                return newBrokenCap("unrecognized pipeline ops");
              }
            }
          }
        }

        return newBrokenCap("invalid 'receiverAnswer'");
      }

      case rpc::CapDescriptor::THIRD_PARTY_HOSTED:
        // We don't support third-party caps, so use the vine instead.
        return import(descriptor.getThirdPartyHosted().getVineId(), false, kj::mv(fd));

      default:
        KJ_FAIL_REQUIRE("unknown CapDescriptor type") { break; }
        return newBrokenCap("unknown CapDescriptor type");
    }
  }

  kj::Array<kj::Maybe<kj::Own<ClientHook>>> receiveCaps(List<rpc::CapDescriptor>::Reader capTable,
                                                        kj::ArrayPtr<kj::AutoCloseFd> fds) {
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
    inline QuestionRef(
        RpcConnectionState& connectionState, QuestionId id,
        kj::Maybe<kj::Own<kj::PromiseFulfiller<kj::Promise<kj::Own<RpcResponse>>>>> fulfiller)
        : connectionState(kj::addRef(connectionState)), id(id), fulfiller(kj::mv(fulfiller)) {}

    ~QuestionRef() noexcept {
      // Contrary to KJ style, we declare this destructor `noexcept` because if anything in here
      // throws (without being caught) we're probably in pretty bad shape and going to be crashing
      // later anyway. Better to abort now.

      KJ_IF_MAYBE(c, connectionState) {
        auto& connectionState = *c;

        auto& question = KJ_ASSERT_NONNULL(
            connectionState->questions.find(id), "Question ID no longer on table?");

        // Send the "Finish" message (if the connection is not already broken).
        if (connectionState->connection.is<Connected>() && !question.skipFinish) {
          KJ_IF_MAYBE(e, kj::runCatchingExceptions([&]() {
            auto message = connectionState->connection.get<Connected>()->newOutgoingMessage(
                messageSizeHint<rpc::Finish>());
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
            connectionState->tasks.add(kj::mv(*e));
          }
        }

        // Check if the question has returned and, if so, remove it from the table.
        // Remove question ID from the table.  Must do this *after* sending `Finish` to ensure that
        // the ID is not re-allocated before the `Finish` message can be sent.
        if (question.isAwaitingReturn) {
          // Still waiting for return, so just remove the QuestionRef pointer from the table.
          question.selfRef = nullptr;
        } else {
          // Call has already returned, so we can now remove it from the table.
          connectionState->questions.erase(id, question);
        }
      }
    }

    inline QuestionId getId() const { return id; }

    void fulfill(kj::Own<RpcResponse>&& response) {
      KJ_IF_MAYBE(f, fulfiller) {
        f->get()->fulfill(kj::mv(response));
      }
    }

    void fulfill(kj::Promise<kj::Own<RpcResponse>>&& promise) {
      KJ_IF_MAYBE(f, fulfiller) {
        f->get()->fulfill(kj::mv(promise));
      }
    }

    void reject(kj::Exception&& exception) {
      KJ_IF_MAYBE(f, fulfiller) {
        f->get()->reject(kj::mv(exception));
      }
    }

    void disconnect() {
      connectionState = nullptr;
    }

  private:
    kj::Maybe<kj::Own<RpcConnectionState>> connectionState;
    QuestionId id;
    kj::Maybe<kj::Own<kj::PromiseFulfiller<kj::Promise<kj::Own<RpcResponse>>>>> fulfiller;
  };

  class RpcRequest final: public RequestHook {
  public:
    RpcRequest(RpcConnectionState& connectionState, VatNetworkBase::Connection& connection,
               kj::Maybe<MessageSize> sizeHint, kj::Own<RpcClient>&& target)
        : connectionState(kj::addRef(connectionState)),
          target(kj::mv(target)),
          message(connection.newOutgoingMessage(
              firstSegmentSize(sizeHint, messageSizeHint<rpc::Call>() +
                  sizeInWords<rpc::Payload>() + MESSAGE_TARGET_SIZE_HINT))),
          callBuilder(message->getBody().getAs<rpc::Message>().initCall()),
          paramsBuilder(capTable.imbue(callBuilder.getParams().getContent())) {}

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

      KJ_IF_MAYBE(redirect, target->writeTarget(callBuilder.getTarget())) {
        // Whoops, this capability has been redirected while we were building the request!
        // We'll have to make a new request and do a copy.  Ick.

        auto replacement = redirect->get()->newCall(
            callBuilder.getInterfaceId(), callBuilder.getMethodId(), paramsBuilder.targetSize(),
            callHintsFromReader(callBuilder));
        replacement.set(paramsBuilder);
        return replacement.send();
      } else {
        bool noPromisePipelining = callBuilder.getNoPromisePipelining();

        auto sendResult = sendInternal(false);

        kj::Own<PipelineHook> pipeline;
        if (noPromisePipelining) {
          pipeline = getDisabledPipeline();
        } else {
          auto forkedPromise = sendResult.promise.fork();

          // The pipeline must get notified of resolution before the app does to maintain ordering.
          pipeline = kj::refcounted<RpcPipeline>(
              *connectionState, kj::mv(sendResult.questionRef), forkedPromise.addBranch());

          sendResult.promise = forkedPromise.addBranch();
        }

        auto appPromise = sendResult.promise.then(
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

      KJ_IF_MAYBE(redirect, target->writeTarget(callBuilder.getTarget())) {
        // Whoops, this capability has been redirected while we were building the request!
        // We'll have to make a new request and do a copy.  Ick.

        auto replacement = redirect->get()->newCall(
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

      KJ_IF_MAYBE(redirect, target->writeTarget(callBuilder.getTarget())) {
        // Whoops, this capability has been redirected while we were building the request!
        // We'll have to make a new request and do a copy.  Ick.

        auto replacement = redirect->get()->newCall(
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
            *connectionState, kj::mv(questionRef));
        return AnyPointer::Pipeline(kj::mv(pipeline));
      }
    }

    struct TailInfo {
      QuestionId questionId;
      kj::Promise<void> promise;
      kj::Own<PipelineHook> pipeline;
    };

    kj::Maybe<TailInfo> tailSend() {
      // Send the request as a tail call.
      //
      // Returns null if for some reason a tail call is not possible and the caller should fall
      // back to using send() and copying the response.

      SendInternalResult sendResult;

      if (!connectionState->connection.is<Connected>()) {
        // Disconnected; fall back to a regular send() which will fail appropriately.
        return nullptr;
      }

      KJ_IF_MAYBE(redirect, target->writeTarget(callBuilder.getTarget())) {
        // Whoops, this capability has been redirected while we were building the request!
        // Fall back to regular send().
        return nullptr;
      } else {
        sendResult = sendInternal(true);
      }

      auto promise = sendResult.promise.then([](kj::Own<RpcResponse>&& response) {
        // Response should be null if `Return` handling code is correct.
        KJ_ASSERT(!response) { break; }
      });

      QuestionId questionId = sendResult.questionRef->getId();

      kj::Own<PipelineHook> pipeline;
      bool noPromisePipelining = callBuilder.getNoPromisePipelining();
      if (noPromisePipelining) {
        pipeline = getDisabledPipeline();
      } else {
        pipeline = kj::refcounted<RpcPipeline>(*connectionState, kj::mv(sendResult.questionRef));
      }

      return TailInfo { questionId, kj::mv(promise), kj::mv(pipeline) };
    }

    const void* getBrand() override {
      return connectionState.get();
    }

  private:
    kj::Own<RpcConnectionState> connectionState;

    kj::Own<RpcClient> target;
    kj::Own<OutgoingRpcMessage> message;
    BuilderCapabilityTable capTable;
    rpc::Call::Builder callBuilder;
    AnyPointer::Builder paramsBuilder;

    struct SendInternalResult {
      kj::Own<QuestionRef> questionRef;
      kj::Promise<kj::Own<RpcResponse>> promise = nullptr;
    };

    struct SetupSendResult: public SendInternalResult {
      QuestionId questionId;
      Question& question;

      SetupSendResult(SendInternalResult&& super, QuestionId questionId, Question& question)
          : SendInternalResult(kj::mv(super)), questionId(questionId), question(question) {}
      // TODO(cleanup): This constructor is implicit in C++17.
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

      // Make the QuentionRef and result promise.
      SendInternalResult result;
      auto paf = kj::newPromiseAndFulfiller<kj::Promise<kj::Own<RpcResponse>>>();
      result.questionRef = kj::refcounted<QuestionRef>(
          *connectionState, questionId, kj::mv(paf.fulfiller));
      question.selfRef = *result.questionRef;
      result.promise = paf.promise.attach(kj::addRef(*result.questionRef));

      return { kj::mv(result), questionId, question };
    }

    SendInternalResult sendInternal(bool isTailCall) {
      auto result = setupSend(isTailCall);

      // Finish and send.
      callBuilder.setQuestionId(result.questionId);
      if (isTailCall) {
        callBuilder.getSendResultsTo().setYourself();
      }
      KJ_IF_MAYBE(exception, kj::runCatchingExceptions([&]() {
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
        result.questionRef->reject(kj::mv(*exception));
      }

      // Send and return.
      return kj::mv(result);
    }

    kj::Promise<void> sendStreamingInternal(bool isTailCall) {
      auto setup = setupSend(isTailCall);

      // Finish and send.
      callBuilder.setQuestionId(setup.questionId);
      if (isTailCall) {
        callBuilder.getSendResultsTo().setYourself();
      }
      kj::Promise<void> flowPromise = nullptr;
      KJ_IF_MAYBE(exception, kj::runCatchingExceptions([&]() {
        KJ_CONTEXT("sending RPC call",
           callBuilder.getInterfaceId(), callBuilder.getMethodId());
        RpcFlowController* flow;
        KJ_IF_MAYBE(f, target->flowController) {
          flow = *f;
        } else {
          flow = target->flowController.emplace(
              connectionState->connection.get<Connected>()->newStream());
        }
        flowPromise = flow->send(kj::mv(message), setup.promise.ignoreResult());
      })) {
        // We can't safely throw the exception from here since we've already modified the question
        // table state. We'll have to reject the promise instead.
        setup.question.isAwaitingReturn = false;
        setup.question.skipFinish = true;
        setup.questionRef->reject(kj::cp(*exception));
        return kj::mv(*exception);
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
      auto& question = connectionState->questions.nextHigh(questionId);
      question.isAwaitingReturn = false;  // No Return needed
      question.paramExports = kj::mv(exports);
      question.isTailCall = false;

      // Make the QuentionRef and result promise.
      auto questionRef = kj::refcounted<QuestionRef>(*connectionState, questionId, nullptr);
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
                kj::Promise<kj::Own<RpcResponse>>&& redirectLaterParam)
        : connectionState(kj::addRef(connectionState)),
          redirectLater(redirectLaterParam.fork()),
          resolveSelfPromise(KJ_ASSERT_NONNULL(redirectLater).addBranch().then(
              [this](kj::Own<RpcResponse>&& response) {
                resolve(kj::mv(response));
              }, [this](kj::Exception&& exception) {
                resolve(kj::mv(exception));
              }).eagerlyEvaluate([&](kj::Exception&& e) {
                // Make any exceptions thrown from resolve() go to the connection's TaskSet which
                // will cause the connection to be terminated.
                connectionState.tasks.add(kj::mv(e));
              })) {
      // Construct a new RpcPipeline.

      state.init<Waiting>(kj::mv(questionRef));
    }

    RpcPipeline(RpcConnectionState& connectionState, kj::Own<QuestionRef>&& questionRef)
        : connectionState(kj::addRef(connectionState)),
          resolveSelfPromise(nullptr) {
      // Construct a new RpcPipeline that is never expected to resolve.

      state.init<Waiting>(kj::mv(questionRef));
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
      return clientMap.findOrCreate(ops.asPtr(), [&]() {
        if (state.is<Waiting>()) {
          // Wrap a PipelineClient in a PromiseClient.
          auto pipelineClient = kj::refcounted<PipelineClient>(
              *connectionState, kj::addRef(*state.get<Waiting>()), kj::heapArray(ops.asPtr()));

          KJ_IF_MAYBE(r, redirectLater) {
            auto resolutionPromise = r->addBranch().then(
                [ops = kj::heapArray(ops.asPtr())](kj::Own<RpcResponse>&& response) {
                  return response->getResults().getPipelinedCap(kj::mv(ops));
                });

            return kj::HashMap<kj::Array<PipelineOp>, kj::Own<ClientHook>>::Entry {
              kj::mv(ops),
              kj::refcounted<PromiseClient>(
                  *connectionState, kj::mv(pipelineClient), kj::mv(resolutionPromise), nullptr)
            };
          } else {
            // Oh, this pipeline will never get redirected, so just return the PipelineClient.
            return kj::HashMap<kj::Array<PipelineOp>, kj::Own<ClientHook>>::Entry {
              kj::mv(ops), kj::mv(pipelineClient)
            };
          }
        } else if (state.is<Resolved>()) {
          auto pipelineClient = state.get<Resolved>()->getResults().getPipelinedCap(ops);
          return kj::HashMap<kj::Array<PipelineOp>, kj::Own<ClientHook>>::Entry {
            kj::mv(ops), kj::mv(pipelineClient)
          };
        } else {
          return kj::HashMap<kj::Array<PipelineOp>, kj::Own<ClientHook>>::Entry {
            kj::mv(ops), newBrokenCap(kj::cp(state.get<Broken>()))
          };
        }
      })->addRef();
    }

  private:
    kj::Own<RpcConnectionState> connectionState;
    kj::Maybe<kj::ForkedPromise<kj::Own<RpcResponse>>> redirectLater;

    typedef kj::Own<QuestionRef> Waiting;
    typedef kj::Own<RpcResponse> Resolved;
    typedef kj::Exception Broken;
    kj::OneOf<Waiting, Resolved, Broken> state;

    kj::HashMap<kj::Array<PipelineOp>, kj::Own<ClientHook>> clientMap;
    // See QueuedPipeline::clientMap in capability.c++ for a discussion of why we must memoize
    // the results of getPipelinedCap(). RpcPipeline has a similar problem when a capability we
    // return is later subject to an embargo. It's important that the embargo is correctly applied
    // across all calls to the same capability.

    // Keep this last, because the continuation uses *this, so it should be destroyed first to
    // ensure the continuation is not still running.
    kj::Promise<void> resolveSelfPromise;

    void resolve(kj::Own<RpcResponse>&& response) {
      KJ_ASSERT(state.is<Waiting>(), "Already resolved?");
      state.init<Resolved>(kj::mv(response));
    }

    void resolve(const kj::Exception&& exception) {
      KJ_ASSERT(state.is<Waiting>(), "Already resolved?");
      state.init<Broken>(kj::mv(exception));
    }
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
      // Send the response and return the export list.  Returns nullptr if there were no caps.
      // (Could return a non-null empty array if there were caps but none of them were exports.)

      // Build the cap table.
      auto capTable = this->capTable.getTable();
      kj::Vector<int> fds;
      auto exports = connectionState.writeDescriptors(capTable, payload, fds);
      message->setFds(fds.releaseAsArray());

      // Populate `resolutionsAtReturnTime`.
      for (auto& slot: capTable) {
        KJ_IF_MAYBE(cap, slot) {
          auto inner = connectionState.getInnermostClient(**cap);
          if (inner.get() != cap->get()) {
            resolutionsAtReturnTime.upsert(cap->get(), kj::mv(inner),
                [&](kj::Own<ClientHook>& existing, kj::Own<ClientHook>&& replacement) {
              KJ_ASSERT(existing.get() == replacement.get());
            });
          }
        }
      }

      message->send();
      if (capTable.size() == 0) {
        return nullptr;
      } else {
        return kj::mv(exports);
      }
    }

    struct Resolution {
      kj::Own<ClientHook> returnedCap;
      // The capabiilty that appeared in the response message in this slot.

      kj::Own<ClientHook> unwrapped;
      // Exactly what `getInnermostClient(returnedCap)` produced at the time that the return
      // message was encoded.
    };

    Resolution getResolutionAtReturnTime(kj::ArrayPtr<const PipelineOp> ops) {
      auto returnedCap = getResultsBuilder().asReader().getPipelinedCap(ops);
      kj::Own<ClientHook> unwrapped;
      KJ_IF_MAYBE(u, resolutionsAtReturnTime.find(returnedCap.get())) {
        unwrapped = u->get()->addRef();
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
    // pipelined calls received targetting those capabilities (as well as any Disembargo messages)
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

    kj::Own<ClientHook> getResolutionAtReturnTime(
        kj::Own<ClientHook> original, RpcServerResponseImpl::Resolution resolution) {
      // Wait for `original` to resolve to `resolution.returnedCap`, then return
      // `resolution.unwrapped`.

      ClientHook* ptr = original.get();
      for (;;) {
        if (ptr == resolution.returnedCap.get()) {
          return kj::mv(resolution.unwrapped);
        } else KJ_IF_MAYBE(r, ptr->getResolved()) {
          ptr = r;
        } else {
          break;
        }
      }

      KJ_IF_MAYBE(p, ptr->whenMoreResolved()) {
        return newLocalPromiseClient(p->then(
            [this, original = kj::mv(original), resolution = kj::mv(resolution)]
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

  class RpcCallContext final: public CallContextHook, public kj::Refcounted {
  public:
    RpcCallContext(RpcConnectionState& connectionState, AnswerId answerId,
                   kj::Own<IncomingRpcMessage>&& request,
                   kj::Array<kj::Maybe<kj::Own<ClientHook>>> capTableArray,
                   const AnyPointer::Reader& params,
                   bool redirectResults, uint64_t interfaceId, uint16_t methodId,
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
          redirectResults(redirectResults) {
      connectionState.callWordsInFlight += requestSize;
    }

    ~RpcCallContext() noexcept(false) {
      if (isFirstResponder()) {
        // We haven't sent a return yet, so we must have been canceled.  Send a cancellation return.
        unwindDetector.catchExceptionsIfUnwinding([&]() {
          // Don't send anything if the connection is broken, or if the onlyPromisePipeline hint
          // was used (in which case the caller doesn't care to receive a `Return`).
          bool shouldFreePipeline = true;
          if (connectionState->connection.is<Connected>() && !hints.onlyPromisePipeline) {
            auto message = connectionState->connection.get<Connected>()->newOutgoingMessage(
                messageSizeHint<rpc::Return>() + sizeInWords<rpc::Payload>());
            auto builder = message->getBody().initAs<rpc::Message>().initReturn();

            builder.setAnswerId(answerId);
            builder.setReleaseParamCaps(false);

            if (redirectResults) {
              // The reason we haven't sent a return is because the results were sent somewhere
              // else.
              builder.setResultsSentElsewhere();

              // The pipeline could still be valid and in-use in this case.
              shouldFreePipeline = false;
            } else {
              builder.setCanceled();
            }

            message->send();
          }

          cleanupAnswerTable(nullptr, shouldFreePipeline);
        });
      }
    }

    kj::Own<RpcResponse> consumeRedirectedResponse() {
      KJ_ASSERT(redirectResults);

      if (response == nullptr) getResults(MessageSize{0, 0});  // force initialization of response

      // Note that the context needs to keep its own reference to the response so that it doesn't
      // get GC'd until the PipelineHook drops its reference to the context.
      return kj::downcast<LocallyRedirectedRpcResponse>(*KJ_ASSERT_NONNULL(response)).addRef();
    }

    void sendReturn() {
      KJ_ASSERT(!redirectResults);
      KJ_ASSERT(!hints.onlyPromisePipeline);

      // Avoid sending results if canceled so that we don't have to figure out whether or not
      // `releaseResultCaps` was set in the already-received `Finish`.
      if (!receivedFinish && isFirstResponder()) {
        KJ_ASSERT(connectionState->connection.is<Connected>(),
                  "Cancellation should have been requested on disconnect.") {
          return;
        }

        if (response == nullptr) getResults(MessageSize{0, 0});  // force initialization of response

        returnMessage.setAnswerId(answerId);
        returnMessage.setReleaseParamCaps(false);

        auto& responseImpl = kj::downcast<RpcServerResponseImpl>(*KJ_ASSERT_NONNULL(response));
        if (!responseImpl.hasCapabilities()) {
          returnMessage.setNoFinishNeeded(true);

          // Tell ourselves that a finsih was already received, so that `cleanupAnswerTable()`
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
        KJ_IF_MAYBE(exception, kj::runCatchingExceptions([&]() {
          // Debug info in case send() fails due to overside message.
          KJ_CONTEXT("returning from RPC call", interfaceId, methodId);
          exports = responseImpl.send();
        })) {
          responseSent = false;
          sendErrorReturn(kj::mv(*exception));
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

        KJ_IF_MAYBE(e, exports) {
          // Caps were returned, so we can't free the pipeline yet.
          cleanupAnswerTable(kj::mv(*e), false);
        } else {
          // No caps in the results, therefore the pipeline is irrelevant.
          cleanupAnswerTable(nullptr, true);
        }
      }
    }
    void sendErrorReturn(kj::Exception&& exception) {
      KJ_ASSERT(!redirectResults);
      KJ_ASSERT(!hints.onlyPromisePipeline);
      if (isFirstResponder()) {
        if (connectionState->connection.is<Connected>()) {
          auto message = connectionState->connection.get<Connected>()->newOutgoingMessage(
              messageSizeHint<rpc::Return>() + exceptionSizeHint(exception));
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
    void sendRedirectReturn() {
      KJ_ASSERT(redirectResults);
      KJ_ASSERT(!hints.onlyPromisePipeline);

      if (isFirstResponder()) {
        auto message = connectionState->connection.get<Connected>()->newOutgoingMessage(
            messageSizeHint<rpc::Return>());
        auto builder = message->getBody().initAs<rpc::Message>().initReturn();

        builder.setAnswerId(answerId);
        builder.setReleaseParamCaps(false);
        builder.setResultsSentElsewhere();

        // TODO(perf): Could `noFinishNeeded` be used here? The `Finish` messages are pretty
        //   redundant after a redirect, but as this case is less common and more complicated I
        //   don't want to fully think through the implications right now.

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
      KJ_REQUIRE(request != nullptr, "Can't call getParams() after releaseParams().");
      return params;
    }
    void releaseParams() override {
      request = nullptr;
    }
    AnyPointer::Builder getResults(kj::Maybe<MessageSize> sizeHint) override {
      KJ_IF_MAYBE(r, response) {
        return r->get()->getResultsBuilder();
      } else {
        kj::Own<RpcServerResponse> response;

        if (redirectResults || !connectionState->connection.is<Connected>()) {
          response = kj::refcounted<LocallyRedirectedRpcResponse>(sizeHint);
        } else {
          auto message = connectionState->connection.get<Connected>()->newOutgoingMessage(
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
      KJ_IF_MAYBE(f, tailCallPipelineFulfiller) {
        f->get()->fulfill(AnyPointer::Pipeline(kj::mv(pipeline)));
      }
    }
    kj::Promise<void> tailCall(kj::Own<RequestHook>&& request) override {
      auto result = directTailCall(kj::mv(request));
      KJ_IF_MAYBE(f, tailCallPipelineFulfiller) {
        f->get()->fulfill(AnyPointer::Pipeline(kj::mv(result.pipeline)));
      }
      return kj::mv(result.promise);
    }
    ClientHook::VoidPromiseAndPipeline directTailCall(kj::Own<RequestHook>&& request) override {
      KJ_REQUIRE(response == nullptr,
                 "Can't call tailCall() after initializing the results struct.");

      if (request->getBrand() == connectionState.get() &&
          !redirectResults && !hints.noPromisePipelining) {
        // The tail call is headed towards the peer that called us in the first place, so we can
        // optimize out the return trip.
        //
        // If the noPromisePipelining hint was sent, we skip this trick since the caller will
        // ignore the `Return` message anyway.

        KJ_IF_MAYBE(tailInfo, kj::downcast<RpcRequest>(*request).tailSend()) {
          if (isFirstResponder()) {
            if (connectionState->connection.is<Connected>()) {
              auto message = connectionState->connection.get<Connected>()->newOutgoingMessage(
                  messageSizeHint<rpc::Return>());
              auto builder = message->getBody().initAs<rpc::Message>().initReturn();

              builder.setAnswerId(answerId);
              builder.setReleaseParamCaps(false);
              builder.setTakeFromOtherQuestion(tailInfo->questionId);

              message->send();
            }

            // There are no caps in our return message, but of course the tail results could have
            // caps, so we must continue to honor pipeline calls (and just bounce them back).
            cleanupAnswerTable(nullptr, false);
          }
          return { kj::mv(tailInfo->promise), kj::mv(tailInfo->pipeline) };
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

    // Request ---------------------------------------------

    size_t requestSize;  // for flow limit purposes
    kj::Maybe<kj::Own<IncomingRpcMessage>> request;
    ReaderCapabilityTable paramsCapTable;
    AnyPointer::Reader params;

    // Response --------------------------------------------

    kj::Maybe<kj::Own<RpcServerResponse>> response;
    rpc::Return::Builder returnMessage;
    bool redirectResults = false;
    bool responseSent = false;
    kj::Maybe<kj::Own<kj::PromiseFulfiller<AnyPointer::Pipeline>>> tailCallPipelineFulfiller;

    // Cancellation state ----------------------------------

    bool receivedFinish = false;
    // True if a `Finish` message has been recevied OR we sent a `Return` with `noFinishNedeed`.
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
        auto& answer = connectionState->answers[answerId];
        answer.callContext = nullptr;
        answer.resultExports = kj::mv(resultExports);

        if (shouldFreePipeline) {
          // We can free the pipeline early, because we know all pipeline calls are invalid (e.g.
          // because there are no caps in the result to receive pipeline requests).
          KJ_ASSERT(resultExports.size() == 0);
          answer.pipeline = nullptr;
        }
      }

      // Also, this is the right time to stop counting the call against the flow limit.
      connectionState->callWordsInFlight -= requestSize;
      connectionState->maybeUnblockFlow();
    }
  };

  // =====================================================================================
  // Message handling

  void maybeUnblockFlow() {
    if (callWordsInFlight < flowLimit) {
      KJ_IF_MAYBE(w, flowWaiter) {
        w->get()->fulfill();
        flowWaiter = nullptr;
      }
    }
  }

  kj::Promise<void> messageLoop() {
    if (!connection.is<Connected>()) {
      return kj::READY_NOW;
    }

    if (callWordsInFlight > flowLimit) {
      auto paf = kj::newPromiseAndFulfiller<void>();
      flowWaiter = kj::mv(paf.fulfiller);
      return paf.promise.then([this]() {
        return messageLoop();
      });
    }

    return canceler.wrap(connection.get<Connected>()->receiveIncomingMessage()).then(
        [this](kj::Maybe<kj::Own<IncomingRpcMessage>>&& message) {
      KJ_IF_MAYBE(m, message) {
        handleMessage(kj::mv(*m));
        return true;
      } else {
        tasks.add(KJ_EXCEPTION(DISCONNECTED, "Peer disconnected."));
        return false;
      }
    }, [this](kj::Exception&& exception) {
      receiveIncomingMessageError = true;
      kj::throwRecoverableException(kj::mv(exception));
      return false;
    }).then([this](bool keepGoing) {
      // No exceptions; continue loop.
      //
      // (We do this in a separate continuation to handle the case where exceptions are
      // disabled.)
      //
      // TODO(perf): We add an evalLater() here so that anything we needed to do in reaction to
      //   the previous message has a chance to complete before the next message is handled. In
      //   particular, without this, I observed an ordering problem: I saw a case where a `Return`
      //   message was followed by a `Resolve` message, but the `PromiseClient` associated with the
      //   `Resolve` had its `resolve()` method invoked _before_ any `PromiseClient`s associated
      //   with pipelined capabilities resolved by the `Return`. This could lead to an
      //   incorrectly-ordered interaction between `PromiseClient`s when they resolve to each
      //   other. This is probably really a bug in the way `Return`s are handled -- apparently,
      //   resolution of `PromiseClient`s based on returned capabilities does not occur in a
      //   depth-first way, when it should. If we could fix that then we can probably remove this
      //   `evalLater()`. However, the `evalLater()` is not that bad and solves the problem...
      if (keepGoing) tasks.add(kj::evalLater([this]() { return messageLoop(); }));
    });
  }

  void handleMessage(kj::Own<IncomingRpcMessage> message) {
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

      default: {
        if (connection.is<Connected>()) {
          auto message = connection.get<Connected>()->newOutgoingMessage(
              firstSegmentSize(reader.totalSize(), messageSizeHint<void>()));
          message->getBody().initAs<rpc::Message>().setUnimplemented(reader);
          message->send();
        }
        break;
      }
    }
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

    VatNetworkBase::Connection& conn = *connection.get<Connected>();
    auto response = conn.newOutgoingMessage(
        messageSizeHint<rpc::Return>() + sizeInWords<rpc::CapDescriptor>() + 32);

    rpc::Return::Builder ret = response->getBody().getAs<rpc::Message>().initReturn();
    ret.setAnswerId(answerId);

    kj::Own<ClientHook> capHook;
    kj::Array<ExportId> resultExports;
    KJ_DEFER(releaseExports(resultExports));  // in case something goes wrong

    // Call the restorer and initialize the answer.
    KJ_IF_MAYBE(exception, kj::runCatchingExceptions([&]() {
      Capability::Client cap = nullptr;

      if (bootstrap.hasDeprecatedObjectId()) {
        KJ_IF_MAYBE(r, restorer) {
          cap = r->baseRestore(bootstrap.getDeprecatedObjectId());
        } else {
          KJ_FAIL_REQUIRE("This vat only supports a bootstrap interface, not the old "
                          "Cap'n-Proto-0.4-style named exports.") { return; }
        }
      } else {
        cap = bootstrapFactory.baseCreateFor(conn.baseGetPeerVatId());
      }

      BuilderCapabilityTable capTable;
      auto payload = ret.initResults();
      capTable.imbue(payload.getContent()).setAs<Capability>(kj::mv(cap));

      auto capTableArray = capTable.getTable();
      KJ_DASSERT(capTableArray.size() == 1);
      kj::Vector<int> fds;
      resultExports = writeDescriptors(capTableArray, payload, fds);
      response->setFds(fds.releaseAsArray());

      // If we're returning a capability that turns out to be an PromiseClient pointing back on
      // this same network, it's important we remove the `PromiseClient` layer and use the inner
      // capability instead. This achieves the same effect that `PostReturnRpcPipeline` does for
      // regular call returns.
      //
      // This single line of code represents two hours of my life.
      capHook = getInnermostClient(*KJ_ASSERT_NONNULL(capTableArray[0]));
    })) {
      fromException(*exception, ret.initException());
      capHook = newBrokenCap(kj::mv(*exception));
    }

    message = nullptr;

    // Add the answer to the answer table for pipelining and send the response.
    auto& answer = answers[answerId];
    KJ_REQUIRE(!answer.active, "questionId is already in use", answerId) {
      return;
    }

    answer.resultExports = kj::mv(resultExports);
    answer.active = true;
    answer.pipeline = kj::Own<PipelineHook>(kj::refcounted<SingleCapPipeline>(kj::mv(capHook)));

    response->send();
  }

  void handleCall(kj::Own<IncomingRpcMessage>&& message, const rpc::Call::Reader& call) {
    kj::Own<ClientHook> capability;

    KJ_IF_MAYBE(t, getMessageTarget(call.getTarget())) {
      capability = kj::mv(*t);
    } else {
      // Exception already reported.
      return;
    }

    bool redirectResults;
    switch (call.getSendResultsTo().which()) {
      case rpc::Call::SendResultsTo::CALLER:
        redirectResults = false;
        break;
      case rpc::Call::SendResultsTo::YOURSELF:
        redirectResults = true;
        break;
      default:
        KJ_FAIL_REQUIRE("Unsupported `Call.sendResultsTo`.") { return; }
    }

    auto payload = call.getParams();
    auto capTableArray = receiveCaps(payload.getCapTable(), message->getAttachedFds());

    AnswerId answerId = call.getQuestionId();

    auto hints = callHintsFromReader(call);

    // Don't honor onlyPromisePipeline if results are redirected, because this situation isn't
    // useful in practice and would be complicated to handle "correctly".
    if (redirectResults) hints.onlyPromisePipeline = false;

    auto context = kj::refcounted<RpcCallContext>(
        *this, answerId, kj::mv(message), kj::mv(capTableArray), payload.getContent(),
        redirectResults, call.getInterfaceId(), call.getMethodId(), hints);

    // No more using `call` after this point, as it now belongs to the context.

    {
      auto& answer = answers[answerId];

      KJ_REQUIRE(!answer.active, "questionId is already in use") {
        return;
      }

      answer.active = true;
      answer.callContext = *context;
    }

    auto promiseAndPipeline = startCall(
        call.getInterfaceId(), call.getMethodId(), kj::mv(capability), context->addRef(), hints);

    // Things may have changed -- in particular if startCall() immediately called
    // context->directTailCall().

    {
      auto& answer = answers[answerId];

      answer.pipeline = kj::mv(promiseAndPipeline.pipeline);

      if (redirectResults) {
        answer.task = promiseAndPipeline.promise.then(
            [context=kj::mv(context)]() mutable {
              return context->consumeRedirectedResponse();
            });
      } else if (hints.onlyPromisePipeline) {
        // The promise is probably fake anyway, so don't bother adding a .then(). We do, however,
        // have to attach `context` to this, since we destroy `task` upon receiving a `Finish`
        // message, and we want `RpcCallContext` to be destroyed no earlier than that.
        answer.task = promiseAndPipeline.promise.attach(kj::mv(context));
      } else {
        // Hack:  Both the success and error continuations need to use the context.  We could
        //   refcount, but both will be destroyed at the same time anyway.
        RpcCallContext& contextRef = *context;

        answer.task = promiseAndPipeline.promise.then(
            [context = kj::mv(context)]() mutable {
              context->sendReturn();
            }, [&contextRef](kj::Exception&& exception) {
              contextRef.sendErrorReturn(kj::mv(exception));
            }).eagerlyEvaluate([&](kj::Exception&& exception) {
              // Handle exceptions that occur in sendReturn()/sendErrorReturn().
              taskFailed(kj::mv(exception));
            });
      }
    }
  }

  ClientHook::VoidPromiseAndPipeline startCall(
      uint64_t interfaceId, uint64_t methodId,
      kj::Own<ClientHook>&& capability, kj::Own<CallContextHook>&& context,
      ClientHook::CallHints hints) {
    return capability->call(interfaceId, methodId, kj::mv(context), hints);
  }

  kj::Maybe<kj::Own<ClientHook>> getMessageTarget(const rpc::MessageTarget::Reader& target) {
    switch (target.which()) {
      case rpc::MessageTarget::IMPORTED_CAP: {
        KJ_IF_MAYBE(exp, exports.find(target.getImportedCap())) {
          return exp->clientHook->addRef();
        } else {
          KJ_FAIL_REQUIRE("Message target is not a current export ID.") {
            return nullptr;
          }
        }
        break;
      }

      case rpc::MessageTarget::PROMISED_ANSWER: {
        auto promisedAnswer = target.getPromisedAnswer();
        kj::Own<PipelineHook> pipeline;

        KJ_IF_MAYBE(answer, answers.find(promisedAnswer.getQuestionId())) {
          if (answer->active) {
            KJ_IF_MAYBE(p, answer->pipeline) {
              pipeline = p->get()->addRef();
            }
          }
        }
        if (pipeline.get() == nullptr) {
          pipeline = newBrokenPipeline(KJ_EXCEPTION(FAILED,
              "Pipeline call on a request that returned no capabilities or was already closed."));
        }

        KJ_IF_MAYBE(ops, toPipelineOps(promisedAnswer.getTransform())) {
          return pipeline->getPipelinedCap(*ops);
        } else {
          // Exception already thrown.
          return nullptr;
        }
      }

      default:
        KJ_FAIL_REQUIRE("Unknown message target type.", target) {
          return nullptr;
        }
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
    if (questions.isHigh(questionId)) {
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

    KJ_IF_MAYBE(question, questions.find(questionId)) {
      KJ_REQUIRE(question->isAwaitingReturn, "Duplicate Return.") { return; }
      question->isAwaitingReturn = false;

      if (ret.getReleaseParamCaps()) {
        exportsToRelease = kj::mv(question->paramExports);
      } else {
        question->paramExports = nullptr;
      }

      if (ret.getNoFinishNeeded()) {
        question->skipFinish = true;
      }

      KJ_IF_MAYBE(questionRef, question->selfRef) {
        switch (ret.which()) {
          case rpc::Return::RESULTS: {
            KJ_REQUIRE(!question->isTailCall,
                "Tail call `Return` must set `resultsSentElsewhere`, not `results`.") {
              return;
            }

            auto payload = ret.getResults();
            auto capTableArray = receiveCaps(payload.getCapTable(), message->getAttachedFds());
            questionRef->fulfill(kj::refcounted<RpcResponseImpl>(
                *this, kj::addRef(*questionRef), kj::mv(message),
                kj::mv(capTableArray), payload.getContent()));
            break;
          }

          case rpc::Return::EXCEPTION:
            KJ_REQUIRE(!question->isTailCall,
                "Tail call `Return` must set `resultsSentElsewhere`, not `exception`.") {
              return;
            }

            questionRef->reject(toException(ret.getException()));
            break;

          case rpc::Return::CANCELED:
            KJ_FAIL_REQUIRE("Return message falsely claims call was canceled.") { return; }
            break;

          case rpc::Return::RESULTS_SENT_ELSEWHERE:
            KJ_REQUIRE(question->isTailCall,
                "`Return` had `resultsSentElsewhere` but this was not a tail call.") {
              return;
            }

            // Tail calls are fulfilled with a null pointer.
            questionRef->fulfill(kj::Own<RpcResponse>());
            break;

          case rpc::Return::TAKE_FROM_OTHER_QUESTION:
            KJ_IF_MAYBE(answer, answers.find(ret.getTakeFromOtherQuestion())) {
              KJ_IF_MAYBE(response, answer->task.tryGet<Answer::Redirected>()) {
                questionRef->fulfill(kj::mv(*response));
                answer->task = Answer::Finished();

                KJ_IF_MAYBE(context, answer->callContext) {
                  // Send the `Return` message  for the call of which we're taking ownership, so
                  // that the peer knows it can now tear down the call state.
                  context->sendRedirectReturn();
                }
              } else {
                KJ_FAIL_REQUIRE("`Return.takeFromOtherQuestion` referenced a call that did not "
                                "use `sendResultsTo.yourself`.") { return; }
              }
            } else {
              KJ_FAIL_REQUIRE("`Return.takeFromOtherQuestion` had invalid answer ID.") { return; }
            }

            break;

          default:
            KJ_FAIL_REQUIRE("Unknown 'Return' type.") { return; }
        }
      } else {
        // This is a response to a question that we canceled earlier.

        if (ret.isTakeFromOtherQuestion()) {
          // This turned out to be a tail call back to us! We now take ownership of the tail call.
          // Since the caller canceled, we need to cancel out the tail call, if it still exists.

          KJ_IF_MAYBE(answer, answers.find(ret.getTakeFromOtherQuestion())) {
            // Indeed, it does still exist.

            // Throw away the result promise.
            promiseToRelease = kj::mv(answer->task);

            KJ_IF_MAYBE(context, answer->callContext) {
              // Send the `Return` message  for the call of which we're taking ownership, so
              // that the peer knows it can now tear down the call state.
              context->sendRedirectReturn();
            }
          }
        }

        // Looks like this question was canceled earlier, so `Finish` was already sent, with
        // `releaseResultCaps` set true so that we don't have to release them here.  We can go
        // ahead and delete it from the table.
        questions.erase(ret.getAnswerId(), *question);
      }

    } else {
      KJ_FAIL_REQUIRE("Invalid question ID in Return message.") { return; }
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

    KJ_IF_MAYBE(answer, answers.find(finish.getQuestionId())) {
      if (!answer->active) {
        // Treat the same as if the answer wasn't in the table; see comment below.
        return;
      }

      if (finish.getReleaseResultCaps()) {
        exportsToRelease = kj::mv(answer->resultExports);
      } else {
        answer->resultExports = nullptr;
      }

      pipelineToRelease = kj::mv(answer->pipeline);

      KJ_IF_MAYBE(context, answer->callContext) {
        // Destroying answer->task will probably destroy the call context, but we can't prove that
        // since it's refcounted. Instead, inform the call context that it is now its job to
        // clean up the answer table. Then, cancel the task.
        promiseToRelease = kj::mv(answer->task);
        answer->task = Answer::Finished();
        context->finish();
      } else {
        // The call context is already gone so we can tear down the Answer here.
        answerToRelease = answers.erase(finish.getQuestionId());
      }
    } else {
      // The `Finish` message targets a qusetion ID that isn't present in our answer table.
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
      KJ_IF_MAYBE(task, promiseToRelease) {
        KJ_IF_MAYBE(running, task->tryGet<Answer::Running>()) {
          tasks.add(kj::evalLast([running = kj::mv(*running)]() {
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
        KJ_IF_MAYBE(cap, receiveCap(resolve.getCap(), message->getAttachedFds())) {
          replacement = kj::mv(*cap);
        } else {
          KJ_FAIL_REQUIRE("'Resolve' contained 'CapDescriptor.none'.") { return; }
        }
        break;

      case rpc::Resolve::EXCEPTION:
        // We can't set `replacement` to a new broken cap here because this will confuse
        // PromiseClient::Resolve() into thinking that the remote promise resolved to a local
        // capability and therefore a Disembargo is needed. We must actually reject the promise.
        exception = toException(resolve.getException());
        break;

      default:
        KJ_FAIL_REQUIRE("Unknown 'Resolve' type.") { return; }
    }

    // If the import is on the table, fulfill it.
    KJ_IF_MAYBE(import, imports.find(resolve.getPromiseId())) {
      KJ_IF_MAYBE(fulfiller, import->promiseFulfiller) {
        // OK, this is in fact an unfulfilled promise!
        KJ_IF_MAYBE(e, exception) {
          fulfiller->get()->reject(kj::mv(*e));
        } else {
          fulfiller->get()->fulfill(kj::mv(replacement));
        }
      } else if (import->importClient != nullptr) {
        // It appears this is a valid entry on the import table, but was not expected to be a
        // promise.
        KJ_FAIL_REQUIRE("Got 'Resolve' for a non-promise import.") { break; }
      }
    }
  }

  void handleRelease(const rpc::Release::Reader& release) {
    releaseExport(release.getId(), release.getReferenceCount());
  }

  void releaseExport(ExportId id, uint refcount) {
    KJ_IF_MAYBE(exp, exports.find(id)) {
      KJ_REQUIRE(refcount <= exp->refcount, "Tried to drop export's refcount below zero.") {
        return;
      }

      exp->refcount -= refcount;
      if (exp->refcount == 0) {
        exportsByCap.erase(exp->clientHook);
        exports.erase(id, *exp);
      }
    } else {
      KJ_FAIL_REQUIRE("Tried to release invalid export ID.") {
        return;
      }
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
        kj::Own<ClientHook> target;

        KJ_IF_MAYBE(t, getMessageTarget(disembargo.getTarget())) {
          target = kj::mv(*t);
        } else {
          // Exception already reported.
          return;
        }

        EmbargoId embargoId = context.getSenderLoopback();

        // It's possible that `target` is a promise capability that hasn't resolved yet, in which
        // case we must wait for the resolution. In particular this can happen in the case where
        // we have Alice -> Bob -> Carol, Alice makes a call that proxies from Bob to Carol, and
        // Carol returns a capability from this call that points all the way back though Bob to
        // Alice. When this return capability passes through Bob, Bob will resolve the previous
        // promise-pipeline capability to it. However, Bob has to send a Disembargo to Carol before
        // completing this resolution. In the meantime, though, Bob returns the final repsonse to
        // Alice. Alice then *also* sends a Disembargo to Bob. The Alice -> Bob Disembargo might
        // arrive at Bob before the Bob -> Carol Disembargo has resolved, in which case the
        // Disembargo is delivered to a promise capability.
        auto promise = target->whenResolved()
            .then([]() {
          // We also need to insert an evalLast() here to make sure that any pending calls towards
          // this cap have had time to find their way through the event loop.
          return kj::evalLast([]() {});
        });

        tasks.add(promise.then([this, embargoId, target = kj::mv(target)]() mutable {
          for (;;) {
            KJ_IF_MAYBE(r, target->getResolved()) {
              target = r->addRef();
            } else {
              break;
            }
          }

          KJ_REQUIRE(target->getBrand() == this,
                    "'Disembargo' of type 'senderLoopback' sent to an object that does not point "
                    "back to the sender.") {
            return;
          }

          if (!connection.is<Connected>()) {
            return;
          }

          RpcClient& downcasted = kj::downcast<RpcClient>(*target);

          auto message = connection.get<Connected>()->newOutgoingMessage(
              messageSizeHint<rpc::Disembargo>() + MESSAGE_TARGET_SIZE_HINT);
          auto builder = message->getBody().initAs<rpc::Message>().initDisembargo();

          {
            auto redirect = downcasted.writeTarget(builder.initTarget());

            // Disembargoes should only be sent to capabilities that were previously the subject of
            // a `Resolve` message.  But `writeTarget` only ever returns non-null when called on
            // a PromiseClient.  The code which sends `Resolve` and `Return` should have replaced
            // any promise with a direct node in order to solve the Tribble 4-way race condition.
            // See the documentation of Disembargo in rpc.capnp for more.
            KJ_REQUIRE(redirect == nullptr,
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
        KJ_IF_MAYBE(embargo, embargoes.find(context.getReceiverLoopback())) {
          KJ_ASSERT_NONNULL(embargo->fulfiller)->fulfill();
          embargoes.erase(context.getReceiverLoopback(), *embargo);
        } else {
          KJ_FAIL_REQUIRE("Invalid embargo ID in 'Disembargo.context.receiverLoopback'.") {
            return;
          }
        }
        break;
      }

      default:
        KJ_FAIL_REQUIRE("Unimplemented Disembargo type.", disembargo) { return; }
    }
  }

  // ---------------------------------------------------------------------------
  // Level 2
};

}  // namespace

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
  Impl(VatNetworkBase& network, SturdyRefRestorerBase& restorer)
      : network(network), bootstrapFactory(*this), restorer(restorer), tasks(*this) {
    acceptLoopPromise = acceptLoop().eagerlyEvaluate([](kj::Exception&& e) { KJ_LOG(ERROR, e); });
  }

  ~Impl() noexcept(false) {
    unwindDetector.catchExceptionsIfUnwinding([&]() {
      // std::unordered_map doesn't like it when elements' destructors throw, so carefully
      // disassemble it.
      if (!connections.empty()) {
        kj::Vector<kj::Own<RpcConnectionState>> deleteMe(connections.size());
        kj::Exception shutdownException = KJ_EXCEPTION(DISCONNECTED, "RpcSystem was destroyed.");
        for (auto& entry: connections) {
          entry.second->disconnect(kj::cp(shutdownException));
          deleteMe.add(kj::mv(entry.second));
        }
      }
    });
  }

  Capability::Client bootstrap(AnyStruct::Reader vatId) {
    // For now we delegate to restore() since it's equivalent, but eventually we'll remove restore()
    // and implement bootstrap() directly.
    return restore(vatId, AnyPointer::Reader());
  }

  Capability::Client restore(AnyStruct::Reader vatId, AnyPointer::Reader objectId) {
    KJ_IF_MAYBE(connection, network.baseConnect(vatId)) {
      auto& state = getConnectionState(kj::mv(*connection));
      return Capability::Client(state.restore(objectId));
    } else if (objectId.isNull()) {
      // Turns out `vatId` refers to ourselves, so we can also pass it as the client ID for
      // baseCreateFor().
      return bootstrapFactory.baseCreateFor(vatId);
    } else KJ_IF_MAYBE(r, restorer) {
      return r->baseRestore(objectId);
    } else {
      return Capability::Client(newBrokenCap(
          "This vat only supports a bootstrap interface, not the old Cap'n-Proto-0.4-style "
          "named exports."));
    }
  }

  void setFlowLimit(size_t words) {
    flowLimit = words;

    for (auto& conn: connections) {
      conn.second->setFlowLimit(words);
    }
  }

  void setTraceEncoder(kj::Function<kj::String(const kj::Exception&)> func) {
    traceEncoder = kj::mv(func);
  }

  kj::Promise<void> run() { return kj::mv(acceptLoopPromise); }

private:
  VatNetworkBase& network;
  kj::Maybe<Capability::Client> bootstrapInterface;
  BootstrapFactoryBase& bootstrapFactory;
  kj::Maybe<SturdyRefRestorerBase&> restorer;
  size_t flowLimit = kj::maxValue;
  kj::Maybe<kj::Function<kj::String(const kj::Exception&)>> traceEncoder;
  kj::Promise<void> acceptLoopPromise = nullptr;
  kj::TaskSet tasks;

  typedef std::unordered_map<VatNetworkBase::Connection*, kj::Own<RpcConnectionState>>
      ConnectionMap;
  ConnectionMap connections;

  kj::UnwindDetector unwindDetector;

  RpcConnectionState& getConnectionState(kj::Own<VatNetworkBase::Connection>&& connection) {
    auto iter = connections.find(connection);
    if (iter == connections.end()) {
      VatNetworkBase::Connection* connectionPtr = connection;
      auto onDisconnect = kj::newPromiseAndFulfiller<RpcConnectionState::DisconnectInfo>();
      tasks.add(onDisconnect.promise
          .then([this,connectionPtr](RpcConnectionState::DisconnectInfo info) {
        connections.erase(connectionPtr);
        tasks.add(kj::mv(info.shutdownPromise));
      }));
      auto newState = kj::refcounted<RpcConnectionState>(
          bootstrapFactory, restorer, kj::mv(connection),
          kj::mv(onDisconnect.fulfiller), flowLimit, traceEncoder);
      RpcConnectionState& result = *newState;
      connections.insert(std::make_pair(connectionPtr, kj::mv(newState)));
      return result;
    } else {
      return *iter->second;
    }
  }

  kj::Promise<void> acceptLoop() {
    return network.baseAccept().then(
        [this](kj::Own<VatNetworkBase::Connection>&& connection) {
      getConnectionState(kj::mv(connection));
      return acceptLoop();
    });
  }

  Capability::Client baseCreateFor(AnyStruct::Reader clientId) override {
    // Implements BootstrapFactory::baseCreateFor() in terms of `bootstrapInterface` or `restorer`,
    // for use when we were given one of those instead of an actual `bootstrapFactory`.

    KJ_IF_MAYBE(cap, bootstrapInterface) {
      return *cap;
    } else KJ_IF_MAYBE(r, restorer) {
      return r->baseRestore(AnyPointer::Reader());
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
RpcSystemBase::RpcSystemBase(VatNetworkBase& network, SturdyRefRestorerBase& restorer)
    : impl(kj::heap<Impl>(network, restorer)) {}
RpcSystemBase::RpcSystemBase(RpcSystemBase&& other) noexcept = default;
RpcSystemBase::~RpcSystemBase() noexcept(false) {}

Capability::Client RpcSystemBase::baseBootstrap(AnyStruct::Reader vatId) {
  return impl->bootstrap(vatId);
}

Capability::Client RpcSystemBase::baseRestore(
    AnyStruct::Reader hostId, AnyPointer::Reader objectId) {
  return impl->restore(hostId, objectId);
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

          KJ_IF_MAYBE(f, emptyFulfiller) {
            if (inFlight == 0) {
              f->get()->fulfill(tasks.onEmpty());
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
    KJ_IF_MAYBE(q, state.tryGet<Running>()) {
      if (!q->empty()) {
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
