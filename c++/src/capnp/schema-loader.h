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

#ifndef CAPNP_SCHEMA_LOADER_H_
#define CAPNP_SCHEMA_LOADER_H_

#include "schema.h"
#include <kj/memory.h>

namespace capnp {

class SchemaLoader {
public:
  SchemaLoader();
  ~SchemaLoader() noexcept(false);
  KJ_DISALLOW_COPY(SchemaLoader);

  Schema get(uint64_t id) const;
  // Gets the schema for the given ID, throwing an exception if it isn't present.
  //
  // The returned schema may be invalidated if load() is called with a new schema for the same ID.
  // In general, you should not call load() while a schema from this loader is in-use.

  kj::Maybe<Schema> tryGet(uint64_t id) const;
  // Like get() but doesn't throw.

  Schema load(const schema::Node::Reader& reader);
  // Loads the given schema node.  Validates the node and throws an exception if invalid.  This
  // makes a copy of the schema, so the object passed in can be destroyed after this returns.
  //
  // If the node has any dependencies which are not already loaded, they will be initialized as
  // stubs -- empty schemas of whichever kind is expected.
  //
  // If another schema for the given reader has already been seen, the loader will inspect both
  // schemas to determine which one is newer, and use that that one.  If the two versions are
  // found to be incompatible, an exception is thrown.  If the two versions differ but are
  // compatible and the loader cannot determine which is newer (e.g., the only changes are renames),
  // the existing schema will be preferred.  Note that in any case, the loader will end up keeping
  // around copies of both schemas, so you shouldn't repeatedly reload schemas into the same loader.
  //
  // The following properties of the schema node are validated:
  // - Struct size and preferred list encoding are valid and consistent.
  // - Struct members are fields or unions.
  // - Union members are fields.
  // - Field offsets are in-bounds.
  // - Ordinals and codeOrders are sequential starting from zero.
  // - Values are of the right union case to match their types.
  //
  // You should assume anything not listed above is NOT validated.  In particular, things that are
  // not validated now, but could be in the future, include but are not limited to:
  // - Names.
  // - Annotation values.  (This is hard because the annotation declaration is not always
  //   available.)
  // - Content of default/constant values of pointer type.  (Validating these would require knowing
  //   their schema, but even if the schemas are available at validation time, they could be
  //   updated by a subsequent load(), invalidating existing values.  Instead, these values are
  //   validated at the time they are used, as usual for Cap'n Proto objects.)
  //
  // Also note that unknown types are not considered invalid.  Instead, the dynamic API returns
  // a DynamicValue with type UNKNOWN for these.

  template <typename T>
  void loadCompiledTypeAndDependencies();
  // Load the schema for the given compiled-in type and all of its dependencies.
  //
  // If you want to be able to cast a DynamicValue built from this SchemaLoader to the compiled-in
  // type using as<T>(), you must call this method before constructing the DynamicValue.  Otherwise,
  // as<T>() will throw an exception complaining about type mismatch.

  kj::Array<Schema> getAllLoaded() const;
  // Get a complete list of all loaded schema nodes.  It is particularly useful to call this after
  // loadCompiledTypeAndDependencies<T>() in order to get a flat list of all of T's transitive
  // dependencies.

private:
  class Validator;
  class CompatibilityChecker;
  class Impl;
  kj::Own<Impl> impl;

  void loadNative(const _::RawSchema* nativeSchema);
};

template <typename T>
inline void SchemaLoader::loadCompiledTypeAndDependencies() {
  loadNative(&_::rawSchema<T>());
}

}  // namespace capnp

#endif  // CAPNP_SCHEMA_LOADER_H_
