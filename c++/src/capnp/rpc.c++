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
  uint64_t sizeHint = size.wordCount + size.capCount * CAP_DESCRIPTOR_SIZE_HINT;
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
  return kj::Exception(static_cast<kj::Exception::Type>(exception.getType()),
      "(remote)", 0, kj::str("remote exception: ", exception.getReason()));
}

void fromException(const kj::Exception& exception, rpc::Exception::Builder builder) {
  // TODO(someday):  Indicate the remote server name as part of the stack trace.  Maybe even
  //   transmit stack traces?

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

  if (exception.getType() == kj::Exception::Type::FAILED &&
      !exception.getDescription().startsWith("remote exception:")) {
    KJ_LOG(INFO, "returning failure over rpc", exception);
  }
}

uint exceptionSizeHint(const kj::Exception& exception) {
  return sizeInWords<rpc::Exception>() + exception.getDescription().size() / sizeof(word) + 1;
}

// =======================================================================================

template <typename Id, typename T>
class ExportTable {
  // Table mapping integers to T, where the integers are chosen locally.

public:
  kj::Maybe<T&> find(Id id) {
    if (id < slots.size() && slots[id] != nullptr) {
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
    KJ_DREQUIRE(&entry == &slots[id]);
    T toRelease = kj::mv(slots[id]);
    slots[id] = T();
    freeIds.push(id);
    return toRelease;
  }

  T& next(Id& id) {
    if (freeIds.empty()) {
      id = slots.size();
      return slots.add();
    } else {
      id = freeIds.top();
      freeIds.pop();
      return slots[id];
    }
  }

  template <typename Func>
  void forEach(Func&& func) {
    for (Id i = 0; i < slots.size(); i++) {
      if (slots[i] != nullptr) {
        func(i, slots[i]);
      }
    }
  }

private:
  kj::Vector<T> slots;
  std::priority_queue<Id, std::vector<Id>, std::greater<Id>> freeIds;
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
                     kj::Maybe<RealmGateway<>::Client> gateway,
                     kj::Maybe<SturdyRefRestorerBase&> restorer,
                     kj::Own<VatNetworkBase::Connection>&& connectionParam,
                     kj::Own<kj::PromiseFulfiller<DisconnectInfo>>&& disconnectFulfiller,
                     size_t flowLimit)
      : bootstrapFactory(bootstrapFactory), gateway(kj::mv(gateway)),
        restorer(restorer), disconnectFulfiller(kj::mv(disconnectFulfiller)), flowLimit(flowLimit),
        tasks(*this) {
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
    if (!connection.is<Connected>()) {
      // Already disconnected.
      return;
    }

    kj::Exception networkException(kj::Exception::Type::DISCONNECTED,
        exception.getFile(), exception.getLine(), kj::heapString(exception.getDescription()));

    KJ_IF_MAYBE(newException, kj::runCatchingExceptions([&]() {
      // Carefully pull all the objects out of the tables prior to releasing them because their
      // destructors could come back and mess with the tables.
      kj::Vector<kj::Own<PipelineHook>> pipelinesToRelease;
      kj::Vector<kj::Own<ClientHook>> clientsToRelease;
      kj::Vector<kj::Promise<kj::Own<RpcResponse>>> tailCallsToRelease;
      kj::Vector<kj::Promise<void>> resolveOpsToRelease;

      // All current questions complete with exceptions.
      questions.forEach([&](QuestionId id, Question& question) {
        KJ_IF_MAYBE(questionRef, question.selfRef) {
          // QuestionRef still present.
          questionRef->reject(kj::cp(networkException));
        }
      });

      answers.forEach([&](AnswerId id, Answer& answer) {
        KJ_IF_MAYBE(p, answer.pipeline) {
          pipelinesToRelease.add(kj::mv(*p));
        }

        KJ_IF_MAYBE(promise, answer.redirectedResults) {
          tailCallsToRelease.add(kj::mv(*promise));
        }

        KJ_IF_MAYBE(context, answer.callContext) {
          context->requestCancel();
        }
      });

      exports.forEach([&](ExportId id, Export& exp) {
        clientsToRelease.add(kj::mv(exp.clientHook));
        resolveOpsToRelease.add(kj::mv(exp.resolveOp));
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
      auto message = connection.get<Connected>()->newOutgoingMessage(
          messageSizeHint<void>() + exceptionSizeHint(exception));
      fromException(exception, message->getBody().getAs<rpc::Message>().initAbort());
      message->send();
    });

    // Indicate disconnect.
    auto shutdownPromise = connection.get<Connected>()->shutdown()
        .attach(kj::mv(connection.get<Connected>()))
        .then([]() -> kj::Promise<void> { return kj::READY_NOW; },
              [](kj::Exception&& e) -> kj::Promise<void> {
          // Don't report disconnects as an error.
          if (e.getType() != kj::Exception::Type::DISCONNECTED) {
            return kj::mv(e);
          }
          return kj::READY_NOW;
        });
    disconnectFulfiller->fulfill(DisconnectInfo { kj::mv(shutdownPromise) });
    connection.init<Disconnected>(kj::mv(networkException));
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

    kj::Maybe<kj::Promise<kj::Own<RpcResponse>>> redirectedResults;
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

    kj::Promise<void> resolveOp = nullptr;
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
  kj::Maybe<RealmGateway<>::Client> gateway;
  kj::Maybe<SturdyRefRestorerBase&> restorer;

  typedef kj::Own<VatNetworkBase::Connection> Connected;
  typedef kj::Exception Disconnected;
  kj::OneOf<Connected, Disconnected> connection;
  // Once the connection has failed, we drop it and replace it with an exception, which will be
  // thrown from all further calls.

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

  kj::TaskSet tasks;

  // =====================================================================================
  // ClientHook implementations

  class RpcClient: public ClientHook, public kj::Refcounted {
  public:
    RpcClient(RpcConnectionState& connectionState)
        : connectionState(kj::addRef(connectionState)) {}

    virtual kj::Maybe<ExportId> writeDescriptor(rpc::CapDescriptor::Builder descriptor) = 0;
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

    // implements ClientHook -----------------------------------------

    Request<AnyPointer, AnyPointer> newCall(
        uint64_t interfaceId, uint16_t methodId, kj::Maybe<MessageSize> sizeHint) override {
      if (interfaceId == typeId<Persistent<>>() && methodId == 0) {
        KJ_IF_MAYBE(g, connectionState->gateway) {
          // Wait, this is a call to Persistent.save() and we need to translate it through our
          // gateway.
          //
          // We pull a neat trick here: We actually end up returning a RequestHook for an import
          // request on the gateway cap, but with the "root" of the request actually pointing
          // to the "params" field of the real request.

          sizeHint = sizeHint.map([](MessageSize hint) {
            ++hint.capCount;
            hint.wordCount += sizeInWords<RealmGateway<>::ImportParams>();
            return hint;
          });

          auto request = g->importRequest(sizeHint);
          request.setCap(Persistent<>::Client(kj::refcounted<NoInterceptClient>(*this)));

          // Awkwardly, request.initParams() would return a SaveParams struct, but to construct
          // the Request<AnyPointer, AnyPointer> to return we need an AnyPointer::Builder, and you
          // can't go backwards from a struct builder to an AnyPointer builder. So instead we
          // manually get at the pointer by converting the outer request to AnyStruct and then
          // pulling the pointer from the pointer section.
          auto pointers = toAny(request).getPointerSection();
          KJ_ASSERT(pointers.size() >= 2);
          auto paramsPtr = pointers[1];
          KJ_ASSERT(paramsPtr.isNull());

          return Request<AnyPointer, AnyPointer>(paramsPtr, RequestHook::from(kj::mv(request)));
        }
      }

      return newCallNoIntercept(interfaceId, methodId, sizeHint);
    }

    Request<AnyPointer, AnyPointer> newCallNoIntercept(
        uint64_t interfaceId, uint16_t methodId, kj::Maybe<MessageSize> sizeHint) {
      if (!connectionState->connection.is<Connected>()) {
        return newBrokenRequest(kj::cp(connectionState->connection.get<Disconnected>()), sizeHint);
      }

      auto request = kj::heap<RpcRequest>(
          *connectionState, *connectionState->connection.get<Connected>(),
          sizeHint, kj::addRef(*this));
      auto callBuilder = request->getCall();

      callBuilder.setInterfaceId(interfaceId);
      callBuilder.setMethodId(methodId);

      auto root = request->getRoot();
      return Request<AnyPointer, AnyPointer>(root, kj::mv(request));
    }

    VoidPromiseAndPipeline call(uint64_t interfaceId, uint16_t methodId,
                                kj::Own<CallContextHook>&& context) override {
      if (interfaceId == typeId<Persistent<>>() && methodId == 0) {
        KJ_IF_MAYBE(g, connectionState->gateway) {
          // Wait, this is a call to Persistent.save() and we need to translate it through our
          // gateway.
          auto params = context->getParams().getAs<Persistent<>::SaveParams>();

          auto requestSize = params.totalSize();
          ++requestSize.capCount;
          requestSize.wordCount += sizeInWords<RealmGateway<>::ImportParams>();

          auto request = g->importRequest(requestSize);
          request.setCap(Persistent<>::Client(kj::refcounted<NoInterceptClient>(*this)));
          request.setParams(params);

          context->allowCancellation();
          context->releaseParams();
          return context->directTailCall(RequestHook::from(kj::mv(request)));
        }
      }

      return callNoIntercept(interfaceId, methodId, kj::mv(context));
    }

    VoidPromiseAndPipeline callNoIntercept(uint64_t interfaceId, uint16_t methodId,
                                           kj::Own<CallContextHook>&& context) {
      // Implement call() by copying params and results messages.

      auto params = context->getParams();
      auto request = newCallNoIntercept(interfaceId, methodId, params.targetSize());

      request.set(params);
      context->releaseParams();

      // We can and should propagate cancellation.
      context->allowCancellation();

      return context->directTailCall(RequestHook::from(kj::mv(request)));
    }

    kj::Own<ClientHook> addRef() override {
      return kj::addRef(*this);
    }
    const void* getBrand() override {
      return connectionState.get();
    }

    kj::Own<RpcConnectionState> connectionState;
  };

  class ImportClient final: public RpcClient {
    // A ClientHook that wraps an entry in the import table.

  public:
    ImportClient(RpcConnectionState& connectionState, ImportId importId)
        : RpcClient(connectionState), importId(importId) {}

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

    void addRemoteRef() {
      // Add a new RemoteRef and return a new ref to this client representing it.
      ++remoteRefcount;
    }

    kj::Maybe<ExportId> writeDescriptor(rpc::CapDescriptor::Builder descriptor) override {
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

  private:
    ImportId importId;

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

   kj::Maybe<ExportId> writeDescriptor(rpc::CapDescriptor::Builder descriptor) override {
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

  private:
    kj::Own<QuestionRef> questionRef;
    kj::Array<PipelineOp> ops;
  };

  class PromiseClient final: public RpcClient {
    // A ClientHook that initially wraps one client (in practice, an ImportClient or a
    // PipelineClient) and then, later on, redirects to some other client.

  public:
    PromiseClient(RpcConnectionState& connectionState,
                  kj::Own<ClientHook> initial,
                  kj::Promise<kj::Own<ClientHook>> eventual,
                  kj::Maybe<ImportId> importId)
        : RpcClient(connectionState),
          isResolved(false),
          cap(kj::mv(initial)),
          importId(importId),
          fork(eventual.fork()),
          resolveSelfPromise(fork.addBranch().then(
              [this](kj::Own<ClientHook>&& resolution) {
                resolve(kj::mv(resolution), false);
              }, [this](kj::Exception&& exception) {
                resolve(newBrokenCap(kj::mv(exception)), true);
              }).eagerlyEvaluate([&](kj::Exception&& e) {
                // Make any exceptions thrown from resolve() go to the connection's TaskSet which
                // will cause the connection to be terminated.
                connectionState.tasks.add(kj::mv(e));
              })) {
      // Create a client that starts out forwarding all calls to `initial` but, once `eventual`
      // resolves, will forward there instead.  In addition, `whenMoreResolved()` will return a fork
      // of `eventual`.  Note that this means the application could hold on to `eventual` even after
      // the `PromiseClient` is destroyed; `eventual` must therefore make sure to hold references to
      // anything that needs to stay alive in order to resolve it correctly (such as making sure the
      // import ID is not released).
    }

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

    kj::Maybe<ExportId> writeDescriptor(rpc::CapDescriptor::Builder descriptor) override {
      receivedCall = true;
      return connectionState->writeDescriptor(*cap, descriptor);
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

    // implements ClientHook -----------------------------------------

    Request<AnyPointer, AnyPointer> newCall(
        uint64_t interfaceId, uint16_t methodId, kj::Maybe<MessageSize> sizeHint) override {
      if (!isResolved && interfaceId == typeId<Persistent<>>() && methodId == 0 &&
          connectionState->gateway != nullptr) {
        // This is a call to Persistent.save(), and we're not resolved yet, and the underlying
        // remote capability will perform a gateway translation. This isn't right if the promise
        // ultimately resolves to a local capability. Instead, we'll need to queue the call until
        // the promise resolves.
        return newLocalPromiseClient(fork.addBranch())
            ->newCall(interfaceId, methodId, sizeHint);
      }

      receivedCall = true;
      return cap->newCall(interfaceId, methodId, sizeHint);
    }

    VoidPromiseAndPipeline call(uint64_t interfaceId, uint16_t methodId,
                                kj::Own<CallContextHook>&& context) override {
      if (!isResolved && interfaceId == typeId<Persistent<>>() && methodId == 0 &&
          connectionState->gateway != nullptr) {
        // This is a call to Persistent.save(), and we're not resolved yet, and the underlying
        // remote capability will perform a gateway translation. This isn't right if the promise
        // ultimately resolves to a local capability. Instead, we'll need to queue the call until
        // the promise resolves.

        auto vpapPromises = fork.addBranch().then(kj::mvCapture(context,
            [interfaceId,methodId](kj::Own<CallContextHook>&& context,
                                   kj::Own<ClientHook> resolvedCap) {
          auto vpap = resolvedCap->call(interfaceId, methodId, kj::mv(context));
          return kj::tuple(kj::mv(vpap.promise), kj::mv(vpap.pipeline));
        })).split();

        return {
          kj::mv(kj::get<0>(vpapPromises)),
          newLocalPromisePipeline(kj::mv(kj::get<1>(vpapPromises))),
        };
      }

      receivedCall = true;
      return cap->call(interfaceId, methodId, kj::mv(context));
    }

    kj::Maybe<ClientHook&> getResolved() override {
      if (isResolved) {
        return *cap;
      } else {
        return nullptr;
      }
    }

    kj::Maybe<kj::Promise<kj::Own<ClientHook>>> whenMoreResolved() override {
      return fork.addBranch();
    }

  private:
    bool isResolved;
    kj::Own<ClientHook> cap;

    kj::Maybe<ImportId> importId;
    kj::ForkedPromise<kj::Own<ClientHook>> fork;

    // Keep this last, because the continuation uses *this, so it should be destroyed first to
    // ensure the continuation is not still running.
    kj::Promise<void> resolveSelfPromise;

    bool receivedCall = false;

    void resolve(kj::Own<ClientHook> replacement, bool isError) {
      const void* replacementBrand = replacement->getBrand();
      if (replacementBrand != connectionState.get() &&
          replacementBrand != &ClientHook::NULL_CAPABILITY_BRAND &&
          receivedCall && !isError && connectionState->connection.is<Connected>()) {
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
        auto embargoPromise = paf.promise.then(
            kj::mvCapture(replacement, [](kj::Own<ClientHook>&& replacement) {
              return kj::mv(replacement);
            }));

        // We need to queue up calls in the meantime, so we'll resolve ourselves to a local promise
        // client instead.
        replacement = newLocalPromiseClient(kj::mv(embargoPromise));

        // Send the `Disembargo`.
        message->send();
      }

      cap = kj::mv(replacement);
      isResolved = true;
    }
  };

  class NoInterceptClient final: public RpcClient {
    // A wrapper around an RpcClient which bypasses special handling of "save" requests. When we
    // intercept a "save" request and invoke a RealmGateway, we give it a version of the capability
    // with intercepting disabled, since usually the first thing the RealmGateway will do is turn
    // around and call save() again.
    //
    // This is admittedly sort of backwards: the interception of "save" ought to be the part
    // implemented by a wrapper. However, that would require placing a wrapper around every
    // RpcClient we create whereas NoInterceptClient only needs to be injected after a save()
    // request occurs and is intercepted.

  public:
    NoInterceptClient(RpcClient& inner)
        : RpcClient(*inner.connectionState),
          inner(kj::addRef(inner)) {}

    kj::Maybe<ExportId> writeDescriptor(rpc::CapDescriptor::Builder descriptor) override {
      return inner->writeDescriptor(descriptor);
    }

    kj::Maybe<kj::Own<ClientHook>> writeTarget(rpc::MessageTarget::Builder target) override {
      return inner->writeTarget(target);
    }

    kj::Own<ClientHook> getInnermostClient() override {
      return inner->getInnermostClient();
    }

    Request<AnyPointer, AnyPointer> newCall(
        uint64_t interfaceId, uint16_t methodId, kj::Maybe<MessageSize> sizeHint) override {
      return inner->newCallNoIntercept(interfaceId, methodId, sizeHint);
    }
    VoidPromiseAndPipeline call(uint64_t interfaceId, uint16_t methodId,
                                kj::Own<CallContextHook>&& context) override {
      return inner->callNoIntercept(interfaceId, methodId, kj::mv(context));
    }

    kj::Maybe<ClientHook&> getResolved() override {
      return nullptr;
    }

    kj::Maybe<kj::Promise<kj::Own<ClientHook>>> whenMoreResolved() override {
      return nullptr;
    }

  private:
    kj::Own<RpcClient> inner;
  };

  kj::Maybe<ExportId> writeDescriptor(ClientHook& cap, rpc::CapDescriptor::Builder descriptor) {
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

    if (inner->getBrand() == this) {
      return kj::downcast<RpcClient>(*inner).writeDescriptor(descriptor);
    } else {
      auto iter = exportsByCap.find(inner);
      if (iter != exportsByCap.end()) {
        // We've already seen and exported this capability before.  Just up the refcount.
        auto& exp = KJ_ASSERT_NONNULL(exports.find(iter->second));
        ++exp.refcount;
        descriptor.setSenderHosted(iter->second);
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
                                       rpc::Payload::Builder payload) {
    auto capTableBuilder = payload.initCapTable(capTable.size());
    kj::Vector<ExportId> exports(capTable.size());
    for (uint i: kj::indices(capTable)) {
      KJ_IF_MAYBE(cap, capTable[i]) {
        KJ_IF_MAYBE(exportId, writeDescriptor(**cap, capTableBuilder[i])) {
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
      writeDescriptor(*exp.clientHook, resolve.initCap());
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

  // =====================================================================================
  // Interpreting CapDescriptor

  kj::Own<ClientHook> import(ImportId importId, bool isPromise) {
    // Receive a new import.

    auto& import = imports[importId];
    kj::Own<ImportClient> importClient;

    // Create the ImportClient, or if one already exists, use it.
    KJ_IF_MAYBE(c, import.importClient) {
      importClient = kj::addRef(*c);
    } else {
      importClient = kj::refcounted<ImportClient>(*this, importId);
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

  kj::Maybe<kj::Own<ClientHook>> receiveCap(rpc::CapDescriptor::Reader descriptor) {
    switch (descriptor.which()) {
      case rpc::CapDescriptor::NONE:
        return nullptr;

      case rpc::CapDescriptor::SENDER_HOSTED:
        return import(descriptor.getSenderHosted(), false);
      case rpc::CapDescriptor::SENDER_PROMISE:
        return import(descriptor.getSenderPromise(), true);

      case rpc::CapDescriptor::RECEIVER_HOSTED:
        KJ_IF_MAYBE(exp, exports.find(descriptor.getReceiverHosted())) {
          return exp->clientHook->addRef();
        } else {
          return newBrokenCap("invalid 'receiverHosted' export ID");
        }

      case rpc::CapDescriptor::RECEIVER_ANSWER: {
        auto promisedAnswer = descriptor.getReceiverAnswer();

        KJ_IF_MAYBE(answer, answers.find(promisedAnswer.getQuestionId())) {
          if (answer->active) {
            KJ_IF_MAYBE(pipeline, answer->pipeline) {
              KJ_IF_MAYBE(ops, toPipelineOps(promisedAnswer.getTransform())) {
                return pipeline->get()->getPipelinedCap(*ops);
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
        return import(descriptor.getThirdPartyHosted().getVineId(), false);

      default:
        KJ_FAIL_REQUIRE("unknown CapDescriptor type") { break; }
        return newBrokenCap("unknown CapDescriptor type");
    }
  }

  kj::Array<kj::Maybe<kj::Own<ClientHook>>> receiveCaps(List<rpc::CapDescriptor>::Reader capTable) {
    auto result = kj::heapArrayBuilder<kj::Maybe<kj::Own<ClientHook>>>(capTable.size());
    for (auto cap: capTable) {
      result.add(receiveCap(cap));
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
        kj::Own<kj::PromiseFulfiller<kj::Promise<kj::Own<RpcResponse>>>> fulfiller)
        : connectionState(kj::addRef(connectionState)), id(id), fulfiller(kj::mv(fulfiller)) {}

    ~QuestionRef() {
      unwindDetector.catchExceptionsIfUnwinding([&]() {
        auto& question = KJ_ASSERT_NONNULL(
            connectionState->questions.find(id), "Question ID no longer on table?");

        // Send the "Finish" message (if the connection is not already broken).
        if (connectionState->connection.is<Connected>() && !question.skipFinish) {
          auto message = connectionState->connection.get<Connected>()->newOutgoingMessage(
              messageSizeHint<rpc::Finish>());
          auto builder = message->getBody().getAs<rpc::Message>().initFinish();
          builder.setQuestionId(id);
          // If we're still awaiting a return, then this request is being canceled, and we're going
          // to ignore any capabilities in the return message, so set releaseResultCaps true. If we
          // already received the return, then we've already built local proxies for the caps and
          // will send Release messages when those are destroyed.
          builder.setReleaseResultCaps(question.isAwaitingReturn);
          message->send();
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
      });
    }

    inline QuestionId getId() const { return id; }

    void fulfill(kj::Own<RpcResponse>&& response) {
      fulfiller->fulfill(kj::mv(response));
    }

    void fulfill(kj::Promise<kj::Own<RpcResponse>>&& promise) {
      fulfiller->fulfill(kj::mv(promise));
    }

    void reject(kj::Exception&& exception) {
      fulfiller->reject(kj::mv(exception));
    }

  private:
    kj::Own<RpcConnectionState> connectionState;
    QuestionId id;
    kj::Own<kj::PromiseFulfiller<kj::Promise<kj::Own<RpcResponse>>>> fulfiller;
    kj::UnwindDetector unwindDetector;
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
        const kj::Exception& e = connectionState->connection.get<Disconnected>();
        return RemotePromise<AnyPointer>(
            kj::Promise<Response<AnyPointer>>(kj::cp(e)),
            AnyPointer::Pipeline(newBrokenPipeline(kj::cp(e))));
      }

      KJ_IF_MAYBE(redirect, target->writeTarget(callBuilder.getTarget())) {
        // Whoops, this capability has been redirected while we were building the request!
        // We'll have to make a new request and do a copy.  Ick.

        auto replacement = redirect->get()->newCall(
            callBuilder.getInterfaceId(), callBuilder.getMethodId(), paramsBuilder.targetSize());
        replacement.set(paramsBuilder);
        return replacement.send();
      } else {
        auto sendResult = sendInternal(false);

        auto forkedPromise = sendResult.promise.fork();

        // The pipeline must get notified of resolution before the app does to maintain ordering.
        auto pipeline = kj::refcounted<RpcPipeline>(
            *connectionState, kj::mv(sendResult.questionRef), forkedPromise.addBranch());

        auto appPromise = forkedPromise.addBranch().then(
            [=](kj::Own<RpcResponse>&& response) {
              auto reader = response->getResults();
              return Response<AnyPointer>(reader, kj::mv(response));
            });

        return RemotePromise<AnyPointer>(
            kj::mv(appPromise),
            AnyPointer::Pipeline(kj::mv(pipeline)));
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

      auto pipeline = kj::refcounted<RpcPipeline>(*connectionState, kj::mv(sendResult.questionRef));

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

    SendInternalResult sendInternal(bool isTailCall) {
      // Build the cap table.
      auto exports = connectionState->writeDescriptors(
          capTable.getTable(), callBuilder.getParams());

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

      // Finish and send.
      callBuilder.setQuestionId(questionId);
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
        question.isAwaitingReturn = false;
        question.skipFinish = true;
        result.questionRef->reject(kj::mv(*exception));
      }

      // Send and return.
      return kj::mv(result);
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
      if (state.is<Waiting>()) {
        // Wrap a PipelineClient in a PromiseClient.
        auto pipelineClient = kj::refcounted<PipelineClient>(
            *connectionState, kj::addRef(*state.get<Waiting>()), kj::heapArray(ops.asPtr()));

        KJ_IF_MAYBE(r, redirectLater) {
          auto resolutionPromise = r->addBranch().then(kj::mvCapture(ops,
              [](kj::Array<PipelineOp> ops, kj::Own<RpcResponse>&& response) {
                return response->getResults().getPipelinedCap(ops);
              }));

          return kj::refcounted<PromiseClient>(
              *connectionState, kj::mv(pipelineClient), kj::mv(resolutionPromise), nullptr);
        } else {
          // Oh, this pipeline will never get redirected, so just return the PipelineClient.
          return kj::mv(pipelineClient);
        }
      } else if (state.is<Resolved>()) {
        return state.get<Resolved>()->getResults().getPipelinedCap(ops);
      } else {
        return newBrokenCap(kj::cp(state.get<Broken>()));
      }
    }

  private:
    kj::Own<RpcConnectionState> connectionState;
    kj::Maybe<kj::ForkedPromise<kj::Own<RpcResponse>>> redirectLater;

    typedef kj::Own<QuestionRef> Waiting;
    typedef kj::Own<RpcResponse> Resolved;
    typedef kj::Exception Broken;
    kj::OneOf<Waiting, Resolved, Broken> state;

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

    kj::Maybe<kj::Array<ExportId>> send() {
      // Send the response and return the export list.  Returns nullptr if there were no caps.
      // (Could return a non-null empty array if there were caps but none of them were exports.)

      // Build the cap table.
      auto capTable = this->capTable.getTable();
      auto exports = connectionState.writeDescriptors(capTable, payload);

      // Capabilities that we are returning are subject to embargos. See `Disembargo` in rpc.capnp.
      // As explained there, in order to deal with the Tribble 4-way race condition, we need to
      // make sure that if we're returning any remote promises, that we ignore any subsequent
      // resolution of those promises for the purpose of pipelined requests on this answer. Luckily,
      // we can modify the cap table in-place.
      for (auto& slot: capTable) {
        KJ_IF_MAYBE(cap, slot) {
          slot = connectionState.getInnermostClient(**cap);
        }
      }

      message->send();
      if (capTable.size() == 0) {
        return nullptr;
      } else {
        return kj::mv(exports);
      }
    }

  private:
    RpcConnectionState& connectionState;
    kj::Own<OutgoingRpcMessage> message;
    BuilderCapabilityTable capTable;
    rpc::Payload::Builder payload;
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

  class RpcCallContext final: public CallContextHook, public kj::Refcounted {
  public:
    RpcCallContext(RpcConnectionState& connectionState, AnswerId answerId,
                   kj::Own<IncomingRpcMessage>&& request,
                   kj::Array<kj::Maybe<kj::Own<ClientHook>>> capTableArray,
                   const AnyPointer::Reader& params,
                   bool redirectResults, kj::Own<kj::PromiseFulfiller<void>>&& cancelFulfiller,
                   uint64_t interfaceId, uint16_t methodId)
        : connectionState(kj::addRef(connectionState)),
          answerId(answerId),
          interfaceId(interfaceId),
          methodId(methodId),
          requestSize(request->getBody().targetSize().wordCount),
          request(kj::mv(request)),
          paramsCapTable(kj::mv(capTableArray)),
          params(paramsCapTable.imbue(params)),
          returnMessage(nullptr),
          redirectResults(redirectResults),
          cancelFulfiller(kj::mv(cancelFulfiller)) {
      connectionState.callWordsInFlight += requestSize;
    }

    ~RpcCallContext() noexcept(false) {
      if (isFirstResponder()) {
        // We haven't sent a return yet, so we must have been canceled.  Send a cancellation return.
        unwindDetector.catchExceptionsIfUnwinding([&]() {
          // Don't send anything if the connection is broken.
          if (connectionState->connection.is<Connected>()) {
            auto message = connectionState->connection.get<Connected>()->newOutgoingMessage(
                messageSizeHint<rpc::Return>() + sizeInWords<rpc::Payload>());
            auto builder = message->getBody().initAs<rpc::Message>().initReturn();

            builder.setAnswerId(answerId);
            builder.setReleaseParamCaps(false);

            if (redirectResults) {
              // The reason we haven't sent a return is because the results were sent somewhere
              // else.
              builder.setResultsSentElsewhere();
            } else {
              builder.setCanceled();
            }

            message->send();
          }

          cleanupAnswerTable(nullptr, true);
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

      // Avoid sending results if canceled so that we don't have to figure out whether or not
      // `releaseResultCaps` was set in the already-received `Finish`.
      if (!(cancellationFlags & CANCEL_REQUESTED) && isFirstResponder()) {
        KJ_ASSERT(connectionState->connection.is<Connected>(),
                  "Cancellation should have been requested on disconnect.") {
          return;
        }

        if (response == nullptr) getResults(MessageSize{0, 0});  // force initialization of response

        returnMessage.setAnswerId(answerId);
        returnMessage.setReleaseParamCaps(false);

        kj::Maybe<kj::Array<ExportId>> exports;
        KJ_IF_MAYBE(exception, kj::runCatchingExceptions([&]() {
          // Debug info incase send() fails due to overside message.
          KJ_CONTEXT("returning from RPC call", interfaceId, methodId);
          exports = kj::downcast<RpcServerResponseImpl>(*KJ_ASSERT_NONNULL(response)).send();
        })) {
          responseSent = false;
          sendErrorReturn(kj::mv(*exception));
          return;
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
      if (isFirstResponder()) {
        if (connectionState->connection.is<Connected>()) {
          auto message = connectionState->connection.get<Connected>()->newOutgoingMessage(
              messageSizeHint<rpc::Return>() + exceptionSizeHint(exception));
          auto builder = message->getBody().initAs<rpc::Message>().initReturn();

          builder.setAnswerId(answerId);
          builder.setReleaseParamCaps(false);
          fromException(exception, builder.initException());

          message->send();
        }

        // Do not allow releasing the pipeline because we want pipelined calls to propagate the
        // exception rather than fail with a "no such field" exception.
        cleanupAnswerTable(nullptr, false);
      }
    }

    void requestCancel() {
      // Hints that the caller wishes to cancel this call.  At the next time when cancellation is
      // deemed safe, the RpcCallContext shall send a canceled Return -- or if it never becomes
      // safe, the RpcCallContext will send a normal return when the call completes.  Either way
      // the RpcCallContext is now responsible for cleaning up the entry in the answer table, since
      // a Finish message was already received.

      bool previouslyAllowedButNotRequested = cancellationFlags == CANCEL_ALLOWED;
      cancellationFlags |= CANCEL_REQUESTED;

      if (previouslyAllowedButNotRequested) {
        // We just set CANCEL_REQUESTED, and CANCEL_ALLOWED was already set previously.  Initiate
        // the cancellation.
        cancelFulfiller->fulfill();
      }
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

      if (request->getBrand() == connectionState.get() && !redirectResults) {
        // The tail call is headed towards the peer that called us in the first place, so we can
        // optimize out the return trip.

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
    void allowCancellation() override {
      bool previouslyRequestedButNotAllowed = cancellationFlags == CANCEL_REQUESTED;
      cancellationFlags |= CANCEL_ALLOWED;

      if (previouslyRequestedButNotAllowed) {
        // We just set CANCEL_ALLOWED, and CANCEL_REQUESTED was already set previously.  Initiate
        // the cancellation.
        cancelFulfiller->fulfill();
      }
    }
    kj::Own<CallContextHook> addRef() override {
      return kj::addRef(*this);
    }

  private:
    kj::Own<RpcConnectionState> connectionState;
    AnswerId answerId;

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

    enum CancellationFlags {
      CANCEL_REQUESTED = 1,
      CANCEL_ALLOWED = 2
    };

    uint8_t cancellationFlags = 0;
    // When both flags are set, the cancellation process will begin.

    kj::Own<kj::PromiseFulfiller<void>> cancelFulfiller;
    // Fulfilled when cancellation has been both requested and permitted.  The fulfilled promise is
    // exclusive-joined with the outermost promise waiting on the call return, so fulfilling it
    // cancels that promise.

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

      if (cancellationFlags & CANCEL_REQUESTED) {
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

    return connection.get<Connected>()->receiveIncomingMessage().then(
        [this](kj::Maybe<kj::Own<IncomingRpcMessage>>&& message) {
      KJ_IF_MAYBE(m, message) {
        handleMessage(kj::mv(*m));
        return true;
      } else {
        disconnect(KJ_EXCEPTION(DISCONNECTED, "Peer disconnected."));
        return false;
      }
    }).then([this](bool keepGoing) {
      // No exceptions; continue loop.
      //
      // (We do this in a separate continuation to handle the case where exceptions are
      // disabled.)
      if (keepGoing) tasks.add(messageLoop());
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
        handleResolve(reader.getResolve());
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
      resultExports = writeDescriptors(capTableArray, payload);
      capHook = KJ_ASSERT_NONNULL(capTableArray[0])->addRef();
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
    auto capTableArray = receiveCaps(payload.getCapTable());
    auto cancelPaf = kj::newPromiseAndFulfiller<void>();

    AnswerId answerId = call.getQuestionId();

    auto context = kj::refcounted<RpcCallContext>(
        *this, answerId, kj::mv(message), kj::mv(capTableArray), payload.getContent(),
        redirectResults, kj::mv(cancelPaf.fulfiller),
        call.getInterfaceId(), call.getMethodId());

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
        call.getInterfaceId(), call.getMethodId(), kj::mv(capability), context->addRef());

    // Things may have changed -- in particular if startCall() immediately called
    // context->directTailCall().

    {
      auto& answer = answers[answerId];

      answer.pipeline = kj::mv(promiseAndPipeline.pipeline);

      if (redirectResults) {
        auto resultsPromise = promiseAndPipeline.promise.then(
            kj::mvCapture(context, [](kj::Own<RpcCallContext>&& context) {
              return context->consumeRedirectedResponse();
            }));

        // If the call that later picks up `redirectedResults` decides to discard it, we need to
        // make sure our call is not itself canceled unless it has called allowCancellation().
        // So we fork the promise and join one branch with the cancellation promise, in order to
        // hold on to it.
        auto forked = resultsPromise.fork();
        answer.redirectedResults = forked.addBranch();

        cancelPaf.promise
            .exclusiveJoin(forked.addBranch().then([](kj::Own<RpcResponse>&&){}))
            .detach([](kj::Exception&&) {});
      } else {
        // Hack:  Both the success and error continuations need to use the context.  We could
        //   refcount, but both will be destroyed at the same time anyway.
        RpcCallContext* contextPtr = context;

        promiseAndPipeline.promise.then(
            [contextPtr]() {
              contextPtr->sendReturn();
            }, [contextPtr](kj::Exception&& exception) {
              contextPtr->sendErrorReturn(kj::mv(exception));
            }).catch_([&](kj::Exception&& exception) {
              // Handle exceptions that occur in sendReturn()/sendErrorReturn().
              taskFailed(kj::mv(exception));
            }).attach(kj::mv(context))
            .exclusiveJoin(kj::mv(cancelPaf.promise))
            .detach([](kj::Exception&&) {});
      }
    }
  }

  ClientHook::VoidPromiseAndPipeline startCall(
      uint64_t interfaceId, uint64_t methodId,
      kj::Own<ClientHook>&& capability, kj::Own<CallContextHook>&& context) {
    if (interfaceId == typeId<Persistent<>>() && methodId == 0) {
      KJ_IF_MAYBE(g, gateway) {
        // Wait, this is a call to Persistent.save() and we need to translate it through our
        // gateway.

        KJ_IF_MAYBE(resolvedPromise, capability->whenMoreResolved()) {
          // The plot thickens: We're looking at a promise capability. It could end up resolving
          // to a capability outside the gateway, in which case we don't want to translate at all.

          auto promises = resolvedPromise->then(kj::mvCapture(context,
              [this,interfaceId,methodId](kj::Own<CallContextHook>&& context,
                                          kj::Own<ClientHook> resolvedCap) {
            auto vpap = startCall(interfaceId, methodId, kj::mv(resolvedCap), kj::mv(context));
            return kj::tuple(kj::mv(vpap.promise), kj::mv(vpap.pipeline));
          })).attach(addRef(*this)).split();

          return {
            kj::mv(kj::get<0>(promises)),
            newLocalPromisePipeline(kj::mv(kj::get<1>(promises))),
          };
        }

        if (capability->getBrand() == this) {
          // This capability is one of our own, pointing back out over the network. That means
          // that it would be inappropriate to apply the gateway transformation. We just want to
          // reflect the call back.
          return kj::downcast<RpcClient>(*capability)
              .callNoIntercept(interfaceId, methodId, kj::mv(context));
        }

        auto params = context->getParams().getAs<Persistent<>::SaveParams>();

        auto requestSize = params.totalSize();
        ++requestSize.capCount;
        requestSize.wordCount += sizeInWords<RealmGateway<>::ExportParams>();

        auto request = g->exportRequest(requestSize);
        request.setCap(Persistent<>::Client(capability->addRef()));
        request.setParams(params);

        context->allowCancellation();
        context->releaseParams();
        return context->directTailCall(RequestHook::from(kj::mv(request)));
      }
    }

    return capability->call(interfaceId, methodId, kj::mv(context));
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

        auto& base = answers[promisedAnswer.getQuestionId()];
        KJ_REQUIRE(base.active, "PromisedAnswer.questionId is not a current question.") {
          return nullptr;
        }
        KJ_IF_MAYBE(p, base.pipeline) {
          pipeline = p->get()->addRef();
        } else {
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
    kj::Maybe<kj::Promise<kj::Own<RpcResponse>>> promiseToRelease;

    KJ_IF_MAYBE(question, questions.find(ret.getAnswerId())) {
      KJ_REQUIRE(question->isAwaitingReturn, "Duplicate Return.") { return; }
      question->isAwaitingReturn = false;

      if (ret.getReleaseParamCaps()) {
        exportsToRelease = kj::mv(question->paramExports);
      } else {
        question->paramExports = nullptr;
      }

      KJ_IF_MAYBE(questionRef, question->selfRef) {
        switch (ret.which()) {
          case rpc::Return::RESULTS: {
            KJ_REQUIRE(!question->isTailCall,
                "Tail call `Return` must set `resultsSentElsewhere`, not `results`.") {
              return;
            }

            auto payload = ret.getResults();
            auto capTableArray = receiveCaps(payload.getCapTable());
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
              KJ_IF_MAYBE(response, answer->redirectedResults) {
                questionRef->fulfill(kj::mv(*response));
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
        if (ret.isTakeFromOtherQuestion()) {
          // Be sure to release the tail call's promise.
          KJ_IF_MAYBE(answer, answers.find(ret.getTakeFromOtherQuestion())) {
            promiseToRelease = kj::mv(answer->redirectedResults);
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

    KJ_IF_MAYBE(answer, answers.find(finish.getQuestionId())) {
      KJ_REQUIRE(answer->active, "'Finish' for invalid question ID.") { return; }

      if (finish.getReleaseResultCaps()) {
        exportsToRelease = kj::mv(answer->resultExports);
      } else {
        answer->resultExports = nullptr;
      }

      pipelineToRelease = kj::mv(answer->pipeline);

      // If the call isn't actually done yet, cancel it.  Otherwise, we can go ahead and erase the
      // question from the table.
      KJ_IF_MAYBE(context, answer->callContext) {
        context->requestCancel();
      } else {
        answerToRelease = answers.erase(finish.getQuestionId());
      }
    } else {
      KJ_REQUIRE(answer->active, "'Finish' for invalid question ID.") { return; }
    }
  }

  // ---------------------------------------------------------------------------
  // Level 1

  void handleResolve(const rpc::Resolve::Reader& resolve) {
    kj::Own<ClientHook> replacement;
    kj::Maybe<kj::Exception> exception;

    // Extract the replacement capability.
    switch (resolve.which()) {
      case rpc::Resolve::CAP:
        KJ_IF_MAYBE(cap, receiveCap(resolve.getCap())) {
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

        EmbargoId embargoId = context.getSenderLoopback();

        // We need to insert an evalLater() here to make sure that any pending calls towards this
        // cap have had time to find their way through the event loop.
        tasks.add(kj::evalLater(kj::mvCapture(
            target, [this,embargoId](kj::Own<ClientHook>&& target) {
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
        })));

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
  Impl(VatNetworkBase& network, kj::Maybe<Capability::Client> bootstrapInterface,
       kj::Maybe<RealmGateway<>::Client> gateway)
      : network(network), bootstrapInterface(kj::mv(bootstrapInterface)),
        bootstrapFactory(*this), gateway(kj::mv(gateway)), tasks(*this) {
    tasks.add(acceptLoop());
  }
  Impl(VatNetworkBase& network, BootstrapFactoryBase& bootstrapFactory,
       kj::Maybe<RealmGateway<>::Client> gateway)
      : network(network), bootstrapFactory(bootstrapFactory),
        gateway(kj::mv(gateway)), tasks(*this) {
    tasks.add(acceptLoop());
  }
  Impl(VatNetworkBase& network, SturdyRefRestorerBase& restorer)
      : network(network), bootstrapFactory(*this), restorer(restorer), tasks(*this) {
    tasks.add(acceptLoop());
  }

  ~Impl() noexcept(false) {
    unwindDetector.catchExceptionsIfUnwinding([&]() {
      // std::unordered_map doesn't like it when elements' destructors throw, so carefully
      // disassemble it.
      if (!connections.empty()) {
        kj::Vector<kj::Own<RpcConnectionState>> deleteMe(connections.size());
        kj::Exception shutdownException = KJ_EXCEPTION(FAILED, "RpcSystem was destroyed.");
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
    } else KJ_IF_MAYBE(r, restorer) {
      return r->baseRestore(objectId);
    } else {
      return Capability::Client(newBrokenCap(
          "SturdyRef referred to a local object but there is no local SturdyRef restorer."));
    }
  }

  void setFlowLimit(size_t words) {
    flowLimit = words;

    for (auto& conn: connections) {
      conn.second->setFlowLimit(words);
    }
  }

private:
  VatNetworkBase& network;
  kj::Maybe<Capability::Client> bootstrapInterface;
  BootstrapFactoryBase& bootstrapFactory;
  kj::Maybe<RealmGateway<>::Client> gateway;
  kj::Maybe<SturdyRefRestorerBase&> restorer;
  size_t flowLimit = kj::maxValue;
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
          bootstrapFactory, gateway, restorer, kj::mv(connection),
          kj::mv(onDisconnect.fulfiller), flowLimit);
      RpcConnectionState& result = *newState;
      connections.insert(std::make_pair(connectionPtr, kj::mv(newState)));
      return result;
    } else {
      return *iter->second;
    }
  }

  kj::Promise<void> acceptLoop() {
    auto receive = network.baseAccept().then(
        [this](kj::Own<VatNetworkBase::Connection>&& connection) {
      getConnectionState(kj::mv(connection));
    });
    return receive.then([this]() {
      // No exceptions; continue loop.
      //
      // (We do this in a separate continuation to handle the case where exceptions are
      // disabled.)
      tasks.add(acceptLoop());
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
                             kj::Maybe<Capability::Client> bootstrapInterface,
                             kj::Maybe<RealmGateway<>::Client> gateway)
    : impl(kj::heap<Impl>(network, kj::mv(bootstrapInterface), kj::mv(gateway))) {}
RpcSystemBase::RpcSystemBase(VatNetworkBase& network,
                             BootstrapFactoryBase& bootstrapFactory,
                             kj::Maybe<RealmGateway<>::Client> gateway)
    : impl(kj::heap<Impl>(network, bootstrapFactory, kj::mv(gateway))) {}
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

}  // namespace _ (private)
}  // namespace capnp
