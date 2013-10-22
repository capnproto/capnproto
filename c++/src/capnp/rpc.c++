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
#include <kj/one-of.h>
#include <unordered_map>
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
        // TODO(soon):  Handle better?
        KJ_FAIL_REQUIRE("Unsupported pipeline op.", (uint)opReader.which()) {
          return nullptr;
        }
    }
  }
  return result.finish();
}

Orphan<List<rpc::PromisedAnswer::Op>> fromPipelineOps(
    Orphanage orphanage, const kj::Array<PipelineOp>& ops) {
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

template <typename ParamCaps, typename RpcPipeline>
struct Question {
  kj::Own<ParamCaps> paramCaps;
  // A handle representing the capabilities in the parameter struct.  This will be dropped as soon
  // as the call returns.

  kj::Own<kj::PromiseFulfiller<Response<ObjectPointer>>> fulfiller;
  // Fulfill with the response.

  kj::Maybe<RpcPipeline&> pipeline;
  // The local pipeline object.  The RpcPipeline's own destructor sets this value to null and then
  // sends the Finish message.
  //
  // TODO(cleanup):  We only have this pointer here because CapInjectorImpl::getInjectedCap() needs
  //   it, but perhaps CapInjectorImpl should instead hold on to the ClientHook it got in the first
  //   place.

  bool isStarted = false;
  // Is this Question currently in-use?

  bool isReturned = false;
  // Has the call returned?

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

class RpcConnectionState: public kj::TaskSet::ErrorHandler {
public:
  RpcConnectionState(const kj::EventLoop& eventLoop,
                     kj::Own<VatNetworkBase::Connection>&& connection)
      : eventLoop(eventLoop), connection(kj::mv(connection)), tasks(eventLoop, *this),
        exportDisposer(*this) {
    tasks.add(messageLoop());
  }

  void taskFailed(kj::Exception&& exception) override {
    // TODO(now):  Kill the connection.
  }

private:
  const kj::EventLoop& eventLoop;
  kj::Own<VatNetworkBase::Connection> connection;

  class ImportClient;
  class CapInjectorImpl;
  class CapExtractorImpl;
  class RpcPipeline;

  struct Tables {
    ExportTable<QuestionId, Question<CapInjectorImpl, RpcPipeline>> questions;
    ImportTable<QuestionId, Answer> answers;
    ExportTable<ExportId, Export> exports;
    ImportTable<ExportId, kj::Maybe<ImportClient&>> imports;
  };
  kj::MutexGuarded<Tables> tables;

  kj::TaskSet tasks;


  class ExportDisposer final: public kj::Disposer {
  public:
    inline ExportDisposer(const RpcConnectionState& connectionState)
        : connectionState(connectionState) {}

  protected:
    void disposeImpl(void* pointer) const override {
      auto lock = connectionState.tables.lockExclusive();
      ExportId id = reinterpret_cast<intptr_t>(pointer);
      KJ_IF_MAYBE(exp, lock->exports.find(id)) {
        if (--exp->refcount == 0) {
          KJ_ASSERT(lock->exports.erase(id)) {
            break;
          }
        }
      } else {
        KJ_FAIL_REQUIRE("invalid export ID", id) { break; }
      }
    }

  private:
    const RpcConnectionState& connectionState;
  };

  ExportDisposer exportDisposer;

  // =====================================================================================
  // ClientHook implementations

  class RpcClient: public ClientHook, public kj::Refcounted {
  public:
    RpcClient(const RpcConnectionState& connectionState)
        : connectionState(connectionState) {}

    virtual kj::Own<const kj::Refcounted> writeDescriptor(
        rpc::CapDescriptor::Builder descriptor) const = 0;
    // Writes a CapDescriptor referencing this client.  Returns a reference to some object which
    // must be held at least until the message containing `descriptor` has been sent.
    //
    // TODO(cleanup):  Specialize Own<void> so that we can return it here instead of
    //   Own<Refcounted>.

    // implements ClientHook -----------------------------------------

    VoidPromiseAndPipeline call(uint64_t interfaceId, uint16_t methodId,
                                kj::Own<CallContextHook>&& context) const override {
      auto params = context->getParams();

      newOutgoingMessage

      newCall(interfaceId, methodId, params.targetSizeInWords() + CALL_MESSAGE_SIZE);
    }

    kj::Own<const ClientHook> addRef() const override {
      return kj::addRef(*this);
    }
    const void* getBrand() const override {
      return &connectionState;
    }

  protected:
    const RpcConnectionState& connectionState;
  };

  class ImportClient final: public RpcClient {
  public:
    ImportClient(const RpcConnectionState& connectionState, ExportId importId, bool isPromise)
        : RpcClient(connectionState), importId(importId), isPromise(isPromise) {}
    ~ImportClient() noexcept(false) {
      {
        // Remove self from the import table, if the table is still pointing at us.  (It's possible
        // that another thread attempted to obtain this import just as the destructor started, in
        // which case that other thread will have constructed a new ImportClient and placed it in
        // the import table.)
        auto lock = connectionState.tables.lockExclusive();
        KJ_IF_MAYBE(ptr, lock->imports[importId]) {
          if (ptr == this) {
            lock->imports[importId] = nullptr;
          }
        }
      }

      // Send a message releasing our remote references.
      if (remoteRefcount > 0) {
        connectionState.sendReleaseLater(importId, remoteRefcount);
      }
    }

    kj::Maybe<kj::Own<ImportClient>> tryAddRemoteRef() {
      // Add a new RemoteRef and return a new ref to this client representing it.  Returns null
      // if this client is being deleted in another thread, in which case the caller should
      // construct a new one.

      KJ_IF_MAYBE(ref, kj::tryAddRef(*this)) {
        ++remoteRefcount;
        return kj::mv(*ref);
      } else {
        return nullptr;
      }
    }

    kj::Own<const kj::Refcounted> writeDescriptor(
        rpc::CapDescriptor::Builder descriptor) const override {
      descriptor.setReceiverHosted(importId);
      return kj::addRef(*this);
    }

    // implements ClientHook -----------------------------------------
    Request<ObjectPointer, ObjectPointer> newCall(
        uint64_t interfaceId, uint16_t methodId, uint firstSegmentWordSize) const override;
    kj::Maybe<kj::Promise<kj::Own<const ClientHook>>> whenMoreResolved() const override;

  private:
    ExportId importId;
    bool isPromise;

    uint remoteRefcount = 0;
    // Number of times we've received this import from the peer.
  };

  class PromisedAnswerClient final: public RpcClient {
  public:
    PromisedAnswerClient(const RpcConnectionState& connectionState, QuestionId questionId,
                         kj::Array<PipelineOp>&& ops, kj::Own<const RpcPipeline> pipeline);

    kj::Own<const kj::Refcounted> writeDescriptor(rpc::CapDescriptor::Builder descriptor) const override {
      auto lock = state.lockShared();

      if (lock->is<Waiting>()) {
        auto promisedAnswer = descriptor.initReceiverAnswer();
        promisedAnswer.setQuestionId(questionId);
        promisedAnswer.adoptTransform(fromPipelineOps(
            Orphanage::getForMessageContaining(descriptor), ops));

        // Return a ref to the RpcPipeline to ensure that we don't send a Finish message for this
        // call before the message containing this CapDescriptor is sent.
        return kj::addRef(*lock->get<Waiting>());
      } else {
        // TODO(now):  Problem:  This won't necessarily be a remote cap!
        return connectionState.writeDescriptor(
            lock->get<Resolved>().getPipelinedCap(ops), descriptor);
      }
    }

    // implements ClientHook -----------------------------------------
    Request<ObjectPointer, ObjectPointer> newCall(
        uint64_t interfaceId, uint16_t methodId, uint firstSegmentWordSize) const override;
    kj::Maybe<kj::Promise<kj::Own<const ClientHook>>> whenMoreResolved() const override;

  private:
    const RpcConnectionState& connectionState;
    QuestionId questionId;
    kj::Array<PipelineOp> ops;

    typedef kj::Own<const RpcPipeline> Waiting;
    typedef Response<ObjectPointer> Resolved;
    kj::MutexGuarded<kj::OneOf<Waiting, Resolved>> state;
  };

  kj::Own<const kj::Refcounted> writeDescriptor(
      kj::Own<const ClientHook>&& cap, rpc::CapDescriptor::Builder descriptor) const {
    // Write a descriptor for the given capability.  Returns a reference to something which must
    // be held at least until the message containing the descriptor is sent.

    if (cap->getBrand() == this) {
      return kj::downcast<const RpcClient>(*cap).writeDescriptor(descriptor);
    } else {
      // TODO(now):  We have to figure out if the client is already in our table.
      // TODO(now):  We have to add a refcount to the export, and return an object that decrements
      //   that refcount later.
    }
  }

  // =====================================================================================
  // CapExtractor / CapInjector implementations

  class CapExtractorImpl final: public CapExtractor<rpc::CapDescriptor> {
    // Reads CapDescriptors from a received message.

  public:
    CapExtractorImpl(const RpcConnectionState& connectionState)
        : connectionState(connectionState) {}

    ~CapExtractorImpl() noexcept(false) {
      KJ_ASSERT(retainedCaps.getWithoutLock().size() > 0,
                "CapExtractorImpl destroyed without getting a chance to retain the caps!") {
        break;
      }
    }

    uint retainedListSizeHint(bool final) {
      // Get the expected size of the retained caps list, in words.  If `final` is true, then it
      // is known that no more caps will be extracted after this point, so an exact value can be
      // returned.  Otherwise, the returned size includes room for error.

      // If `final` is true then there's no need to lock.  If it is false, then asynchronous
      // access is possible.  It's probably not worth taking the lock to look; we'll just return
      // a silly estimate.
      uint count = final ? retainedCaps.getWithoutLock().size() : 32;
      return (count * sizeof(ExportId) + (sizeof(ExportId) - 1)) / sizeof(word);
    }

    Orphan<List<ExportId>> finalizeRetainedCaps(Orphanage orphanage) {
      // Called on finalization, when the lock is no longer needed.
      kj::Vector<ExportId> retainedCaps = kj::mv(this->retainedCaps.getWithoutLock());

      auto lock = connectionState.tables.lockExclusive();

      auto actualRetained = retainedCaps.begin();
      for (ExportId importId: retainedCaps) {
        // Check if the import still exists under this ID.
        KJ_IF_MAYBE(import, lock->imports[importId]) {
          if (import->tryAddRemoteRef() != nullptr) {
            // Import indeed still exists!  We are responsible for retaining it.
            *actualRetained++ = importId;
          }
        }
      }

      uint count = actualRetained - retainedCaps.begin();

      // Build the retain list out of the imports that had non-zero refcounts.
      auto result = orphanage.newOrphan<List<ExportId>>(count);
      auto resultBuilder = result.get();
      count = 0;
      for (auto iter = retainedCaps.begin(); iter < actualRetained; ++iter) {
        resultBuilder.set(count++, *iter);
      }

      return kj::mv(result);
    }

    // implements CapDescriptor ------------------------------------------------

    kj::Own<const ClientHook> extractCap(rpc::CapDescriptor::Reader descriptor) const override {
      switch (descriptor.which()) {
        case rpc::CapDescriptor::SENDER_HOSTED: {
          ExportId importId = descriptor.getSenderHosted();

          auto lock = connectionState.tables.lockExclusive();

          KJ_IF_MAYBE(import, lock->imports[importId]) {
            // The import is already on the table, but it could be being deleted in another
            // thread.
            KJ_IF_MAYBE(ref, kj::tryAddRef(*import)) {
              // We successfully grabbed a reference to the import without it being deleted in
              // another thread.  Since this import already exists, we don't have to take
              // responsibility for retaining it.  We can just return the existing object and
              // be done with it.
              return kj::mv(*ref);
            }
          }

          // No import for this ID exists currently, so create one.
          auto result = kj::refcounted<ImportClient>(connectionState, importId);
          lock->imports[importId] = *result;

          // Note that we need to retain this import later if it still exists.
          retainedCaps.lockExclusive()->add(importId);

          return kj::mv(result);
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
    // Imports which we are responsible for retaining, should they still exist at the time that
    // this message is released.
  };

  // -----------------------------------------------------------------

  class CapInjectorImpl final: public CapInjector<rpc::CapDescriptor> {
    // Write CapDescriptors into a message as it is being built, before sending it.

  public:
    CapInjectorImpl(const RpcConnectionState& connectionState)
        : connectionState(connectionState) {}
    ~CapInjectorImpl() {}

    // implements CapInjector ----------------------------------------

    void injectCap(rpc::CapDescriptor::Builder descriptor,
                   kj::Own<const ClientHook>&& cap) const override {
      auto ref = connectionState.writeDescriptor(kj::mv(cap), descriptor);
      refs.lockExclusive()->add(kj::mv(ref));
    }
    kj::Own<const ClientHook> getInjectedCap(rpc::CapDescriptor::Reader descriptor) const override {
      switch (descriptor.which()) {
        case rpc::CapDescriptor::SENDER_HOSTED: {
          auto lock = connectionState.tables.lockExclusive();
          KJ_IF_MAYBE(exp, lock->exports.find(descriptor.getSenderHosted())) {
            return exp->clientHook->addRef();
          } else {
            KJ_FAIL_REQUIRE("Dropped descriptor had invalid 'senderHosted'.") {
              return newBrokenCap("Calling invalid CapDescriptor found in builder.");
            }
          }
        }

        case rpc::CapDescriptor::RECEIVER_HOSTED: {
          auto lock = connectionState.tables.lockExclusive();

          KJ_IF_MAYBE(import, lock->imports[descriptor.getReceiverHosted()]) {
            KJ_IF_MAYBE(ref, kj::tryAddRef(*import)) {
              return kj::mv(*ref);
            }
          }

          // If we wrote this CapDescriptor then we should hold a reference to the import in
          // our `receiverHosted` table, yet it seems that the import ID is not valid.  Something
          // is wrong.
          return newBrokenCap("CapDescriptor in builder had invalid 'receiverHosted'.");
        }

        case rpc::CapDescriptor::RECEIVER_ANSWER: {
          auto promisedAnswer = descriptor.getReceiverAnswer();
          auto lock = connectionState.tables.lockExclusive();

          KJ_IF_MAYBE(question, lock->questions.find(promisedAnswer.getQuestionId())) {
            KJ_IF_MAYBE(pipeline, question->pipeline) {
              KJ_IF_MAYBE(ops, toPipelineOps(promisedAnswer.getTransform())) {
                return kj::refcounted<PromisedAnswerClient>(
                    connectionState, promisedAnswer.getQuestionId(),
                    kj::mv(*ops), kj::addRef(*pipeline));
              }
            }
          }
          return newBrokenCap("CapDescriptor in builder had invalid PromisedAnswer.");
        }

        default:
          KJ_FAIL_REQUIRE("I don't think I wrote this descriptor.") {
            return newBrokenCap("CapDescriptor in builder was invalid.");
          }
          break;
      }
    }
    void dropCap(rpc::CapDescriptor::Reader descriptor) const override {
      // TODO(someday):  We could implement this by maintaining a map from CapDescriptors to
      //   the corresponding refs, but is it worth it?
    }

  private:
    const RpcConnectionState& connectionState;

    kj::MutexGuarded<kj::Vector<kj::Own<const kj::Refcounted>>> refs;
    // List of references that need to be held until the message is destroyed.
  };

  // =====================================================================================
  // RequestHook/PipelineHook/ResponseHook implementations

  class RpcRequest: public RequestHook {
  public:
    RpcRequest(const RpcConnectionState& connectionState, uint firstSegmentWordSize)
        : connectionState(connectionState),
          message(connectionState.connection->newOutgoingMessage(
              firstSegmentWordSize == 0 ? 0 : firstSegmentWordSize + messageSizeHint<rpc::Call>())),
          injector(kj::heap<CapInjectorImpl>(connectionState)),
          context(*injector),
          callBuilder(message->getBody().getAs<rpc::Message>().initCall()),
          paramsBuilder(context.imbue(callBuilder.getRequest())) {}

    inline ObjectPointer::Builder getRoot() {
      return paramsBuilder;
    }

    RemotePromise<ObjectPointer> send() override {
      auto paf = kj::newPromiseAndFulfiller<Response<ObjectPointer>>(connectionState.eventLoop);
      QuestionId questionId;

      {
        auto lock = connectionState.tables.lockExclusive();

        auto& question = lock->questions.next(questionId);

        callBuilder.setQuestionId(questionId);
        question.isStarted = true;
        question.paramCaps = kj::mv(injector);

        question.fulfiller = kj::mv(paf.fulfiller);
      }

      auto pipeline = kj::refcounted<RpcPipeline>(connectionState, questionId);

      // If the caller discards the pipeline without discarding the promise, we need the pipeline
      // to stay alive so that we don't cancel the call altogether.
      auto promiseWithPipelineRef = paf.promise.then(kj::mvCapture(pipeline->addRef(),
          [](kj::Own<const PipelineHook>&&, Response<ObjectPointer>&& response)
              -> Response<ObjectPointer> {
            return kj::mv(response);
          }));

      message->send();
      return RemotePromise<ObjectPointer>(
          kj::mv(promiseWithPipelineRef),
          ObjectPointer::Pipeline(kj::mv(pipeline)));
    }

  private:
    const RpcConnectionState& connectionState;
    kj::Own<OutgoingRpcMessage> message;
    kj::Own<CapInjectorImpl> injector;
    CapBuilderContext context;
    rpc::Call::Builder callBuilder;
    ObjectPointer::Builder paramsBuilder;
  };

  class RpcPipeline: public PipelineHook, public kj::Refcounted {
  public:
    RpcPipeline(const RpcConnectionState& connectionState, QuestionId questionId)
        : connectionState(connectionState), questionId(questionId) {}

    ~RpcPipeline() noexcept(false) {
      uint sizeHint = messageSizeHint<rpc::Finish>();
      KJ_IF_MAYBE(ce, capExtractor) {
        sizeHint += ce->retainedListSizeHint(true);
      }
      auto finishMessage = connectionState.connection->newOutgoingMessage(sizeHint);

      rpc::Finish::Builder builder = finishMessage->getBody().initAs<rpc::Message>().initFinish();

      builder.setQuestionId(questionId);

      KJ_IF_MAYBE(ce, capExtractor) {
        builder.adoptRetainedCaps(ce->finalizeRetainedCaps(
            Orphanage::getForMessageContaining(builder)));
      }

      finishMessage->send();

      {
        auto lock = connectionState.tables.lockExclusive();
        auto& question = KJ_ASSERT_NONNULL(lock->questions.find(questionId),
                                           "RpcPipeline had invalid questionId?");
        question.pipeline = nullptr;

        if (question.isReturned) {
          KJ_ASSERT(lock->questions.erase(questionId));
        }
      }
    }

    kj::Promise<Response<ObjectPointer>> getResponse();

    // implements PipelineHook ---------------------------------------

    kj::Own<const PipelineHook> addRef() const override {
      return kj::addRef(*this);
    }

    kj::Own<const ClientHook> getPipelinedCap(kj::ArrayPtr<const PipelineOp> ops) const override {
      auto copy = kj::heapArrayBuilder<PipelineOp>(ops.size());
      for (auto& op: ops) {
        copy.add(op);
      }
      return getPipelinedCap(copy.finish());
    }

    kj::Own<const ClientHook> getPipelinedCap(kj::Array<PipelineOp>&& ops) const override {
      return kj::refcounted<PromisedAnswerClient>(
          connectionState, questionId, kj::mv(ops), kj::addRef(*this));
    }

  private:
    const RpcConnectionState& connectionState;
    QuestionId questionId;
    kj::Maybe<CapExtractorImpl&> capExtractor;
  };

  class RpcResponse {
  public:
    RpcResponse(RpcConnectionState& connectionState,
                kj::Own<OutgoingRpcMessage>&& message,
                ObjectPointer::Builder results)
        : message(kj::mv(message)),
          injector(connectionState),
          context(injector),
          builder(context.imbue(results)) {}

    ObjectPointer::Builder getResults() {
      return builder;
    }

    void send() {
      message->send();
    }

  private:
    kj::Own<OutgoingRpcMessage> message;
    CapInjectorImpl injector;
    CapBuilderContext context;
    ObjectPointer::Builder builder;
  };

  // =====================================================================================
  // CallContextHook implementation

  class RpcCallContext final: public CallContextHook, public kj::Refcounted {
  public:
    RpcCallContext(RpcConnectionState& connectionState, QuestionId questionId,
                   kj::Own<IncomingRpcMessage>&& request, const ObjectPointer::Reader& params)
        : connectionState(connectionState),
          questionId(questionId),
          request(kj::mv(request)),
          requestCapExtractor(connectionState),
          requestCapContext(requestCapExtractor),
          params(requestCapContext.imbue(params)),
          returnMessage(nullptr) {}

    void sendReturn() {
      if (response == nullptr) getResults(1);  // force initialization of response

      returnMessage.setQuestionId(questionId);
      returnMessage.adoptRetainedCaps(requestCapExtractor.finalizeRetainedCaps(
          Orphanage::getForMessageContaining(returnMessage)));

      KJ_ASSERT_NONNULL(response)->send();
    }
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
        return r->get()->getResults();
      } else {
        auto message = connectionState.connection->newOutgoingMessage(
            firstSegmentWordSize == 0 ? 0 :
            firstSegmentWordSize + messageSizeHint<rpc::Return>() +
            requestCapExtractor.retainedListSizeHint(request == nullptr));
        returnMessage = message->getBody().initAs<rpc::Message>().initReturn();
        auto response = kj::heap<RpcResponse>(connectionState, kj::mv(message),
                                              returnMessage.getAnswer());
        auto results = response->getResults();
        this->response = kj::mv(response);
        return results;
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
    ObjectPointer::Reader params;

    kj::Maybe<kj::Own<RpcResponse>> response;
    rpc::Return::Builder returnMessage;
  };

  // =====================================================================================
  // Message handling

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
        auto message = connection->newOutgoingMessage(
            reader.totalSizeInWords() + messageSizeHint<void>());
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

        KJ_IF_MAYBE(ops, toPipelineOps(promisedAnswer.getTransform())) {
          capability = pipeline->getPipelinedCap(*ops);
        } else {
          // Exception already thrown.
          return;
        }
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
      auto message = connection->newOutgoingMessage(messageSizeHint<rpc::Release>());
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
