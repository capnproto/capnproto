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

// Classes for imbuing message readers/builders with a capability context.
//
// These classes are for use by RPC implementations.  Application code need not know about them.
//
// Normally, MessageReader and MessageBuilder do not support interface pointers because they
// are not RPC-aware and so have no idea how to convert between a serialized CapabilityDescriptor
// and a live capability.  To fix this, a reader/builder object needs to be "imbued" with a
// capability context.  This creates a new reader/builder which points at the same object but has
// the ability to deal with interface fields.  Use `CapReaderContext` and `CapBuilderContext` to
// accomplish this.

#ifndef CAPNP_CAPABILITY_CONTEXT_H_
#define CAPNP_CAPABILITY_CONTEXT_H_

#include "layout.h"
#include "any.h"
#include "message.h"
#include <kj/mutex.h>
#include <kj/vector.h>

namespace kj { class Exception; }

namespace capnp {

class ClientHook;

namespace _ {  // private

class ImbuedReaderArena;
class ImbuedBuilderArena;

}  // namespace _ (private)

class CapExtractorBase {
  // Non-template base class for CapExtractor<T>.

private:
  virtual kj::Own<ClientHook> extractCapInternal(const _::StructReader& capDescriptor) = 0;
  virtual kj::Own<ClientHook> newBrokenCapInternal(kj::StringPtr description) = 0;
  friend class _::ImbuedReaderArena;
};

class CapInjectorBase {
  // Non-template base class for CapInjector<T>.

private:
  virtual _::OrphanBuilder injectCapInternal(
      _::BuilderArena* arena, kj::Own<ClientHook>&& cap) = 0;
  virtual void dropCapInternal(const _::StructReader& capDescriptor) = 0;
  virtual kj::Own<ClientHook> getInjectedCapInternal(const _::StructReader& capDescriptor) = 0;
  virtual kj::Own<ClientHook> newBrokenCapInternal(kj::StringPtr description) = 0;
  friend class _::ImbuedBuilderArena;
};

template <typename CapDescriptor>
class CapExtractor: public CapExtractorBase {
  // Callback used to read a capability from a message, implemented by the RPC system.
  // `CapDescriptor` is the struct type which the RPC implementation uses to represent
  // capabilities.  (On the wire, an interface pointer actually points to a struct of this type.)

public:
  virtual kj::Own<ClientHook> extractCap(typename CapDescriptor::Reader descriptor) = 0;
  // Given the descriptor read off the wire, construct a live capability.

private:
  kj::Own<ClientHook> extractCapInternal(const _::StructReader& capDescriptor) override final;
  kj::Own<ClientHook> newBrokenCapInternal(kj::StringPtr description) override final;
};

template <typename CapDescriptor>
class CapInjector: public CapInjectorBase {
  // Callback used to write a capability into a message, implemented by the RPC system.
  // `CapDescriptor` is the struct type which the RPC implementation uses to represent
  // capabilities.  (On the wire, an interface pointer actually points to a struct of this type.)

public:
  virtual void injectCap(typename CapDescriptor::Builder descriptor, kj::Own<ClientHook>&& cap) = 0;
  // Fill in the given descriptor so that it describes the given capability.

  virtual kj::Own<ClientHook> getInjectedCap(typename CapDescriptor::Reader descriptor) = 0;
  // Read back a cap that was previously injected with `injectCap`.  This should return a new
  // reference.

  virtual void dropCap(typename CapDescriptor::Reader descriptor) = 0;
  // Read back a cap that was previously injected with `injectCap`.  This should return a new
  // reference.

private:
  _::OrphanBuilder injectCapInternal(_::BuilderArena* arena,
                                     kj::Own<ClientHook>&& cap) override final;
  void dropCapInternal(const _::StructReader& capDescriptor) override final;
  kj::Own<ClientHook> getInjectedCapInternal(const _::StructReader& capDescriptor) override final;
  kj::Own<ClientHook> newBrokenCapInternal(kj::StringPtr description) override final;
};

// -------------------------------------------------------------------

class CapReaderContext {
  // Class which can "imbue" reader objects from some other message with a capability context,
  // so that interface pointers found in the message can be extracted and called.
  //
  // `imbue()` can only be called once per context.

public:
  CapReaderContext(CapExtractorBase& extractor);
  ~CapReaderContext() noexcept(false);

  AnyPointer::Reader imbue(AnyPointer::Reader base);

private:
  CapExtractorBase* extractor;  // becomes null once arena is allocated
  void* arenaSpace[12 + sizeof(kj::MutexGuarded<void*>) / sizeof(void*)];

  _::ImbuedReaderArena& arena() { return *reinterpret_cast<_::ImbuedReaderArena*>(arenaSpace); }

