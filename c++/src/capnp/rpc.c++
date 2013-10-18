// Copyright (c) 2013, Kenton Varda <temporal@gmail.com>
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this
//    list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
// ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "rpc.h"
#include "capability-context.h"
#include <kj/debug.h>
#include <kj/vector.h>
#include <kj/async.h>
#include <unordered_map>
#include <queue>
#include <capnp/rpc.capnp.h>

namespace capnp {
namespace _ {  // private

namespace {

typedef uint32_t QuestionId;
typedef uint32_t ExportId;

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

  bool erase(Id id) {
    if (id < slots.size() && slots[id] != nullptr) {
      slots[id] = T();
      freeIds.push(id);
      return true;
    } else {
      return false;
    }
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

  void erase(Id id) {
    if (id < kj::size(low)) {
      low[id] = T();
    } else {
      high.erase(id);
    }
  }

private:
  T low[16];
  std::unordered_map<Id, T> high;
};

struct Question {
  kj::Array<ExportId> exportsInParams;
  // Exports embedded in the call message which should be implicitly released on return (unless
  // they are in the retain list).

  kj::Own<kj::PromiseFulfiller<Response<ObjectPointer>>> fulfiller;
  // Fulfill with the response.

  bool isStarted = false;
  // Is this Question currently in-use?

  bool isReturned = false;
  // Has the call returned?

  bool isReleased = false;
  // Has the call been released locally, and the ReleaseAnswer message sent?  Note that this could
  // occur *before* the call returns.

  inline bool operator==(decltype(nullptr)) const { return !isStarted; }
  inline bool operator!=(decltype(nullptr)) const { return isStarted; }
};

struct Answer {
  bool active = false;

  kj::Own<const PipelineHook> pipeline;
  // Send pipelined calls here.

  kj::Promise<void> asyncOp = nullptr;
  // Delete this promise to cancel the call.
};

struct Export {
  uint refcount = 0;
  // When this reaches 0, drop `clientHook` and free this export.

  kj::Own<ClientHook> clientHook;

  inline bool operator==(decltype(nullptr)) const { return refcount == 0; }
  inline bool operator!=(decltype(nullptr)) const { return refcount != 0; }
};

struct Import {
  uint remoteRefcount = 0;
  // Number of times we've received this import from the peer.

  uint localRefcount = 0;
  // Number of local proxies that currently exist wrapping this import.  Once this reaches zero,
  // a Release message should be sent for `remoteRefcount` references and the import should be
  // removed from the table.  (It would be nice to construct only one proxy object and use its
  // own reference count, but it would be hard to prevent it from being destroyed in another thread
  // at exactly the moment that we call addRef() on it.)
};

class RpcConnectionState: public kj::TaskSet::ErrorHandler {
public:
  RpcConnectionState(const kj::EventLoop& eventLoop,
                     kj::Own<VatNetworkBase::Connection>&& connection)
      : eventLoop(eventLoop), connection(kj::mv(connection)), tasks(eventLoop, *this) {
    tasks.add(messageLoop());
  }

  void taskFailed(kj::Exception&& exception) override {
    // TODO(now):  Kill the connection.
  }

private:
  const kj::EventLoop& eventLoop;
  kj::Own<VatNetworkBase::Connection> connection;

  struct Tables {
    ExportTable<QuestionId, Question> questions;
    ImportTable<QuestionId, Answer> answers;
    ExportTable<ExportId, Export> exports;
    ImportTable<ExportId, Import> imports;
  };
  kj::MutexGuarded<Tables> tables;

  kj::TaskSet tasks;

  class ClientHookImpl final: public ClientHook {
  public:
    ClientHookImpl(RpcConnectionState& connectionState, ExportId importId);
  };

  // =====================================================================================

  class CapExtractorImpl final: public CapExtractor<rpc::CapDescriptor> {
  public:
    CapExtractorImpl(const RpcConnectionState& connectionState)
        : connectionState(connectionState) {}

