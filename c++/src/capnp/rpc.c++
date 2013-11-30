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
#include <kj/function.h>
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
  kj::Exception::Nature nature =
      exception.getIsCallersFault()
          ? kj::Exception::Nature::PRECONDITION
          : kj::Exception::Nature::LOCAL_BUG;

  kj::Exception::Durability durability;
  switch (exception.getDurability()) {
    default:
    case rpc::Exception::Durability::PERMANENT:
      durability = kj::Exception::Durability::PERMANENT;
      break;
    case rpc::Exception::Durability::TEMPORARY:
      durability = kj::Exception::Durability::TEMPORARY;
      break;
    case rpc::Exception::Durability::OVERLOADED:
      durability = kj::Exception::Durability::OVERLOADED;
      break;
  }

  return kj::Exception(nature, durability, "(remote)", 0,
                       kj::str("remote exception: ", exception.getReason()));
}

void fromException(const kj::Exception& exception, rpc::Exception::Builder builder) {
  // TODO(someday):  Indicate the remote server name as part of the stack trace.  Maybe even
  //   transmit stack traces?
  builder.setReason(exception.getDescription());
  builder.setIsCallersFault(exception.getNature() == kj::Exception::Nature::PRECONDITION);
  switch (exception.getDurability()) {
    case kj::Exception::Durability::PERMANENT:
      builder.setDurability(rpc::Exception::Durability::PERMANENT);
      break;
    case kj::Exception::Durability::TEMPORARY:
      builder.setDurability(rpc::Exception::Durability::TEMPORARY);
      break;
    case kj::Exception::Durability::OVERLOADED:
      builder.setDurability(rpc::Exception::Durability::OVERLOADED);
      break;
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

  kj::Maybe<T> erase(Id id) {
    // Remove an entry from the table and return it.  We return it so that the caller can be
    // careful to release it (possibly invoking arbitrary destructors) at a time that makes sense.
    if (id < slots.size() && slots[id] != nullptr) {
      kj::Maybe<T> toRelease = kj::mv(slots[id]);
      slots[id] = T();
      freeIds.push(id);
      return toRelease;
    } else {
      return nullptr;
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
  class PromisedAnswerClient;

public:
  RpcConnectionState(kj::Maybe<SturdyRefRestorerBase&> restorer,
                     kj::Own<VatNetworkBase::Connection>&& connection,
                     kj::Own<kj::PromiseFulfiller<void>>&& disconnectFulfiller)
      : restorer(restorer), connection(kj::mv(connection)),
        disconnectFulfiller(kj::mv(disconnectFulfiller)),
        tasks(*this) {
    tasks.add(messageLoop());
    resolutionChainTail = kj::refcounted<ResolutionChain>();
  }

  kj::Own<ClientHook> restore(ObjectPointer::Reader objectId) {
    QuestionId questionId;
    auto& question = questions.next(questionId);

    // We need a dummy paramCaps since null normally indicates that the question has completed.
    question.paramCaps = kj::heap<CapInjectorImpl>(*this);

    auto paf = kj::newPromiseAndFulfiller<kj::Promise<kj::Own<RpcResponse>>>();

    auto questionRef = kj::refcounted<QuestionRef>(*this, questionId, kj::mv(paf.fulfiller));
    question.selfRef = *questionRef;

    paf.promise.attach(kj::addRef(*questionRef));

    {
      auto message = connection->newOutgoingMessage(
          objectId.targetSizeInWords() + messageSizeHint<rpc::Restore>());

      auto builder = message->getBody().initAs<rpc::Message>().initRestore();
      builder.setQuestionId(questionId);
      builder.getObjectId().set(objectId);

      message->send();
    }

    auto pipeline = kj::refcounted<RpcPipeline>(*this, kj::mv(questionRef), kj::mv(paf.promise));

    return pipeline->getPipelinedCap(kj::Array<const PipelineOp>(nullptr));
  }

  void taskFailed(kj::Exception&& exception) override {
    KJ_LOG(ERROR, "Closing connection due to protocol error.", exception);
    disconnect(kj::mv(exception));
  }

  void disconnect(kj::Exception&& exception) {
    {
      // Carefully pull all the objects out of the tables prior to releasing them because their
      // destructors could come back and mess with the tables.
      kj::Vector<kj::Own<PipelineHook>> pipelinesToRelease;
      kj::Vector<kj::Own<ClientHook>> clientsToRelease;
      kj::Vector<kj::Own<CapInjectorImpl>> capInjectorsToRelease;
      kj::Vector<kj::Promise<kj::Own<RpcResponse>>> tailCallsToRelease;
      kj::Vector<kj::Promise<void>> resolveOpsToRelease;

      if (networkException != nullptr) {
        // Oops, already disconnected.
        return;
      }

      kj::Exception networkException(
          kj::Exception::Nature::NETWORK_FAILURE, kj::Exception::Durability::PERMANENT,
          __FILE__, __LINE__, kj::str("Disconnected: ", exception.getDescription()));

      // All current questions complete with exceptions.
      questions.forEach([&](QuestionId id, Question& question) {
        KJ_IF_MAYBE(questionRef, question.selfRef) {
          // QuestionRef still present.
          questionRef->reject(kj::cp(networkException));
        }
        KJ_IF_MAYBE(pc, question.paramCaps) {
          capInjectorsToRelease.add(kj::mv(*pc));
        }
      });

      answers.forEach([&](QuestionId id, Answer& answer) {
        KJ_IF_MAYBE(p, answer.pipeline) {
          pipelinesToRelease.add(kj::mv(*p));
        }

        KJ_IF_MAYBE(promise, answer.redirectedResults) {
          tailCallsToRelease.add(kj::mv(*promise));
        }

        KJ_IF_MAYBE(context, answer.callContext) {
          context->requestCancel();
        }

        KJ_IF_MAYBE(capInjector, answer.resultCaps) {
          capInjectorsToRelease.add(kj::mv(*capInjector));
        }
      });

      exports.forEach([&](ExportId id, Export& exp) {
        clientsToRelease.add(kj::mv(exp.clientHook));
        resolveOpsToRelease.add(kj::mv(exp.resolveOp));
        exp = Export();
      });

      imports.forEach([&](ExportId id, Import& import) {
        KJ_IF_MAYBE(f, import.promiseFulfiller) {
          f->get()->reject(kj::cp(networkException));
        }
      });

      embargoes.forEach([&](EmbargoId id, Embargo& embargo) {
        KJ_IF_MAYBE(f, embargo.fulfiller) {
          f->get()->reject(kj::cp(networkException));
        }
      });

      this->networkException = kj::mv(networkException);
    }

    {
      // Send an abort message.
      auto message = connection->newOutgoingMessage(
          messageSizeHint<void>() + exceptionSizeHint(exception));
      fromException(exception, message->getBody().getAs<rpc::Message>().initAbort());
      message->send();
    }

    // Indicate disconnect.
    disconnectFulfiller->fulfill();
  }

private:
  class ResolutionChain;
  class RpcClient;
  class ImportClient;
  class PromiseClient;
  class CapInjectorImpl;
  class CapExtractorImpl;
  class QuestionRef;
  class RpcPipeline;
  class RpcCallContext;
  class RpcResponse;

  // =======================================================================================
  // The Four Tables entry types
  //
  // We have to define these before we can define the class's fields.

  typedef uint32_t QuestionId;
  typedef uint32_t ExportId;

  struct Question {
    kj::Maybe<kj::Own<CapInjectorImpl>> paramCaps;
    // CapInjector from the parameter struct.  This will be released once the `Return` message is
    // received and `retainedCaps` processed.  (If this is non-null, then the call has not returned
    // yet.)

    kj::Maybe<QuestionRef&> selfRef;
    // The local QuestionRef, set to nullptr when it is destroyed, which is also when `Finish` is
    // sent.

    bool isTailCall = false;
    // Is this a tail call?  If so, we don't expect to receive results in the `Return`.

    inline bool operator==(decltype(nullptr)) const {
      return paramCaps == nullptr && selfRef == nullptr;
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

    kj::Maybe<kj::Own<CapInjectorImpl>> resultCaps;
    // Set when `Return` is sent, free when `Finish` is received.
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

  kj::Maybe<SturdyRefRestorerBase&> restorer;
  kj::Own<VatNetworkBase::Connection> connection;
  kj::Own<kj::PromiseFulfiller<void>> disconnectFulfiller;

  ExportTable<ExportId, Export> exports;
  ExportTable<QuestionId, Question> questions;
  ImportTable<QuestionId, Answer> answers;
  ImportTable<ExportId, Import> imports;
  // The Four Tables!
  // The order of the tables is important for correct destruction.

  std::unordered_map<ClientHook*, ExportId> exportsByCap;
  // Maps already-exported ClientHook objects to their ID in the export table.

  kj::Own<ResolutionChain> resolutionChainTail;
  // The end of the resolution chain.  This node actually isn't filled in yet, but it will be
  // filled in and the chain will be extended with a new node any time a `Resolve` is received.
  // CapExtractors need to hold a ref to resolutionChainTail to prevent resolved promises from
  // becoming invalid while the app is still processing the message.  See `ResolutionChain`.

  kj::Maybe<kj::Exception> networkException;
  // If the connection has failed, this is the exception describing the failure.  All future
  // calls should throw this exception.

  ExportTable<EmbargoId, Embargo> embargoes;
  // There are only four tables.  This definitely isn't a fifth table.  I don't know what you're
  // talking about.

  kj::TaskSet tasks;

  // =====================================================================================

  class ResolutionChain: public kj::Refcounted {
    // A chain of pending promise resolutions and export releases which may affect messages that
    // are still being processed.
    //
    // When a `Resolve` message comes in, we can't just handle it and then release the original
    // promise import all at once, because it's possible that the application is still processing
    // the `params` or `results` from a previous call, and that it will encounter an instance of
    // the promise as it does.  We need to hold off on the release until we've actually gotten
    // through all outstanding messages.
    //
    // Similarly, when a `Release` message comes in that causes an export's refcount to hit zero,
    // we can't actually release until all previous messages are consumed because any of them
    // could contain a `CapDescriptor` with `receiverHosted`.  Oh god, and I suppose
    // `receiverAnswer` is affected too.  So we have to delay cleanup of pipelines.  Sigh.
    //
    // To that end, we have the resolution chain.  Each time a `CapExtractorImpl` is created --
    // representing a message to be consumed by the application -- it takes a reference to the
    // current end of the chain.  When a `Resolve` message or whatever arrives, it is added to the
    // end of the chain, and thus all `CapExtractorImpl`s that exist at that point now hold a
    // reference to it, but new `CapExtractorImpl`s will not.  Once all references are dropped,
    // the original promise can be released.
    //
    // The connection state actually holds one instance of ResolutionChain which doesn't yet have
    // a promise attached to it, representing the end of the chain.  This is what allows a new
    // resolution to be "added to the end" and have existing `CapExtractorImpl`s suddenly be
    // holding a reference to it.

  public:
    kj::Own<ResolutionChain> addResolve(ExportId importId, kj::Own<ClientHook>&& replacement) {
      // Add the a new resolution to the chain.  Returns the new end-of-chain.
      value.init<ResolvedImport>(ResolvedImport { importId, kj::mv(replacement) });
      auto result = kj::refcounted<ResolutionChain>();
      next = kj::addRef(*result);
      return kj::mv(result);
    }

    kj::Maybe<ClientHook&> findImport(ExportId importId) {
      // Look for the given import ID in the resolution chain.

      ResolutionChain* ptr = this;

      while (ptr->value != nullptr) {
        if (ptr->value.is<ResolvedImport>()) {
          auto& ri = ptr->value.get<ResolvedImport>();
          if (ri.importId == importId) {
            return *ri.replacement;
          }
        }
        ptr = ptr->next;
      }

      return nullptr;
    }

    kj::Own<ResolutionChain> addRelease(ExportId exportId, kj::Own<ClientHook>&& cap) {
      // Add the a new release to the chain.  Returns the new end-of-chain.
      value.init<DelayedRelease>(DelayedRelease { exportId, kj::mv(cap) });
      auto result = kj::refcounted<ResolutionChain>();
      next = kj::addRef(*result);
      return kj::mv(result);
    }

    kj::Maybe<ClientHook&> findExport(ExportId exportId) {
      // Look for the given export ID in the resolution chain.

      ResolutionChain* ptr = this;

      while (ptr->value != nullptr) {
        if (ptr->value.is<DelayedRelease>()) {
          auto& ri = ptr->value.get<DelayedRelease>();
          if (ri.exportId == exportId) {
            return *ri.cap;
          }
        }
        ptr = ptr->next;
      }

      return nullptr;
    }

    kj::Own<ResolutionChain> addFinish(QuestionId answerId, kj::Own<PipelineHook>&& pipeline) {
      // Add the a new finish to the chain.  Returns the new end-of-chain.
      value.init<DelayedFinish>(DelayedFinish { answerId, kj::mv(pipeline) });
      auto result = kj::refcounted<ResolutionChain>();
      next = kj::addRef(*result);
      return kj::mv(result);
    }

    kj::Maybe<PipelineHook&> findPipeline(QuestionId answerId) {
      // Look for the given answer ID in the resolution chain.

      ResolutionChain* ptr = this;

      while (ptr->value != nullptr) {
        if (ptr->value.is<DelayedFinish>()) {
          auto& ri = ptr->value.get<DelayedFinish>();
          if (ri.answerId == answerId) {
            return *ri.pipeline;
          }
        }
        ptr = ptr->next;
      }

      return nullptr;
    }

  private:
    kj::Own<ResolutionChain> next;

    struct ResolvedImport {
      ExportId importId;
      kj::Own<ClientHook> replacement;
    };
    struct DelayedRelease {
      ExportId exportId;
      kj::Own<ClientHook> cap;
    };
    struct DelayedFinish {
      QuestionId answerId;
      kj::Own<PipelineHook> pipeline;
    };

    kj::OneOf<ResolvedImport, DelayedRelease, DelayedFinish> value;
  };

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

    Request<ObjectPointer, ObjectPointer> newCall(
        uint64_t interfaceId, uint16_t methodId, uint firstSegmentWordSize) override {
      auto request = kj::heap<RpcRequest>(
          *connectionState, firstSegmentWordSize, kj::addRef(*this));
      auto callBuilder = request->getCall();

      callBuilder.setInterfaceId(interfaceId);
      callBuilder.setMethodId(methodId);

      auto root = request->getRoot();
      return Request<ObjectPointer, ObjectPointer>(root, kj::mv(request));
    }

    VoidPromiseAndPipeline call(uint64_t interfaceId, uint16_t methodId,
                                kj::Own<CallContextHook>&& context) override {
      // Implement call() by copying params and results messages.

      auto params = context->getParams();

      size_t sizeHint = params.targetSizeInWords();

      // TODO(perf):  Extend targetSizeInWords() to include a capability count?  Here we increase
      //   the size by 1/16 to deal with cap descriptors possibly expanding.  See also in
      //   RpcRequest::send() and RpcCallContext::directTailCall().
      sizeHint += sizeHint / 16;

      // Don't overflow.
      if (uint(sizeHint) != sizeHint) {
        sizeHint = ~uint(0);
      }

      auto request = newCall(interfaceId, methodId, sizeHint);

      request.set(params);
      context->releaseParams();

      // We can and should propagate cancellation.
      context->allowAsyncCancellation();

      return context->directTailCall(RequestHook::from(kj::mv(request)));
    }

    kj::Own<ClientHook> addRef() override {
      return kj::addRef(*this);
    }
    const void* getBrand() override {
      return connectionState.get();
    }

  protected:
    kj::Own<RpcConnectionState> connectionState;
  };

  class ImportClient final: public RpcClient {
    // A ClientHook that wraps an entry in the import table.

  public:
    ImportClient(RpcConnectionState& connectionState, ExportId importId)
        : RpcClient(connectionState), importId(importId) {}

    ~ImportClient() noexcept(false) {
      unwindDetector.catchExceptionsIfUnwinding([&]() {
        // Remove self from the import table, if the table is still pointing at us.  (It's possible
        // that another thread attempted to obtain this import just as the destructor started, in
        // which case that other thread will have constructed a new ImportClient and placed it in
        // the import table.  Therefore, we must actually verify that the import table points at
        // this object.)
        KJ_IF_MAYBE(import, connectionState->imports.find(importId)) {
          KJ_IF_MAYBE(i, import->importClient) {
            if (i == this) {
              connectionState->imports.erase(importId);
            }
          }
        }

        // Send a message releasing our remote references.
        if (remoteRefcount > 0) {
          auto message = connectionState->connection->newOutgoingMessage(
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
      target.setExportedCap(importId);
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
    ExportId importId;

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
                  kj::Maybe<ExportId> importId)
        : RpcClient(connectionState),
          isResolved(false),
          cap(kj::mv(initial)),
          importId(importId),
          fork(eventual.fork()),
          resolveSelfPromise(fork.addBranch().then(
              [this](kj::Own<ClientHook>&& resolution) {
                resolve(kj::mv(resolution));
              }, [this](kj::Exception&& exception) {
                resolve(newBrokenCap(kj::mv(exception)));
              })) {
      // Create a client that starts out forwarding all calls to `initial` but, once `eventual`
      // resolves, will forward there instead.  In addition, `whenMoreResolved()` will return a fork
      // of `eventual`.  Note that this means the application could hold on to `eventual` even after
      // the `PromiseClient` is destroyed; `eventual` must therefore make sure to hold references to
      // anything that needs to stay alive in order to resolve it correctly (such as making sure the
      // import ID is not released).

      // Make any exceptions thrown from resolveSelfPromise go to the connection's TaskSet which
      // will cause the connection to be terminated.
      resolveSelfPromise = resolveSelfPromise.then(
          []() {}, [&](kj::Exception&& e) { connectionState.tasks.add(kj::mv(e)); });

      resolveSelfPromise.eagerlyEvaluate();
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

    Request<ObjectPointer, ObjectPointer> newCall(
        uint64_t interfaceId, uint16_t methodId, uint firstSegmentWordSize) override {
      receivedCall = true;
      return cap->newCall(interfaceId, methodId, firstSegmentWordSize);
    }

    VoidPromiseAndPipeline call(uint64_t interfaceId, uint16_t methodId,
                                kj::Own<CallContextHook>&& context) override {
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

    kj::Maybe<ExportId> importId;
    kj::ForkedPromise<kj::Own<ClientHook>> fork;

    // Keep this last, because the continuation uses *this, so it should be destroyed first to
    // ensure the continuation is not still running.
    kj::Promise<void> resolveSelfPromise;

    bool receivedCall = false;

    void resolve(kj::Own<ClientHook> replacement) {
      if (replacement->getBrand() != connectionState.get() && receivedCall) {
        // The new capability is hosted locally, not on the remote machine.  And, we had made calls
        // to the promise.  We need to make sure those calls echo back to us before we allow new
        // calls to go directly to the local capability, so we need to set a local embargo and send
        // a `Disembargo` to echo through the peer.

        auto message = connectionState->connection->newOutgoingMessage(
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
            kj::mvCapture(replacement, [this](kj::Own<ClientHook>&& replacement) {
              return kj::mv(replacement);
            }));

        // We need to queue up calls in the meantime, so we'll resolve ourselves to a local promise
        // client instead.
        replacement = newLocalPromiseClient(kj::mv(embargoPromise));

        // Send the `Disembargo`.
        message->send();
      }

      cap = replacement->addRef();
      isResolved = true;
    }
  };

  kj::Maybe<ExportId> writeDescriptor(ClientHook& cap, rpc::CapDescriptor::Builder descriptor) {
    // Write a descriptor for the given capability.  The tables must be locked by the caller and
    // passed in as a parameter.

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
        descriptor.setSenderHosted(exportId);

        KJ_IF_MAYBE(wrapped, inner->whenMoreResolved()) {
          // This is a promise.  Arrange for the `Resolve` message to be sent later.
          exp.resolveOp = resolveExportedPromise(exportId, kj::mv(*wrapped));
        }

        return exportId;
      }
    }
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

    auto result = promise.then(
        [this,exportId](kj::Own<ClientHook>&& resolution) -> kj::Promise<void> {
      // Successful resolution.

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
      auto message = connection->newOutgoingMessage(
          messageSizeHint<rpc::Resolve>() + sizeInWords<rpc::CapDescriptor>() + 16);
      auto resolve = message->getBody().initAs<rpc::Message>().initResolve();
      resolve.setPromiseId(exportId);
      writeDescriptor(*exp.clientHook, resolve.initCap());
      message->send();

      return kj::READY_NOW;
    }, [this,exportId](kj::Exception&& exception) {
      // send error resolution
      auto message = connection->newOutgoingMessage(
          messageSizeHint<rpc::Resolve>() + exceptionSizeHint(exception) + 8);
      auto resolve = message->getBody().initAs<rpc::Message>().initResolve();
      resolve.setPromiseId(exportId);
      fromException(exception, resolve.initException());
      message->send();
    });
    result.eagerlyEvaluate();
    return kj::mv(result);
  }

  // =====================================================================================
  // CapExtractor / CapInjector implementations

  class CapExtractorImpl final: public CapExtractor<rpc::CapDescriptor> {
    // Reads CapDescriptors from a received message.

  public:
    CapExtractorImpl(RpcConnectionState& connectionState,
                     kj::Own<ResolutionChain> resolutionChain)
        : connectionState(connectionState),
          resolutionChain(kj::mv(resolutionChain)) {}

    ~CapExtractorImpl() noexcept(false) {
      KJ_ASSERT(retainedCaps.size() == 0 || connectionState.networkException != nullptr,
                "CapExtractorImpl destroyed without getting a chance to retain the caps!") {
        break;
      }
    }

    void doneExtracting() {
      resolutionChain = nullptr;
    }

    uint retainedListSizeHint(bool final) {
      // Get the expected size of the retained caps list, in words.  If `final` is true, then it
      // is known that no more caps will be extracted after this point, so an exact value can be
      // returned.  Otherwise, the returned size includes room for error.

      // If `final` is true then there's no need to lock.  If it is false, then asynchronous
      // access is possible.  It's probably not worth taking the lock to look; we'll just return
      // a silly estimate.
      uint count = final ? retainedCaps.size() : 32;
      return (count * sizeof(ExportId) + (sizeof(word) - 1)) / sizeof(word);
    }

    struct FinalizedRetainedCaps {
      // List of capabilities extracted from this message which are to be retained past the
      // message's release.

      Orphan<List<ExportId>> exportList;
      // List of export IDs, to be placed in the Return/Finish message.

      kj::Vector<kj::Own<ClientHook>> refs;
      // List of ClientHooks which need to be kept live until the message is sent, to prevent
      // their premature release.
    };

    FinalizedRetainedCaps finalizeRetainedCaps(Orphanage orphanage) {
      // Build the list of export IDs found in this message which are to be retained past the
      // message's release.

      // Called on finalization, when all extractions have ceased, so we can skip the lock.
      kj::Vector<ExportId> retainedCaps = kj::mv(this->retainedCaps);
      kj::Vector<kj::Own<ClientHook>> refs(retainedCaps.size());

      auto actualRetained = retainedCaps.begin();
      for (ExportId importId: retainedCaps) {
        // Check if the import still exists under this ID.
        KJ_IF_MAYBE(import, connectionState.imports.find(importId)) {
          KJ_IF_MAYBE(ic, import->importClient) {
            // Import indeed still exists!  We'll return it in the retained caps, which means it
            // now has a new remote ref.
            ic->addRemoteRef();
            *actualRetained++ = importId;
            refs.add(kj::addRef(*ic));
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

      return FinalizedRetainedCaps { kj::mv(result), kj::mv(refs) };
    }

    static kj::Own<ClientHook> extractCapAndAddRef(
        RpcConnectionState& connectionState, rpc::CapDescriptor::Reader descriptor) {
      // Interpret the given capability descriptor and, if it is an import, immediately give it
      // a remote ref.  This is called when interpreting messages that have a CapabilityDescriptor
      // but do not have a corresponding response message where a list of retained caps is given.
      // In these cases, the cap is always assumed retained, and must be explicitly released.
      // For example, the 'Resolve' message contains a capability which is presumed to be retained.

      return extractCapImpl(connectionState, descriptor,
                            *connectionState.resolutionChainTail, nullptr);
    }

    // implements CapDescriptor ------------------------------------------------

    kj::Own<ClientHook> extractCap(rpc::CapDescriptor::Reader descriptor) override {
      return extractCapImpl(connectionState, descriptor, *resolutionChain, retainedCaps);
    }

  private:
    RpcConnectionState& connectionState;

    kj::Own<ResolutionChain> resolutionChain;
    // Reference to the resolution chain, which prevents any promises that might be extracted from
    // this message from being invalidated by `Resolve` messages before extraction is finished.
    // Simply holding on to the chain keeps the import table entries valid.

    kj::Vector<ExportId> retainedCaps;
    // Imports which we are responsible for retaining, should they still exist at the time that
    // this message is released.

    static kj::Own<ClientHook> extractCapImpl(
        RpcConnectionState& connectionState,
        rpc::CapDescriptor::Reader descriptor,
        ResolutionChain& resolutionChain,
        kj::Maybe<kj::Vector<ExportId>&> retainedCaps) {
      switch (descriptor.which()) {
        case rpc::CapDescriptor::SENDER_HOSTED:
        case rpc::CapDescriptor::SENDER_PROMISE: {
          ExportId importId = descriptor.getSenderHosted();

          // First check to see if this import ID is a promise that has resolved since when this
          // message was received.  In this case, the original import ID will already have been
          // dropped and could even have been reused for another capability.  Luckily, the
          // resolution chain holds the capability we actually want.
          KJ_IF_MAYBE(resolution, resolutionChain.findImport(importId)) {
            return resolution->addRef();
          }

          // No recent resolutions.  Check the import table then.
          auto& import = connectionState.imports[importId];
          KJ_IF_MAYBE(c, import.appClient) {
            // The import is already on the table.  Since this import already exists, we don't have
            // to take responsibility for retaining it.  We can just return the existing object and
            // be done with it.
            return kj::addRef(*c);
          }

          // No import for this ID exists currently, so create one.
          kj::Own<ImportClient> importClient =
              kj::refcounted<ImportClient>(connectionState, importId);
          import.importClient = *importClient;

          KJ_IF_MAYBE(rc, retainedCaps) {
            // We need to retain this import later if it still exists.
            rc->add(importId);
          } else {
            // Automatically increment the refcount.
            importClient->addRemoteRef();
          }

          kj::Own<RpcClient> result;
          if (descriptor.which() == rpc::CapDescriptor::SENDER_PROMISE) {
            // TODO(now):  Check for pending `Resolve` messages replacing this import ID, and if
            //   one exists, use that client instead.

            auto paf = kj::newPromiseAndFulfiller<kj::Own<ClientHook>>();
            import.promiseFulfiller = kj::mv(paf.fulfiller);
            paf.promise.attach(kj::addRef(*importClient));
            result = kj::refcounted<PromiseClient>(
                connectionState, kj::mv(importClient), kj::mv(paf.promise), importId);
          } else {
            result = kj::mv(importClient);
          }

          import.appClient = *result;

          return kj::mv(result);
        }

        case rpc::CapDescriptor::RECEIVER_HOSTED: {
          // First check to see if this export ID was recently released.
          KJ_IF_MAYBE(cap, resolutionChain.findExport(descriptor.getReceiverHosted())) {
            return cap->addRef();
          }

          KJ_IF_MAYBE(exp, connectionState.exports.find(descriptor.getReceiverHosted())) {
            return exp->clientHook->addRef();
          }
          return newBrokenCap("invalid 'receiverHosted' export ID");
        }

        case rpc::CapDescriptor::RECEIVER_ANSWER: {
          auto promisedAnswer = descriptor.getReceiverAnswer();

          PipelineHook* pipeline;

          // First check to see if this question ID was recently finished.
          KJ_IF_MAYBE(p, resolutionChain.findPipeline(promisedAnswer.getQuestionId())) {
            pipeline = p;
          } else {
            KJ_IF_MAYBE(answer, connectionState.answers.find(promisedAnswer.getQuestionId())) {
              if (answer->active) {
                KJ_IF_MAYBE(p, answer->pipeline) {
                  pipeline = p->get();
                }
              }
            }

            return newBrokenCap("invalid 'receiverAnswer'");
          }

          KJ_IF_MAYBE(ops, toPipelineOps(promisedAnswer.getTransform())) {
            return pipeline->getPipelinedCap(*ops);
          } else {
            return newBrokenCap("unrecognized pipeline ops");
          }
        }

        case rpc::CapDescriptor::THIRD_PARTY_HOSTED:
          return newBrokenCap("three-way introductions not implemented");

        default:
          return newBrokenCap("unknown CapDescriptor type");
      }
    }
  };

  // -----------------------------------------------------------------

  class CapInjectorImpl final: public CapInjector<rpc::CapDescriptor> {
    // Write CapDescriptors into a message as it is being built, before sending it.

  public:
    CapInjectorImpl(RpcConnectionState& connectionState)
        : connectionState(connectionState) {}
    ~CapInjectorImpl() noexcept(false) {
      unwindDetector.catchExceptionsIfUnwinding([&]() {
        if (connectionState.networkException == nullptr) {
          for (auto exportId: exports) {
            connectionState.releaseExport(exportId, 1);
          }
        }
      });
    }

    bool hasCaps() {
      // Return true if the message contains any capabilities.  (If not, it may be possible to
      // release earlier.)

      return !caps.empty();
    }

    void finishDescriptors() {
      // Finish writing all of the CapDescriptors.  Must be called with the tables locked, and the
      // message must be sent before the tables are unlocked.

      exports = kj::Vector<ExportId>(caps.size());

      for (auto& entry: caps) {
        // If maybeExportId is inlined, GCC 4.7 reports a spurious "may be used uninitialized"
        // error (GCC 4.8 and Clang do not complain).
        auto maybeExportId = connectionState.writeDescriptor(
            *entry.second.cap, entry.second.builder);
        KJ_IF_MAYBE(exportId, maybeExportId) {
          KJ_ASSERT(connectionState.exports.find(*exportId) != nullptr);
          exports.add(*exportId);
        }
      }
    }

    // implements CapInjector ----------------------------------------

    void injectCap(rpc::CapDescriptor::Builder descriptor, kj::Own<ClientHook>&& cap) override {
      auto result = caps.insert(std::make_pair(
          identity(descriptor), CapInfo(descriptor, kj::mv(cap))));
      KJ_REQUIRE(result.second, "A cap has already been injected at this location.") {
        break;
      }
    }
    kj::Own<ClientHook> getInjectedCap(rpc::CapDescriptor::Reader descriptor) override {
      auto iter = caps.find(identity(descriptor));
      KJ_REQUIRE(iter != caps.end(), "getInjectedCap() called on descriptor I didn't write.");
      return iter->second.cap->addRef();
    }
    void dropCap(rpc::CapDescriptor::Reader descriptor) override {
      caps.erase(identity(descriptor));
    }

  private:
    RpcConnectionState& connectionState;

    struct CapInfo {
      rpc::CapDescriptor::Builder builder;
      kj::Own<ClientHook> cap;

      CapInfo(): builder(nullptr) {}

      CapInfo(rpc::CapDescriptor::Builder& builder, kj::Own<ClientHook>&& cap)
          : builder(builder), cap(kj::mv(cap)) {}

      CapInfo(const CapInfo& other) = delete;
      // Work around problem where std::pair complains about the copy constructor requiring a
      // non-const argument due to `builder` inheriting kj::DisableConstCopy.  The copy constructor
      // should be deleted anyway because `cap` is not copyable.

      CapInfo(CapInfo&& other) = default;
    };

    std::map<const void*, CapInfo> caps;
    // Maps CapDescriptor locations to embedded caps.  The descriptors aren't actually filled in
    // until just before the message is sent.

    kj::Vector<ExportId> exports;
    // IDs of objects exported during finishDescriptors().  These will need to be released later.

    kj::UnwindDetector unwindDetector;

    static const void* identity(const rpc::CapDescriptor::Reader& desc) {
      // TODO(cleanup):  Don't rely on internal APIs here.
      return _::PointerHelpers<rpc::CapDescriptor>::getInternalReader(desc).getLocation();
    }
  };

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
        if (connectionState->networkException != nullptr) {
          return;
        }

        // Send the "Finish" message.
        {
          uint retainedListSizeHint = resultCaps
              .map([](kj::Own<CapExtractorImpl>& caps) { return caps->retainedListSizeHint(true); })
              .orDefault(0);
          auto message = connectionState->connection->newOutgoingMessage(
              messageSizeHint<rpc::Finish>() + retainedListSizeHint);
          auto builder = message->getBody().getAs<rpc::Message>().initFinish();
          builder.setQuestionId(id);

          CapExtractorImpl::FinalizedRetainedCaps retainedCaps;
          KJ_IF_MAYBE(caps, resultCaps) {
            retainedCaps = caps->get()->finalizeRetainedCaps(
                Orphanage::getForMessageContaining(builder));
          }
          builder.adoptRetainedCaps(kj::mv(retainedCaps.exportList));
          message->send();
        }

        // Check if the question has returned and, if so, remove it from the table.
        // Remove question ID from the table.  Must do this *after* sending `Finish` to ensure that
        // the ID is not re-allocated before the `Finish` message can be sent.
        auto& question = KJ_ASSERT_NONNULL(
            connectionState->questions.find(id), "Question ID no longer on table?");
        if (question.paramCaps == nullptr) {
          // Call has already returned, so we can now remove it from the table.
          connectionState->questions.erase(id);
        } else {
          question.selfRef = nullptr;
        }
      });
    }

    inline QuestionId getId() const { return id; }

    CapExtractorImpl& getCapExtractor(kj::Own<ResolutionChain> resolutionChain) {
      auto extractor = kj::heap<CapExtractorImpl>(*connectionState, kj::mv(resolutionChain));
      auto& result = *extractor;
      resultCaps = kj::mv(extractor);
      return result;
    }

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
    kj::Maybe<kj::Own<CapExtractorImpl>> resultCaps;
    kj::UnwindDetector unwindDetector;
  };

  class RpcRequest final: public RequestHook {
  public:
    RpcRequest(RpcConnectionState& connectionState, uint firstSegmentWordSize,
               kj::Own<RpcClient>&& target)
        : connectionState(kj::addRef(connectionState)),
          target(kj::mv(target)),
          message(connectionState.connection->newOutgoingMessage(
              firstSegmentWordSize == 0 ? 0 :
                  firstSegmentWordSize + messageSizeHint<rpc::Call>() + MESSAGE_TARGET_SIZE_HINT)),
          injector(kj::heap<CapInjectorImpl>(connectionState)),
          context(*injector),
          callBuilder(message->getBody().getAs<rpc::Message>().initCall()),
          paramsBuilder(context.imbue(callBuilder.getParams())) {}

    inline ObjectPointer::Builder getRoot() {
      return paramsBuilder;
    }
    inline rpc::Call::Builder getCall() {
      return callBuilder;
    }

    RemotePromise<ObjectPointer> send() override {
      SendInternalResult sendResult;

      KJ_IF_MAYBE(e, connectionState->networkException) {
        return RemotePromise<ObjectPointer>(
            kj::Promise<Response<ObjectPointer>>(kj::cp(*e)),
            ObjectPointer::Pipeline(newBrokenPipeline(kj::cp(*e))));
      }

      KJ_IF_MAYBE(redirect, target->writeTarget(callBuilder.getTarget())) {
        // Whoops, this capability has been redirected while we were building the request!
        // We'll have to make a new request and do a copy.  Ick.

        size_t sizeHint = paramsBuilder.targetSizeInWords();

        // TODO(perf):  See TODO in RpcClient::call() about why we need to inflate the size a bit.
        sizeHint += sizeHint / 16;

        // Don't overflow.
        if (uint(sizeHint) != sizeHint) {
          sizeHint = ~uint(0);
        }

        auto replacement = redirect->get()->newCall(
            callBuilder.getInterfaceId(), callBuilder.getMethodId(), sizeHint);
        replacement.set(paramsBuilder);
        return replacement.send();
      } else {
        sendResult = sendInternal(false);
      }

      auto forkedPromise = sendResult.promise.fork();

      // The pipeline must get notified of resolution before the app does to maintain ordering.
      auto pipeline = kj::refcounted<RpcPipeline>(
          *connectionState, kj::mv(sendResult.questionRef), forkedPromise.addBranch());

      auto appPromise = forkedPromise.addBranch().then(
          [=](kj::Own<RpcResponse>&& response) {
            auto reader = response->getResults();
            return Response<ObjectPointer>(reader, kj::mv(response));
          });

      return RemotePromise<ObjectPointer>(
          kj::mv(appPromise),
          ObjectPointer::Pipeline(kj::mv(pipeline)));
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

      if (connectionState->networkException != nullptr) {
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
    kj::Own<CapInjectorImpl> injector;
    CapBuilderContext context;
    rpc::Call::Builder callBuilder;
    ObjectPointer::Builder paramsBuilder;

    struct SendInternalResult {
      kj::Own<QuestionRef> questionRef;
      kj::Promise<kj::Own<RpcResponse>> promise = nullptr;
    };

    SendInternalResult sendInternal(bool isTailCall) {
      injector->finishDescriptors();

      auto paf = kj::newPromiseAndFulfiller<kj::Promise<kj::Own<RpcResponse>>>();
      QuestionId questionId;
      auto& question = connectionState->questions.next(questionId);

      callBuilder.setQuestionId(questionId);
      if (isTailCall) {
        callBuilder.getSendResultsTo().setYourself();
      }

      question.paramCaps = kj::mv(injector);
      question.isTailCall = isTailCall;

      SendInternalResult result;

      result.questionRef = kj::refcounted<QuestionRef>(
          *connectionState, questionId, kj::mv(paf.fulfiller));
      question.selfRef = *result.questionRef;

      message->send();

      result.promise = kj::mv(paf.promise);
      result.promise.attach(kj::addRef(*result.questionRef));

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
              })) {
      // Construct a new RpcPipeline.

      resolveSelfPromise.eagerlyEvaluate();
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
    kj::Maybe<CapExtractorImpl&> capExtractor;
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
    virtual ObjectPointer::Reader getResults() = 0;
    virtual kj::Own<RpcResponse> addRef() = 0;
  };

  class RpcResponseImpl final: public RpcResponse, public kj::Refcounted {
  public:
    RpcResponseImpl(RpcConnectionState& connectionState,
                    kj::Own<QuestionRef>&& questionRef,
                    kj::Own<IncomingRpcMessage>&& message,
                    ObjectPointer::Reader results,
                    kj::Own<ResolutionChain>&& resolutionChain)
        : connectionState(kj::addRef(connectionState)),
          message(kj::mv(message)),
          context(questionRef->getCapExtractor(kj::mv(resolutionChain))),
          reader(context.imbue(results)),
          questionRef(kj::mv(questionRef)) {}

    ObjectPointer::Reader getResults() override {
      return reader;
    }

    kj::Own<RpcResponse> addRef() override {
      return kj::addRef(*this);
    }

  private:
    kj::Own<RpcConnectionState> connectionState;
    kj::Own<IncomingRpcMessage> message;
    CapReaderContext context;
    ObjectPointer::Reader reader;
    kj::Own<QuestionRef> questionRef;
  };

  // =====================================================================================
  // CallContextHook implementation

  class RpcServerResponse {
  public:
    virtual ObjectPointer::Builder getResultsBuilder() = 0;
  };

  class RpcServerResponseImpl final: public RpcServerResponse {
  public:
    RpcServerResponseImpl(RpcConnectionState& connectionState,
                          kj::Own<OutgoingRpcMessage>&& message,
                          ObjectPointer::Builder results)
        : message(kj::mv(message)),
          injector(kj::heap<CapInjectorImpl>(connectionState)),
          context(*injector),
          builder(context.imbue(results)) {}

    ObjectPointer::Builder getResultsBuilder() override {
      return builder;
    }

    kj::Own<CapInjectorImpl> send() {
      injector->finishDescriptors();
      message->send();
      return kj::mv(injector);
    }

  private:
    kj::Own<OutgoingRpcMessage> message;
    kj::Own<CapInjectorImpl> injector;
    CapBuilderContext context;
    ObjectPointer::Builder builder;
  };

  class LocallyRedirectedRpcResponse final
      : public RpcResponse, public RpcServerResponse, public kj::Refcounted{
  public:
    LocallyRedirectedRpcResponse(uint firstSegmentWordSize)
        : message(firstSegmentWordSize == 0 ?
            SUGGESTED_FIRST_SEGMENT_WORDS : firstSegmentWordSize + 1) {}

    ObjectPointer::Builder getResultsBuilder() override {
      return message.getRoot();
    }

    ObjectPointer::Reader getResults() override {
      return message.getRootReader();
    }

    kj::Own<RpcResponse> addRef() override {
      return kj::addRef(*this);
    }

  private:
    LocalMessage message;
  };

  class RpcCallContext final: public CallContextHook, public kj::Refcounted {
  public:
    RpcCallContext(RpcConnectionState& connectionState, QuestionId questionId,
                   kj::Own<IncomingRpcMessage>&& request, const ObjectPointer::Reader& params,
                   kj::Own<ResolutionChain> resolutionChain, bool redirectResults,
                   kj::Own<kj::PromiseFulfiller<void>>&& cancelFulfiller)
        : connectionState(kj::addRef(connectionState)),
          questionId(questionId),
          request(kj::mv(request)),
          requestCapExtractor(connectionState, kj::mv(resolutionChain)),
          requestCapContext(requestCapExtractor),
          params(requestCapContext.imbue(params)),
          returnMessage(nullptr),
          redirectResults(redirectResults),
          cancelFulfiller(kj::mv(cancelFulfiller)) {}

    ~RpcCallContext() noexcept(false) {
      if (isFirstResponder()) {
        // We haven't sent a return yet, so we must have been canceled.  Send a cancellation return.
        unwindDetector.catchExceptionsIfUnwinding([&]() {
          // Don't send anything if the connection is broken.
          if (connectionState->networkException == nullptr) {
            auto message = connectionState->connection->newOutgoingMessage(
                requestCapExtractor.retainedListSizeHint(true) + messageSizeHint<rpc::Return>());
            auto builder = message->getBody().initAs<rpc::Message>().initReturn();

            builder.setQuestionId(questionId);
            auto retainedCaps = requestCapExtractor.finalizeRetainedCaps(
                Orphanage::getForMessageContaining(builder));
            builder.adoptRetainedCaps(kj::mv(retainedCaps.exportList));

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

      if (response == nullptr) getResults(1);  // force initialization of response

      // Note that the context needs to keep its own reference to the response so that it doesn't
      // get GC'd until the PipelineHook drops its reference to the context.
      return kj::downcast<LocallyRedirectedRpcResponse>(*KJ_ASSERT_NONNULL(response)).addRef();
    }

    void sendReturn() {
      KJ_ASSERT(!redirectResults);
      if (isFirstResponder()) {
        if (response == nullptr) getResults(1);  // force initialization of response

        returnMessage.setQuestionId(questionId);
        auto retainedCaps = requestCapExtractor.finalizeRetainedCaps(
            Orphanage::getForMessageContaining(returnMessage));
        returnMessage.adoptRetainedCaps(kj::mv(retainedCaps.exportList));

        cleanupAnswerTable(kj::downcast<RpcServerResponseImpl>(
            *KJ_ASSERT_NONNULL(response)).send(), true);
      }
    }
    void sendErrorReturn(kj::Exception&& exception) {
      KJ_ASSERT(!redirectResults);
      if (isFirstResponder()) {
        auto message = connectionState->connection->newOutgoingMessage(
            messageSizeHint<rpc::Return>() + requestCapExtractor.retainedListSizeHint(true) +
            exceptionSizeHint(exception));
        auto builder = message->getBody().initAs<rpc::Message>().initReturn();

        builder.setQuestionId(questionId);
        auto retainedCaps = requestCapExtractor.finalizeRetainedCaps(
            Orphanage::getForMessageContaining(builder));
        builder.adoptRetainedCaps(kj::mv(retainedCaps.exportList));
        fromException(exception, builder.initException());

        message->send();
        cleanupAnswerTable(nullptr, true);
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

    ObjectPointer::Reader getParams() override {
      KJ_REQUIRE(request != nullptr, "Can't call getParams() after releaseParams().");
      return params;
    }
    void releaseParams() override {
      request = nullptr;
      requestCapExtractor.doneExtracting();
    }
    ObjectPointer::Builder getResults(uint firstSegmentWordSize) override {
      KJ_IF_MAYBE(r, response) {
        return r->get()->getResultsBuilder();
      } else {
        kj::Own<RpcServerResponse> response;

        if (redirectResults) {
          response = kj::refcounted<LocallyRedirectedRpcResponse>(firstSegmentWordSize);
        } else {
          auto message = connectionState->connection->newOutgoingMessage(
              firstSegmentWordSize == 0 ? 0 :
              firstSegmentWordSize + messageSizeHint<rpc::Return>() +
              requestCapExtractor.retainedListSizeHint(request == nullptr));
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
        f->get()->fulfill(ObjectPointer::Pipeline(kj::mv(result.pipeline)));
      }
      return kj::mv(result.promise);
    }
    ClientHook::VoidPromiseAndPipeline directTailCall(kj::Own<RequestHook>&& request) override {
      KJ_REQUIRE(response == nullptr,
                 "Can't call tailCall() after initializing the results struct.");
      KJ_REQUIRE(this->request == nullptr,
                 "Must call releaseParams() before tailCall().");

      if (request->getBrand() == connectionState.get() && !redirectResults) {
        // The tail call is headed towards the peer that called us in the first place, so we can
        // optimize out the return trip.

        KJ_IF_MAYBE(tailInfo, kj::downcast<RpcRequest>(*request).tailSend()) {
          if (isFirstResponder()) {
            auto message = connectionState->connection->newOutgoingMessage(
                messageSizeHint<rpc::Return>() + requestCapExtractor.retainedListSizeHint(true));
            auto builder = message->getBody().initAs<rpc::Message>().initReturn();

            builder.setQuestionId(questionId);
            auto retainedCaps = requestCapExtractor.finalizeRetainedCaps(
                Orphanage::getForMessageContaining(builder));
            builder.adoptRetainedCaps(kj::mv(retainedCaps.exportList));
            builder.setTakeFromOtherAnswer(tailInfo->questionId);

            message->send();
            cleanupAnswerTable(nullptr, false);
          }
          return { kj::mv(tailInfo->promise), kj::mv(tailInfo->pipeline) };
        }
      }

      // Just forwarding to another local call.
      auto promise = request->send();

      // Wait for response.
      auto voidPromise = promise.then([this](Response<ObjectPointer>&& tailResponse) {
        // Copy the response.
        // TODO(perf):  It would be nice if we could somehow make the response get built in-place
        //   but requires some refactoring.
        size_t sizeHint = tailResponse.targetSizeInWords();
        sizeHint += sizeHint / 16;  // see TODO in RpcClient::call().
        getResults(sizeHint).set(tailResponse);
      });

      return { kj::mv(voidPromise), PipelineHook::from(kj::mv(promise)) };
    }
    kj::Promise<ObjectPointer::Pipeline> onTailCall() override {
      auto paf = kj::newPromiseAndFulfiller<ObjectPointer::Pipeline>();
      tailCallPipelineFulfiller = kj::mv(paf.fulfiller);
      return kj::mv(paf.promise);
    }
    void allowAsyncCancellation() override {
      // TODO(cleanup):  We need to drop the request because it is holding on to the resolution
      //   chain which in turn holds on to the pipeline which holds on to this object thus
      //   preventing cancellation from working.  This is a bit silly because obviously our
      //   request couldn't contain PromisedAnswers referring to itself, but currently the chain
      //   is a linear list and we have no way to tell that a reference to the chain taken before
      //   a call started doesn't really need to hold the call open.  To fix this we'd presumably
      //   need to make the answer table snapshot-able and have CapExtractorImpl take a snapshot
      //   at creation.
      KJ_REQUIRE(request == nullptr, "Must call releaseParams() before allowAsyncCancellation().");

      bool previouslyRequestedButNotAllowed = cancellationFlags == CANCEL_REQUESTED;
      cancellationFlags |= CANCEL_ALLOWED;

      if (previouslyRequestedButNotAllowed) {
        // We just set CANCEL_ALLOWED, and CANCEL_REQUESTED was already set previously.  Initiate
        // the cancellation.
        cancelFulfiller->fulfill();
      }
    }
    bool isCanceled() override {
      return cancellationFlags & CANCEL_REQUESTED;
    }
    kj::Own<CallContextHook> addRef() override {
      return kj::addRef(*this);
    }

  private:
    kj::Own<RpcConnectionState> connectionState;
    QuestionId questionId;

    // Request ---------------------------------------------

    kj::Maybe<kj::Own<IncomingRpcMessage>> request;
    CapExtractorImpl requestCapExtractor;
    CapReaderContext requestCapContext;
    ObjectPointer::Reader params;

    // Response --------------------------------------------

    kj::Maybe<kj::Own<RpcServerResponse>> response;
    rpc::Return::Builder returnMessage;
    bool redirectResults = false;
    bool responseSent = false;
    kj::Maybe<kj::Own<kj::PromiseFulfiller<ObjectPointer::Pipeline>>> tailCallPipelineFulfiller;

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

    void cleanupAnswerTable(kj::Maybe<kj::Own<CapInjectorImpl>> resultCaps,
                            bool freePipelineIfNoCaps) {
      // We need to remove the `callContext` pointer -- which points back to us -- from the
      // answer table.  Or we might even be responsible for removing the entire answer table
      // entry.

      if (cancellationFlags & CANCEL_REQUESTED) {
        // Already received `Finish` so it's our job to erase the table entry.
        connectionState->answers.erase(questionId);
      } else {
        // We just have to null out callContext.
        auto& answer = connectionState->answers[questionId];
        answer.callContext = nullptr;
        answer.resultCaps = kj::mv(resultCaps);

        // If the response has capabilities, we need to arrange to keep the CapInjector around
        // until the `Finish` message.
        // If the response has no capabilities in it, then we should also delete the pipeline
        // so that the context can be released sooner.
        if (freePipelineIfNoCaps &&
            !answer.resultCaps.map([](kj::Own<CapInjectorImpl>& i) { return i->hasCaps(); })
                              .orDefault(false)) {
          answer.pipeline = nullptr;
        }
      }
    }
  };

  // =====================================================================================
  // Message handling

  kj::Promise<void> messageLoop() {
    return connection->receiveIncomingMessage().then(
        [this](kj::Maybe<kj::Own<IncomingRpcMessage>>&& message) {
      KJ_IF_MAYBE(m, message) {
        handleMessage(kj::mv(*m));
      } else {
        disconnect(kj::Exception(
            kj::Exception::Nature::PRECONDITION, kj::Exception::Durability::PERMANENT,
            __FILE__, __LINE__, kj::str("Peer disconnected.")));
      }
    }).then([this]() {
      // No exceptions; continue loop.
      //
      // (We do this in a separate continuation to handle the case where exceptions are
      // disabled.)
      tasks.add(messageLoop());
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

      case rpc::Message::RESTORE:
        handleRestore(kj::mv(message), reader.getRestore());
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

  void handleUnimplemented(const rpc::Message::Reader& message) {
    switch (message.which()) {
      case rpc::Message::RESOLVE: {
        auto cap = message.getResolve().getCap();
        switch (cap.which()) {
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

    auto cancelPaf = kj::newPromiseAndFulfiller<void>();

    QuestionId questionId = call.getQuestionId();

    // Note:  resolutionChainTail couldn't possibly be changing here because we only handle one
    //   message at a time, so we can hold off locking the tables for a bit longer.
    auto context = kj::refcounted<RpcCallContext>(
        *this, questionId, kj::mv(message), call.getParams(),
        kj::addRef(*resolutionChainTail),
        redirectResults, kj::mv(cancelPaf.fulfiller));

    // No more using `call` after this point!

    {
      auto& answer = answers[questionId];

      KJ_REQUIRE(!answer.active, "questionId is already in use") {
        return;
      }

      answer.active = true;
      answer.callContext = *context;
    }

    auto promiseAndPipeline = capability->call(
        call.getInterfaceId(), call.getMethodId(), context->addRef());

    // Things may have changed -- in particular if call() immediately called
    // context->directTailCall().

    {
      auto& answer = answers[questionId];

      answer.pipeline = kj::mv(promiseAndPipeline.pipeline);

      if (redirectResults) {
        auto resultsPromise = promiseAndPipeline.promise.then(
            kj::mvCapture(context, [](kj::Own<RpcCallContext>&& context) {
              return context->consumeRedirectedResponse();
            }));

        // If the call that later picks up `redirectedResults` decides to discard it, we need to
        // make sure our call is not itself canceled unless it has called allowAsyncCancellation().
        // So we fork the promise and join one branch with the cancellation promise, in order to
        // hold on to it.
        auto forked = resultsPromise.fork();
        answer.redirectedResults = forked.addBranch();

        auto promise = kj::mv(cancelPaf.promise);
        promise.exclusiveJoin(forked.addBranch().then([](kj::Own<RpcResponse>&&){}));
        kj::EventLoop::current().daemonize(kj::mv(promise));
      } else {
        // Hack:  Both the success and error continuations need to use the context.  We could
        //   refcount, but both will be destroyed at the same time anyway.
        RpcCallContext* contextPtr = context;

        auto promise = promiseAndPipeline.promise.then(
            [contextPtr]() {
              contextPtr->sendReturn();
            }, [contextPtr](kj::Exception&& exception) {
              contextPtr->sendErrorReturn(kj::mv(exception));
            }).then([]() {}, [&](kj::Exception&& exception) {
              // Handle exceptions that occur in sendReturn()/sendErrorReturn().
              taskFailed(kj::mv(exception));
            });
        promise.attach(kj::mv(context));
        promise.exclusiveJoin(kj::mv(cancelPaf.promise));
        kj::EventLoop::current().daemonize(kj::mv(promise));
      }
    }
  }

  kj::Maybe<kj::Own<ClientHook>> getMessageTarget(const rpc::MessageTarget::Reader& target) {
    switch (target.which()) {
      case rpc::MessageTarget::EXPORTED_CAP: {
        KJ_IF_MAYBE(exp, exports.find(target.getExportedCap())) {
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
          KJ_FAIL_REQUIRE("PromisedAnswer.questionId is already finished or contained no "
                          "capabilities.") {
            return nullptr;
          }
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
    kj::Own<CapInjectorImpl> paramCapsToRelease;
    kj::Maybe<kj::Promise<kj::Own<RpcResponse>>> promiseToRelease;

    KJ_IF_MAYBE(question, questions.find(ret.getQuestionId())) {
      KJ_REQUIRE(question->paramCaps != nullptr, "Duplicate Return.") { return; }

      KJ_IF_MAYBE(pc, question->paramCaps) {
        // Release these later, after we're done with the answer table.
        paramCapsToRelease = kj::mv(*pc);
      } else {
        KJ_FAIL_REQUIRE("Duplicate return.") { return; }
      }

      for (ExportId retained: ret.getRetainedCaps()) {
        KJ_IF_MAYBE(exp, exports.find(retained)) {
          ++exp->refcount;
        } else {
          KJ_FAIL_REQUIRE("Invalid export ID in Return.retainedCaps list.") { return; }
        }
      }

      KJ_IF_MAYBE(questionRef, question->selfRef) {
        switch (ret.which()) {
          case rpc::Return::RESULTS:
            KJ_REQUIRE(!question->isTailCall,
                "Tail call `Return` must set `resultsSentElsewhere`, not `results`.") {
              return;
            }

            questionRef->fulfill(kj::refcounted<RpcResponseImpl>(
                *this, kj::addRef(*questionRef), kj::mv(message), ret.getResults(),
                kj::addRef(*resolutionChainTail)));
            break;

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

          case rpc::Return::TAKE_FROM_OTHER_ANSWER:
            KJ_IF_MAYBE(answer, answers.find(ret.getTakeFromOtherAnswer())) {
              KJ_IF_MAYBE(response, answer->redirectedResults) {
                questionRef->fulfill(kj::mv(*response));
              } else {
                KJ_FAIL_REQUIRE("`Return.takeFromOtherAnswer` referenced a call that did not "
                                "use `sendResultsTo.yourself`.") { return; }
              }
            } else {
              KJ_FAIL_REQUIRE("`Return.takeFromOtherAnswer` had invalid answer ID.") { return; }
            }

            break;

          default:
            KJ_FAIL_REQUIRE("Unknown 'Return' type.") { return; }
        }
      } else {
        if (ret.isTakeFromOtherAnswer()) {
          // Be sure to release the tail call's promise.
          KJ_IF_MAYBE(answer, answers.find(ret.getTakeFromOtherAnswer())) {
            promiseToRelease = kj::mv(answer->redirectedResults);
          }
        }

        // Looks like this question was canceled earlier, so `Finish` was already sent.  We can go
        // ahead and delete it from the table.
        questions.erase(ret.getQuestionId());
      }

    } else {
      KJ_FAIL_REQUIRE("Invalid question ID in Return message.") { return; }
    }
  }

  void handleFinish(const rpc::Finish::Reader& finish) {
    // Delay release of these things until return so that transitive destructors don't accidentally
    // modify the answer table and invalidate our pointer into it.
    kj::Own<ResolutionChain> chainToRelease;
    Answer answerToRelease;

    for (ExportId retained: finish.getRetainedCaps()) {
      KJ_IF_MAYBE(exp, exports.find(retained)) {
        ++exp->refcount;
      } else {
        KJ_FAIL_REQUIRE("Invalid export ID in Return.retainedCaps list.") { return; }
      }
    }

    KJ_IF_MAYBE(answer, answers.find(finish.getQuestionId())) {
      KJ_REQUIRE(answer->active, "'Finish' for invalid question ID.") { return; }

      // `Finish` indicates that no further pipeline requests will be made.
      // However, previously-sent messages that are still being processed could still refer to this
      // pipeline, so we have to move it into the resolution chain.
      KJ_IF_MAYBE(p, kj::mv(answer->pipeline)) {
        chainToRelease = kj::mv(resolutionChainTail);
        resolutionChainTail = chainToRelease->addFinish(finish.getQuestionId(), kj::mv(*p));
      }

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
    kj::Own<ResolutionChain> oldResolutionChainTail;  // must be freed outside of lock

    kj::Own<ClientHook> replacement;

    // Extract the replacement capability.
    switch (resolve.which()) {
      case rpc::Resolve::CAP:
        replacement = CapExtractorImpl::extractCapAndAddRef(*this, resolve.getCap());
        break;

      case rpc::Resolve::EXCEPTION:
        replacement = newBrokenCap(toException(resolve.getException()));
        break;

      default:
        KJ_FAIL_REQUIRE("Unknown 'Resolve' type.") { return; }
    }

    // Extend the resolution chain.
    oldResolutionChainTail = kj::mv(resolutionChainTail);
    resolutionChainTail = oldResolutionChainTail->addResolve(
        resolve.getPromiseId(), kj::mv(replacement));

    // If the import is on the table, fulfill it.
    KJ_IF_MAYBE(import, imports.find(resolve.getPromiseId())) {
      KJ_IF_MAYBE(fulfiller, import->promiseFulfiller) {
        // OK, this is in fact an unfulfilled promise!
        fulfiller->get()->fulfill(kj::mv(replacement));
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
        auto client = kj::mv(exp->clientHook);
        exports.erase(id);
        resolutionChainTail = resolutionChainTail->addRelease(id, kj::mv(client));
      }
    } else {
      KJ_FAIL_REQUIRE("Tried to release invalid export ID.") {
        return;
      }
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
          RpcClient& downcasted = kj::downcast<RpcClient>(*target);

          auto message = connection->newOutgoingMessage(
              messageSizeHint<rpc::Disembargo>() + MESSAGE_TARGET_SIZE_HINT);
          auto builder = message->getBody().initAs<rpc::Message>().initDisembargo();

          {
            auto redirect = downcasted.writeTarget(builder.initTarget());

            // Disembargoes should only be sent to capabilities that were previously the object of
            // a `Resolve` message.  But `writeTarget` only ever returns non-null when called on
            // a PromiseClient.  The code which sends `Resolve` should have replaced any promise
            // with a direct node in order to solve the Tribble 4-way race condition.
            KJ_REQUIRE(redirect == nullptr,
                       "'Disembargo' of type 'senderLoopback' sent to an object that does not "
                       "appear to have been the object of a previous 'Resolve' message.") {
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
          embargoes.erase(context.getReceiverLoopback());
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

  class SingleCapPipeline: public PipelineHook, public kj::Refcounted {
  public:
    SingleCapPipeline(kj::Own<ClientHook>&& cap,
                      kj::Own<CapInjectorImpl>&& capInjector)
        : cap(kj::mv(cap)), capInjector(kj::mv(capInjector)) {}

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
    kj::Own<CapInjectorImpl> capInjector;
  };

  void handleRestore(kj::Own<IncomingRpcMessage>&& message, const rpc::Restore::Reader& restore) {
    QuestionId questionId = restore.getQuestionId();

    auto response = connection->newOutgoingMessage(
        messageSizeHint<rpc::Return>() + sizeInWords<rpc::CapDescriptor>() + 32);

    rpc::Return::Builder ret = response->getBody().getAs<rpc::Message>().initReturn();
    ret.setQuestionId(questionId);

    auto injector = kj::heap<CapInjectorImpl>(*this);
    CapBuilderContext context(*injector);

    kj::Own<ClientHook> capHook;

    // Call the restorer and initialize the answer.
    KJ_IF_MAYBE(exception, kj::runCatchingExceptions([&]() {
      KJ_IF_MAYBE(r, restorer) {
        Capability::Client cap = r->baseRestore(restore.getObjectId());
        auto results = context.imbue(ret.initResults());
        results.setAs<Capability>(cap);

        // Hack to extract the ClientHook, because Capability::Client doesn't provide direct
        // access.  Maybe it should?
        capHook = results.asReader().getPipelinedCap(nullptr);
      } else {
        KJ_FAIL_REQUIRE("This vat cannot restore this SturdyRef.") { break; }
      }
    })) {
      fromException(*exception, ret.initException());
      capHook = newBrokenCap(kj::mv(*exception));
    }

    message = nullptr;

    // Add the answer to the answer table for pipelining and send the response.
    auto& answer = answers[questionId];
    KJ_REQUIRE(!answer.active, "questionId is already in use") {
      return;
    }

    injector->finishDescriptors();

    answer.active = true;
    answer.pipeline = kj::Own<PipelineHook>(
        kj::refcounted<SingleCapPipeline>(kj::mv(capHook), kj::mv(injector)));

    response->send();
  }
};

}  // namespace

class RpcSystemBase::Impl final: public kj::TaskSet::ErrorHandler {
public:
  Impl(VatNetworkBase& network, kj::Maybe<SturdyRefRestorerBase&> restorer)
      : network(network), restorer(restorer), tasks(*this) {
    tasks.add(acceptLoop());
  }

  ~Impl() noexcept(false) {
    unwindDetector.catchExceptionsIfUnwinding([&]() {
      // std::unordered_map doesn't like it when elements' destructors throw, so carefully
      // disassemble it.
      if (!connections.empty()) {
        kj::Vector<kj::Own<RpcConnectionState>> deleteMe(connections.size());
        kj::Exception shutdownException(
            kj::Exception::Nature::LOCAL_BUG, kj::Exception::Durability::PERMANENT,
            __FILE__, __LINE__, kj::str("RpcSystem was destroyed."));
        for (auto& entry: connections) {
          entry.second->disconnect(kj::cp(shutdownException));
          deleteMe.add(kj::mv(entry.second));
        }
      }
    });
  }

  Capability::Client restore(_::StructReader hostId, ObjectPointer::Reader objectId) {
    KJ_IF_MAYBE(connection, network.baseConnectToRefHost(hostId)) {
      auto& state = getConnectionState(kj::mv(*connection));
      return Capability::Client(state.restore(objectId));
    } else KJ_IF_MAYBE(r, restorer) {
      return r->baseRestore(objectId);
    } else {
      return Capability::Client(newBrokenCap(
          "SturdyRef referred to a local object but there is no local SturdyRef restorer."));
    }
  }

  void taskFailed(kj::Exception&& exception) override {
    KJ_LOG(ERROR, exception);
  }

private:
  VatNetworkBase& network;
  kj::Maybe<SturdyRefRestorerBase&> restorer;
  kj::TaskSet tasks;

  typedef std::unordered_map<VatNetworkBase::Connection*, kj::Own<RpcConnectionState>>
      ConnectionMap;
  ConnectionMap connections;

  kj::UnwindDetector unwindDetector;

  RpcConnectionState& getConnectionState(kj::Own<VatNetworkBase::Connection>&& connection) {
    auto iter = connections.find(connection);
    if (iter == connections.end()) {
      VatNetworkBase::Connection* connectionPtr = connection;
      auto onDisconnect = kj::newPromiseAndFulfiller<void>();
      tasks.add(onDisconnect.promise.then([this,connectionPtr]() {
        connections.erase(connectionPtr);
      }));
      auto newState = kj::refcounted<RpcConnectionState>(
          restorer, kj::mv(connection), kj::mv(onDisconnect.fulfiller));
      RpcConnectionState& result = *newState;
      connections.insert(std::make_pair(connectionPtr, kj::mv(newState)));
      return result;
    } else {
      return *iter->second;
    }
  }

  kj::Promise<void> acceptLoop() {
    auto receive = network.baseAcceptConnectionAsRefHost().then(
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
};

RpcSystemBase::RpcSystemBase(VatNetworkBase& network, kj::Maybe<SturdyRefRestorerBase&> restorer)
    : impl(kj::heap<Impl>(network, restorer)) {}
RpcSystemBase::RpcSystemBase(RpcSystemBase&& other) noexcept = default;
RpcSystemBase::~RpcSystemBase() noexcept(false) {}

Capability::Client RpcSystemBase::baseRestore(
    _::StructReader hostId, ObjectPointer::Reader objectId) {
  return impl->restore(hostId, objectId);
}

}  // namespace _ (private)
}  // namespace capnp