  friend class _::ImbuedReaderArena;
};

class CapBuilderContext {
  // Class which can "imbue" reader objects from some other message with a capability context,
  // so that interface pointers found in the message can be set to point at live capabilities.
  //
  // `imbue()` can only be called once per context.

public:
  CapBuilderContext(CapInjectorBase& injector);
  ~CapBuilderContext() noexcept(false);

  AnyPointer::Builder imbue(AnyPointer::Builder base);

private:
  CapInjectorBase* injector;  // becomes null once arena is allocated
  void* arenaSpace[13];

  _::ImbuedBuilderArena& arena() { return *reinterpret_cast<_::ImbuedBuilderArena*>(arenaSpace); }

  friend class _::ImbuedBuilderArena;
};

// -------------------------------------------------------------------

namespace _ {  // private

struct LocalCapDescriptor {
  class Reader;
  class Builder;
};

}  // namespace _ (private)

class LocalMessage final: private CapInjector<_::LocalCapDescriptor> {
  // An in-process message which can contain capabilities.  Use in place of MallocMessageBuilder
  // when you need to be able to construct a message in-memory that contains capabilities, and this
  // message will never leave the process.  You cannot serialize this message, since it doesn't
  // know how to properly serialize its capabilities.

public:
  LocalMessage(uint firstSegmentWords = SUGGESTED_FIRST_SEGMENT_WORDS,
               AllocationStrategy allocationStrategy = SUGGESTED_ALLOCATION_STRATEGY);

  inline AnyPointer::Builder getRoot() { return root; }
  inline AnyPointer::Reader getRootReader() const { return root.asReader(); }

private:
  MallocMessageBuilder message;
  CapBuilderContext capContext;
  AnyPointer::Builder root;

  struct State {
    uint counter;
    kj::Vector<kj::Own<ClientHook>> caps;
  };
  kj::MutexGuarded<State> state;

  void injectCap(_::LocalCapDescriptor::Builder descriptor, kj::Own<ClientHook>&& cap) override;
  kj::Own<ClientHook> getInjectedCap(_::LocalCapDescriptor::Reader descriptor) override;
  void dropCap(_::LocalCapDescriptor::Reader descriptor) override;
};

kj::Own<ClientHook> newBrokenCap(kj::StringPtr reason);
kj::Own<ClientHook> newBrokenCap(kj::Exception&& reason);
// Helper function that creates a capability which simply throws exceptions when called.

kj::Own<PipelineHook> newBrokenPipeline(kj::Exception&& reason);
// Helper function that creates a pipeline which simply throws exceptions when called.

// =======================================================================================
// inline implementation details

template <typename CapDescriptor>
kj::Own<ClientHook> CapExtractor<CapDescriptor>::extractCapInternal(
    const _::StructReader& capDescriptor) {
  return extractCap(typename CapDescriptor::Reader(capDescriptor));
}

template <typename CapDescriptor>
kj::Own<ClientHook> CapExtractor<CapDescriptor>::newBrokenCapInternal(kj::StringPtr description) {
  // Notice that because this method was virtualized and then implemented in the template,
  // we can call newBrokenCap which is only implemented in libcapnp-rpc even though arena.c++
  // (in libcapnp proper) is the only caller of this method.
  return newBrokenCap(description.cStr());
}

template <typename CapDescriptor>
_::OrphanBuilder CapInjector<CapDescriptor>::injectCapInternal(
    _::BuilderArena* arena, kj::Own<ClientHook>&& cap) {
  auto result = _::OrphanBuilder::initStruct(arena, _::structSize<CapDescriptor>());
  injectCap(typename CapDescriptor::Builder(result.asStruct(_::structSize<CapDescriptor>())),
            kj::mv(cap));
  return kj::mv(result);
}

template <typename CapDescriptor>
void CapInjector<CapDescriptor>::dropCapInternal(const _::StructReader& capDescriptor) {
  dropCap(typename CapDescriptor::Reader(capDescriptor));
}

template <typename CapDescriptor>
kj::Own<ClientHook> CapInjector<CapDescriptor>::getInjectedCapInternal(
    const _::StructReader& capDescriptor) {
  return getInjectedCap(typename CapDescriptor::Reader(capDescriptor));
}

template <typename CapDescriptor>
kj::Own<ClientHook> CapInjector<CapDescriptor>::newBrokenCapInternal(kj::StringPtr description) {
  // Notice that because this method was virtualized and then implemented in the template,
  // we can call newBrokenCap which is only implemented in libcapnp-rpc even though arena.c++
  // (in libcapnp proper) is the only caller of this method.
  return newBrokenCap(description.cStr());
}

}  // namespace capnp

#endif  // CAPNP_CAPABILITY_CONTEXT_H_