    ~CapExtractorImpl() {
      if (retainedCaps.getWithoutLock().size() > 0) {
        // Oops, we were deleted without finalizeRetainedCaps.  We really need to make sure that
        // the references we kept get unreferenced.

        auto lock = connectionState.tables.lockExclusive();
        for (auto importId: retainedCaps.getWithoutLock()) {
          Import& import = lock->imports[importId];
          if (--lock->imports[importId].localRefcount == 0) {
            if (import.remoteRefcount != 0) {
              connectionState.sendReleaseLater(importId, import.remoteRefcount);
              import.remoteRefcount = 0;
            }
          }
        }
      }
    }

    Orphan<List<ExportId>> finalizeRetainedCaps(Orphanage orphanage) {
      // TODO(now):  Go back through the caps and decrement their localRefcounts.  Then go through
      //   them again, and for each whose refcount is now zero, remove the import from the table
      //   and don't retain it after all (if remoteRefcount is non-zero, arrange for a release
      //   message to be sent).  Otherwise, retain it and increment the remoteRefcount.

      kj::Vector<ExportId> retainedCaps = kj::mv(*this->retainedCaps.lockExclusive());

      auto lock = connectionState.tables.lockExclusive();

      // Remove the extra refcount we kept on each retained cap.
      for (auto importId: retainedCaps) {
        --lock->imports[importId].localRefcount;
      }

      // Un-retain all of the ones that now have a refcount of zero.
      uint count = 0;
      for (auto importId: retainedCaps) {
        Import& import = lock->imports[importId];
        if (import.localRefcount == 0) {
          if (import.remoteRefcount != 0) {
            // localRefcount reached zero but remoteRefcount is not zero.  `extractCap()` only
            // adds the cap to `retainedCaps` at all if it provided the first local reference.
            // So, the only way to get here is the following sequence:
            // - extractCap() extracts the first instance of this import ID.  `remoteRefcount`
            //   is zero at that time.
            // - Some parallel message introduces the same import ID again, incrementing its
            //   `remoteRefcount`.
            // - Both references are discarded by the application.
            //
            // In any case, since we're dropping the last local reference but the import has a
            // non-zero remote refcount, we have to arrange for a `Release` message to be sent.
            connectionState.sendReleaseLater(importId, import.remoteRefcount);
            import.remoteRefcount = 0;
          }
        } else {
          ++count;
        }
      }

      // Finally, build the retain list out of the imports that had non-zero refcounts.
      auto result = orphanage.newOrphan<List<ExportId>>(count);
      auto resultBuilder = result.get();
      count = 0;
      for (auto importId: retainedCaps) {
        Import& import = lock->imports[importId];
        if (import.localRefcount != 0) {
          resultBuilder.set(count++, importId);
        }
      }

      return kj::mv(result);
    }

    // implements CapDescriptor ------------------------------------------------

    kj::Own<const ClientHook> extractCap(rpc::CapDescriptor::Reader descriptor) const override {
      switch (descriptor.which()) {
        case rpc::CapDescriptor::SENDER_HOSTED: {
          ExportId importId = descriptor.getSenderHosted();
          {
            auto lock = connectionState.tables.lockExclusive();
            Import& import = lock->imports[importId];

            if (import.localRefcount == 0) {
              // We haven't seen this import before, so we'll need to flag it as retained.  For
              // now, increment its local refcount so that it can't possibly be released before
              // we get to `finalizeRetainedCaps()`.
              retainedCaps.lockExclusive()->add(importId);
              import.localRefcount = 1;
            }

            // Increment the local refcount for the ClientHook that we're about to return.
            ++import.localRefcount;
          }
          return kj::refcounted<ClientHookImpl>(importId);
        }

        case rpc::CapDescriptor::SENDER_PROMISE:
          // TODO(now):  Implement this or remove `senderPromise`.
          return newBrokenCap("senderPromise not implemented");

        case rpc::CapDescriptor::RECEIVER_HOSTED: {
          auto lock = connectionState.tables.lockExclusive();  // TODO(perf): shared?
          KJ_IF_MAYBE(exp, lock->exports.find(descriptor.getReceiverHosted())) {
            return exp->clientHook->addRef();
          }
          return newBrokenCap("invalid 'receiverHosted' export ID");
        }

        case rpc::CapDescriptor::RECEIVER_ANSWER: {
          // TODO(now): implement
          return newBrokenCap("'receiverAnswer' not implemented");
        }

        case rpc::CapDescriptor::THIRD_PARTY_HOSTED:
          return newBrokenCap("three-way introductions not implemented");

        default:
          return newBrokenCap("unknown CapDescriptor type");
      }
    }

