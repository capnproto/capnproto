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

#ifndef CAPNP_CAPABILITY_H_
#define CAPNP_CAPABILITY_H_

#include <kj/async.h>
#include "object.h"

namespace capnp {

template <typename T>
class RemotePromise: public kj::Promise<kj::Own<ReaderFor<T>>>, public T::Pipeline {
  // A Promise which supports pipelined calls.  T is typically a struct type.

public:
  inline RemotePromise(kj::Promise<kj::Own<ReaderFor<T>>>&& promise,
                       typename T::Pipeline&& pipeline)
      : kj::Promise<kj::Own<ReaderFor<T>>>(kj::mv(promise)),
        T::Pipeline(kj::mv(pipeline)) {}
  inline RemotePromise(decltype(nullptr))
      : kj::Promise<kj::Own<ReaderFor<T>>>(nullptr) {}
  KJ_DISALLOW_COPY(RemotePromise);
  RemotePromise(RemotePromise&& other) = default;
  RemotePromise& operator=(RemotePromise&& other) = default;
};

// =======================================================================================

class Call;
struct CallResult;

class TypelessCapability {
  // This is an internal type used to represent a live capability (or a promise for a capability).
  // This class should not be used directly by applications; it is intended to be used by the
  // generated code wrappers.

public:
  virtual kj::Own<Call> newCall(uint64_t interfaceId, uint16_t methodId) const = 0;
  // Begin a new call to a method of this capability.

  virtual kj::Own<TypelessCapability> addRef() const = 0;
  // Return a new reference-counted pointer to the same capability.  (Reference counting can be
  // implemented using a special Disposer, so that the returned pointer actually has the same
  // identity as the original.)

  virtual kj::Promise<void> whenResolved() const = 0;
  // If the capability is actually only a promise, the returned promise resolves once the
  // capability itself has resolved to its final destination (or propagates the exception if
  // the capability promise is rejected).  This is mainly useful for error-checking in the case
  // where no calls are being made.  There is no reason to wait for this before making calls; if
  // the capability does not resolve, the call results will propagate the error.

  // TODO(soon):  method implementing Join
};

// =======================================================================================

class Call {
public:
  virtual ObjectPointer::Builder getRequest() = 0;
  // Get the request object for this call, to be filled in before sending.

  virtual RemotePromise<CallResult> send() = 0;
  // Send the call and return a promise for the result.
};

class CallRunner {
  // Implements pipelined requests for a particular outstanding call.

public:
  virtual kj::Own<CallRunner> addRef() const = 0;
  // Increment this object's reference count.

  struct PipelineOp {
    enum Type {
      GET_POINTER_FIELD
    };

    Type type;
    union {
      uint16_t pointerIndex;
    };
  };

  virtual kj::Own<TypelessCapability> getPipelinedCap(kj::ArrayPtr<const PipelineOp> ops) const = 0;
  // Extract a promised Capability from the answer.
};

struct CallResult {
  // Result of a call.  Designed to be used as RemotePromise<CallResult>.

  typedef ObjectPointer::Reader Reader;
  // So RemotePromise<CallResult> resolves to Own<ObjectPointer::Reader>.

  class Pipeline {
  public:
    inline explicit Pipeline(kj::Own<CallRunner>&& runner): runner(kj::mv(runner)) {}

    Pipeline getPointerField(uint16_t pointerIndex) const;
    // Return a new Promise representing a sub-object of the result.  `pointerIndex` is the index
    // of the sub-object within the pointer section of the result (the result must be a struct).
    //
    // TODO(kenton):  On GCC 4.8 / Clang 3.3, use rvalue qualifiers to avoid the need for copies.
    //   Make `ops` into a Vector so that it can be extended without allocation in this case.

    inline kj::Own<TypelessCapability> asCap() const { return runner->getPipelinedCap(ops); }
    // Expect that the result is a capability and construct a pipelined version of it now.

  private:
    kj::Own<CallRunner> runner;
    kj::Array<CallRunner::PipelineOp> ops;

