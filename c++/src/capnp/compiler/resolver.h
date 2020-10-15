// Copyright (c) 2013-2020 Sandstorm Development Group, Inc. and contributors
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

#pragma once

#include <capnp/compiler/grammar.capnp.h>
#include <capnp/schema.capnp.h>
#include <capnp/schema.h>
#include <kj/one-of.h>

CAPNP_BEGIN_HEADER

namespace capnp {
namespace compiler {

class Resolver {
  // Callback class used to find other nodes relative to some existing node.
  //
  // `Resolver` is used when compiling one declaration requires inspecting the compiled versions
  // of other declarations it depends on. For example, if struct type Foo contains a field of type
  // Bar, and specifies a default value for that field, then to parse that default value we need
  // the compiled version of `Bar`. Or, more commonly, if a struct type Foo refers to some other
  // type `Bar.Baz`, this requires doing a lookup that depends on at least partial compilation of
  // `Bar`, in order to discover its nested type `Baz`.
  //
  // Note that declarations are often compiled just-in-time the first time they are resolved. So,
  // the methods of Resolver may recurse back into other parts of the compiler. It must detect when
  // a dependency cycle occurs and report an error in order to prevent an infinite loop.

public:
  struct ResolvedDecl {
    // Information about a resolved declaration.

    uint64_t id;
    // Type ID / node ID of the resolved declaration.

    uint genericParamCount;
    // If non-zero, the declaration is a generic with the given number of parameters.

    uint64_t scopeId;
    // The ID of the parent scope of this declaration.

    Declaration::Which kind;
    // What basic kind of declaration is this? E.g. struct, interface, const, etc.

    Resolver* resolver;
    // `Resolver` instance that can be used to further resolve other declarations relative to this
    // one.

    kj::Maybe<schema::Brand::Reader> brand;
    // If present, then it is necessary to replace the brand scope with the given brand before
    // using the target type. This happens when the decl resolved to an alias; all other fields
    // of `ResolvedDecl` refer to the target of the alias, except for `scopeId` which is the
    // scope that contained the alias.
  };

  struct ResolvedParameter {
    uint64_t id;  // ID of the node declaring the parameter.
    uint index;   // Index of the parameter.
  };

  typedef kj::OneOf<ResolvedDecl, ResolvedParameter> ResolveResult;

  virtual kj::Maybe<ResolveResult> resolve(kj::StringPtr name) = 0;
  // Look up the given name, relative to this node, and return basic information about the
  // target.

  virtual kj::Maybe<ResolveResult> resolveMember(kj::StringPtr name) = 0;
  // Look up a member of this node.

  virtual ResolvedDecl resolveBuiltin(Declaration::Which which) = 0;
  virtual ResolvedDecl resolveId(uint64_t id) = 0;

  virtual kj::Maybe<ResolvedDecl> getParent() = 0;
  // Returns the parent of this scope, or null if this is the top scope.

  virtual ResolvedDecl getTopScope() = 0;
  // Get the top-level scope containing this node.

  virtual kj::Maybe<Schema> resolveBootstrapSchema(uint64_t id, schema::Brand::Reader brand) = 0;
  // Get the schema for the given ID.  If a schema is returned, it must be safe to traverse its
  // dependencies via the Schema API.  A schema that is only at the bootstrap stage is
  // acceptable.
  //
  // Throws an exception if the id is not one that was found by calling resolve() or by
  // traversing other schemas.  Returns null if the ID is recognized, but the corresponding
  // schema node failed to be built for reasons that were already reported.

  virtual kj::Maybe<schema::Node::Reader> resolveFinalSchema(uint64_t id) = 0;
  // Get the final schema for the given ID.  A bootstrap schema is not acceptable.  A raw
  // node reader is returned rather than a Schema object because using a Schema object built
  // by the final schema loader could trigger lazy initialization of dependencies which could
  // lead to a cycle and deadlock.
  //
  // Throws an exception if the id is not one that was found by calling resolve() or by
  // traversing other schemas.  Returns null if the ID is recognized, but the corresponding
  // schema node failed to be built for reasons that were already reported.

  virtual kj::Maybe<ResolvedDecl> resolveImport(kj::StringPtr name) = 0;
  // Get the ID of an imported file given the import path.

  virtual kj::Maybe<kj::Array<const byte>> readEmbed(kj::StringPtr name) = 0;
  // Read and return the contents of a file for an `embed` expression.

  virtual kj::Maybe<Type> resolveBootstrapType(schema::Type::Reader type, Schema scope) = 0;
  // Compile a schema::Type into a Type whose dependencies may safely be traversed via the schema
  // API. These dependencies may have only bootstrap schemas. Returns null if the type could not
  // be constructed due to already-reported errors.
};

}  // namespace compiler
}  // namespace capnp

CAPNP_END_HEADER