  private:
    const RpcConnectionState& connectionState;
    kj::MutexGuarded<kj::Vector<ExportId>> retainedCaps;
  };

  // =====================================================================================

  class CapInjectorImpl final: public CapInjector<rpc::CapDescriptor> {
  public:
    // implements CapInjector ----------------------------------------

    void injectCap(rpc::CapDescriptor::Builder descriptor,
                   kj::Own<const ClientHook>&& cap) const override {
      if (cap->getBrand() == &connectionState) {
        kj::downcast<const ClientHookImpl&>(*cap).writeDescriptor(descriptor);
        state.lockExclusive()->receiverHosted.add(kj::mv(cap));
      } else {
        // TODO(now):  We have to figure out if the client is already in our table.
      }
    }
    kj::Own<const ClientHook> getInjectedCap(rpc::CapDescriptor::Reader descriptor) const override {

    }
    void dropCap(rpc::CapDescriptor::Reader descriptor) const override {
      switch (descriptor.which()) {
        case rpc::CapDescriptor::SENDER_HOSTED: {
          state.lockExclusive()->dropped.add(descriptor.getSenderHosted());
          auto lock = connectionState.tables.lockExclusive();
          KJ_IF_MAYBE(exp, lock->exports.find(descriptor.getSenderHosted())) {
            if (--exp->refcount == 0) {
              exp->clientHook = nullptr;
            }
          } else {
            KJ_FAIL_REQUIRE("Dropped descriptor had invalid 'senderHosted'.") { break; }
          }
          break;
        }

        case rpc::CapDescriptor::RECEIVER_HOSTED:
        case rpc::CapDescriptor::RECEIVER_ANSWER:
          // No big deal if we hold on to the ClientHooks a little longer until this message
          // is sent.
          break;

        default:
          KJ_FAIL_REQUIRE("I don't think I wrote this descriptor.") { break; }
          break;
      }
    }

  private:
    const RpcConnectionState& connectionState;

    struct State {
      kj::Vector<ExportId> senderHosted;
      // Local capabilities that are being exported with this message.  These have had their
      // refcounts in the exports table increased by one while the CapInjector exists, but those
      // refs will be released in the destructor, so the receiver will have to explicitly retain
      // them before that point to keep them live.

      kj::Vector<ExportId> dropped;
      // Exports that were injected but then subsequently dropped.  Each ID in this list also
      // appears in senderHosted -- the instance in `dropped` essentially negates its existence in
      // `senderHosted`.

      kj::Vector<kj::Own<const ClientHook>> receiverHosted;
      // Capabilities (exports and promised-answers) hosted by the receiver which have been injected
      // into this message.  This vector exists only to hold references to these caps to prevent
      // them from being prematurely released before the message can be sent.
    };
    kj::MutexGuarded<State> state;
  };

  // =====================================================================================

