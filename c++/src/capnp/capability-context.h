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

// -------------------------------------------------------------------

class CapReaderContext {
  // Class which can "imbue" reader objects from some other message with a capability context,
  // so that interface pointers found in the message can be extracted and called.
  //
  // `imbue()` can only be called once per context.

public:
  CapReaderContext(kj::Array<kj::Own<ClientHook>>&& capTable);
  // `capTable` is the list of capabilities for this message.

  ~CapReaderContext() noexcept(false);

  AnyPointer::Reader imbue(AnyPointer::Reader base);

private:
  kj::Maybe<kj::Array<kj::Own<ClientHook>>> capTable;  // becomes null once arena is allocated
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
  CapBuilderContext();
  ~CapBuilderContext() noexcept(false);

  AnyPointer::Builder imbue(AnyPointer::Builder base);

  kj::ArrayPtr<kj::Own<ClientHook>> getCapTable();
  // Return the table of capabilities injected into the message.

private:
  bool arenaAllocated = false;
  void* arenaSpace[15];

  _::ImbuedBuilderArena& arena() { return *reinterpret_cast<_::ImbuedBuilderArena*>(arenaSpace); }

  friend class _::ImbuedBuilderArena;
};

// -------------------------------------------------------------------

class LocalMessage final {
  // An in-process message which can contain capabilities.  Use in place of MallocMessageBuilder
  // when you need to be able to construct a message in-memory that contains capabilities, and this
  // message will never leave the process.  You cannot serialize this message, since it doesn't
  // know how to properly serialize its capabilities.

public:
  LocalMessage(kj::Maybe<MessageSize> sizeHint = nullptr);

  inline AnyPointer::Builder getRoot() { return root; }
  inline AnyPointer::Reader getRootReader() const { return root.asReader(); }

private:
  MallocMessageBuilder message;
  CapBuilderContext capContext;
  AnyPointer::Builder root;
};

kj::Own<ClientHook> newBrokenCap(kj::StringPtr reason);
kj::Own<ClientHook> newBrokenCap(kj::Exception&& reason);
// Helper function that creates a capability which simply throws exceptions when called.

kj::Own<PipelineHook> newBrokenPipeline(kj::Exception&& reason);
// Helper function that creates a pipeline which simply throws exceptions when called.

}  // namespace capnp

#endif  // CAPNP_CAPABILITY_CONTEXT_H_