    inline Pipeline(kj::Own<CallRunner>&& runner, kj::Array<CallRunner::PipelineOp>&& ops)
        : runner(kj::mv(runner)), ops(kj::mv(ops)) {}
  };
};

// =======================================================================================
// Classes for imbuing message readers/builders with a capability context.
//
// These classes are for use by RPC implementations.  Application code need not know about them.
//
// TODO(kenton):  Move these to a separate header.
//
// Normally, MessageReader and MessageBuilder do not support interface pointers because they
// are not RPC-aware and so have no idea how to convert between a serialized CapabilityDescriptor
// and a live capability.  To fix this, a reader/builder object needs to be "imbued" with a
// capability context.  This creates a new reader/builder which points at the same object but has
// the ability to deal with interface fields.

namespace _ {  // private

class ImbuedReaderArena;
class ImbuedBuilderArena;

}  // namespace _ (private)

class CapExtractorBase {
  // Non-template base class for CapExtractor<T>.

private:
  virtual kj::Own<TypelessCapability> extractCapInternal(const _::StructReader& capDescriptor) = 0;
  friend class _::ImbuedReaderArena;
};

class CapInjectorBase {
  // Non-template base class for CapInjector<T>.

private:
  virtual void injectCapInternal(_::PointerBuilder builder, kj::Own<TypelessCapability>&& cap) = 0;
  friend class _::ImbuedBuilderArena;
};

template <typename CapDescriptor>
class CapExtractor: public CapExtractorBase {
  // Callback used to read a capability from a message, implemented by the RPC system.
  // `CapDescriptor` is the struct type which the RPC implementation uses to represent
  // capabilities.  (On the wire, an interface pointer actually points to a struct of this type.)

public:
  virtual kj::Own<TypelessCapability> extractCap(typename CapDescriptor::Reader descriptor) = 0;
  // Given the descriptor read off the wire, construct a live capability.

private:
  kj::Own<TypelessCapability> extractCapInternal(
      const _::StructReader& capDescriptor) override final {
    return extractCap(typename CapDescriptor::Reader(capDescriptor));
  }
};

template <typename CapDescriptor>
class CapInjector: public CapInjectorBase {
  // Callback used to write a capability into a message, implemented by the RPC system.
  // `CapDescriptor` is the struct type which the RPC implementation uses to represent
  // capabilities.  (On the wire, an interface pointer actually points to a struct of this type.)

public:
  virtual void injectCap(typename CapDescriptor::Builder descriptor,
                         kj::Own<TypelessCapability>&& cap) = 0;
  // Fill in the given descriptor so that it describes the given capability.

private:
  void injectCapInternal(_::PointerBuilder builder,
                         kj::Own<TypelessCapability>&& cap) override final {
    injectCap(
        typename CapDescriptor::Builder(builder.initCapDescriptor(_::structSize<CapDescriptor>())),
        kj::mv(cap));
  }
};

class CapReaderContext {
  // Class which can "imbue" reader objects from some other message with a capability context,
  // so that interface pointers found in the message can be extracted and called.

public:
  CapReaderContext(Orphanage arena, CapExtractorBase& extractor);
  ~CapReaderContext() noexcept(false);

  ObjectPointer::Reader imbue(ObjectPointer::Reader base);

private:
  void* arenaSpace[15 + sizeof(kj::MutexGuarded<void*>) / sizeof(void*)];

  virtual kj::Own<TypelessCapability> extractCapInternal(const _::StructReader& capDescriptor) = 0;

  friend class _::ImbuedReaderArena;
};

class CapBuilderContext {
  // Class which can "imbue" reader objects from some other message with a capability context,
  // so that interface pointers found in the message can be set to point at live capabilities.

public:
  CapBuilderContext(Orphanage arena, CapInjectorBase& injector);
  ~CapBuilderContext() noexcept(false);

  ObjectPointer::Builder imbue(ObjectPointer::Builder base);

private:
  void* arenaSpace[15 + sizeof(kj::MutexGuarded<void*>) / sizeof(void*)];

  virtual void injectCapInternal(_::PointerBuilder builder, kj::Own<TypelessCapability>&& cap) = 0;

  friend class _::ImbuedBuilderArena;
};

}  // namespace capnp

#endif  // CAPNP_CAPABILITY_H_