  class RpcCallContext final: public CallContextHook,
                              public CapExtractor<rpc::CapDescriptor>,
                              public CapInjector<rpc::CapDescriptor>,
                              public kj::Refcounted {
  public:
    RpcCallContext(RpcConnectionState& connectionState, QuestionId questionId,
                   kj::Own<IncomingRpcMessage>&& request, const ObjectPointer::Reader& params);

    void sendReturn();
    void sendErrorReturn(kj::Exception&& exception);

    // implements CallContextHook ------------------------------------

    ObjectPointer::Reader getParams() override {
      KJ_REQUIRE(request != nullptr, "Can't call getParams() after releaseParams().");
      return params;
    }
    void releaseParams() override {
      request = nullptr;
    }
    ObjectPointer::Builder getResults(uint firstSegmentWordSize) override {
      KJ_IF_MAYBE(r, response) {
        return r->get()->getBody().getAs<rpc::Message>().getReturn().getAnswer();
      } else {
        auto message = connectionState.connection->newOutgoingMessage(
            firstSegmentWordSize == 0 ? 0 : firstSegmentWordSize + 10);
        auto result = message->getBody().initAs<rpc::Message>().initReturn().getAnswer();
        response = kj::mv(message);
        return result;
      }
    }
    void allowAsyncCancellation(bool allow) override {
      // TODO(soon):  Do we want this or not?
      KJ_FAIL_REQUIRE("not implemented");
    }
    bool isCanceled() override {
      // TODO(soon):  Do we want this or not?
      KJ_FAIL_REQUIRE("not implemented");
    }
    kj::Own<CallContextHook> addRef() override {
      return kj::addRef(*this);
    }

  private:
    RpcConnectionState& connectionState;
    QuestionId questionId;
    kj::Maybe<kj::Own<IncomingRpcMessage>> request;

    CapExtractorImpl requestCapExtractor;
    CapReaderContext requestCapContext;

    CapBuilderContext responseContext;

    ObjectPointer::Reader params;
    kj::Maybe<kj::Own<OutgoingRpcMessage>> response;
  };

  kj::Promise<void> messageLoop() {
    return connection->receiveIncomingMessage().then(
        [this](kj::Own<IncomingRpcMessage>&& message) {
      handleMessage(kj::mv(message));
      // TODO(now):  Don't repeat messageLoop in case of exception.
      tasks.add(messageLoop());
    }, [this](kj::Exception&& exception) {
      // TODO(now):  This probably isn't right.
      connection = nullptr;
      kj::throwRecoverableException(kj::mv(exception));
    });
  }

  void handleMessage(kj::Own<IncomingRpcMessage> message) {
    auto reader = message->getBody().getAs<rpc::Message>();
    switch (reader.which()) {
      case rpc::Message::UNIMPLEMENTED:
        doUnimplemented(reader.getUnimplemented());
        break;

      case rpc::Message::ABORT:
        doAbort(reader.getAbort());
        break;

      case rpc::Message::CALL:
        doCall(kj::mv(message), reader.getCall());
        break;

      default: {
        auto message = connection->newOutgoingMessage(reader.totalSizeInWords() + 6);
        message->getBody().initAs<rpc::Message>().setUnimplemented(reader);
        message->send();
        break;
      }
    }
  }

  void doUnimplemented(const rpc::Message::Reader& message) {
    // TODO(now)
  }

  void doAbort(const rpc::Exception::Reader& exception) {
    kj::throwRecoverableException(toException(exception));
  }

  void doCall(kj::Own<IncomingRpcMessage>&& message, const rpc::Call::Reader& call) {
    kj::Own<const ClientHook> capability;

    auto target = call.getTarget();
    switch (target.which()) {
      case rpc::Call::Target::EXPORTED_CAP: {
        auto lock = tables.lockExclusive();  // TODO(perf):  shared?
        KJ_IF_MAYBE(exp, lock->exports.find(target.getExportedCap())) {
          capability = exp->clientHook->addRef();
        } else {
          KJ_FAIL_REQUIRE("Call target is not a current export ID.") {
            return;
          }
        }
        break;
      }

      case rpc::Call::Target::PROMISED_ANSWER: {
        auto promisedAnswer = target.getPromisedAnswer();
        kj::Own<const PipelineHook> pipeline;

        {
          auto lock = tables.lockExclusive();  // TODO(perf):  shared?
          const Answer& base = lock->answers[promisedAnswer.getQuestionId()];
          KJ_REQUIRE(base.active, "PromisedAnswer.questionId is not a current question.") {
            return;
          }
          pipeline = base.pipeline->addRef();
        }

        auto opsReader = promisedAnswer.getTransform();
        auto ops = kj::heapArrayBuilder<PipelineOp>(opsReader.size());
        for (auto opReader: opsReader) {
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
              // TODO(soon):  Handle better.
              KJ_FAIL_REQUIRE("Unsupported pipeline op.", (uint)opReader.which()) {
                return;
              }
          }
          ops.add(op);
        }
        capability = pipeline->getPipelinedCap(ops.finish());
        break;
      }

      default:
        // TODO(soon):  Handle better.
        KJ_FAIL_REQUIRE("Unknown call target type.", (uint)target.which()) {
          return;
        }
    }

    // TODO(now):  Imbue the message!

    QuestionId questionId = call.getQuestionId();
    auto context = kj::refcounted<RpcCallContext>(
        *this, questionId, kj::mv(message), call.getRequest());
    auto promiseAndPipeline = capability->call(
        call.getInterfaceId(), call.getMethodId(), context->addRef());

    // No more using `call` after this point!

    {
      auto lock = tables.lockExclusive();

      Answer& answer = lock->answers[questionId];

      // We don't want to overwrite an active question because the destructors for the promise and
      // pipeline could try to lock our mutex.  Of course, we did already fire off the new call
      // above, but that's OK because it won't actually ever inspect the Answer table itself, and
      // we're about to close the connection anyway.
      KJ_REQUIRE(!answer.active, "questionId is already in use") {
        return;
      }

      answer.active = true;
      answer.pipeline = kj::mv(promiseAndPipeline.pipeline);

      // Hack:  Both the success and error continuations need to use the context.  We could
      //   refcount, but both will be destroyed at the same time anyway.
      RpcCallContext* contextPtr = context;

      // TODO(cleanup):  We have the continuations return Promise<void> rather than void because
      //   this tricks the promise framework into making sure the continuations actually run
      //   without anyone waiting on them.  We should find a cleaner way to do this.
      answer.asyncOp = promiseAndPipeline.promise.then(
          kj::mvCapture(context, [this](kj::Own<RpcCallContext>&& context) -> kj::Promise<void> {
            context->sendReturn();
            return kj::READY_NOW;
          }), [this,contextPtr](kj::Exception&& exception) -> kj::Promise<void> {
            contextPtr->sendErrorReturn(kj::mv(exception));
            return kj::READY_NOW;
          });
    }
  }




  kj::Exception toException(const rpc::Exception::Reader& exception) {
    // TODO(now)
  }

  void sendReleaseLater(ExportId importId, uint remoteRefcount) const {
    tasks.add(eventLoop.evalLater([this,importId,remoteRefcount]() {
      auto message = connection->newOutgoingMessage(8);
      rpc::Release::Builder builder = message->getBody().initAs<rpc::Message>().initRelease();
      builder.setId(importId);
      builder.setReferenceCount(remoteRefcount);
      message->send();
    }));
  }
};

}  // namespace

class RpcSystemBase::Impl {
public:
  Impl(VatNetworkBase& network, SturdyRefRestorerBase& restorer, const kj::EventLoop& eventLoop)
      : network(network), restorer(restorer), eventLoop(eventLoop) {}

private:
  VatNetworkBase& network;
  SturdyRefRestorerBase& restorer;
  const kj::EventLoop& eventLoop;
};

RpcSystemBase::RpcSystemBase(VatNetworkBase& network, SturdyRefRestorerBase& restorer,
                             const kj::EventLoop& eventLoop)
    : impl(kj::heap<Impl>(network, restorer, eventLoop)) {}
RpcSystemBase::~RpcSystemBase() noexcept(false) {}

Capability::Client RpcSystemBase::baseConnect(_::StructReader reader) {
  impl->connect(reader);
}

}  // namespace _ (private)
}  // namespace capnp
