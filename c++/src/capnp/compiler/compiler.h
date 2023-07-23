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

#pragma once

#include <capnp/compiler/grammar.capnp.h>
#include <capnp/schema.capnp.h>
#include <capnp/schema-loader.h>
#include "error-reporter.h"

CAPNP_BEGIN_HEADER

namespace capnp {
namespace compiler {

class Module: public ErrorReporter {
public:
  virtual kj::StringPtr getSourceName() = 0;
  // The name of the module file relative to the source tree.  Used to decide where to output
  // generated code and to form the `displayName` in the schema.

  virtual Orphan<ParsedFile> loadContent(Orphanage orphanage) = 0;
  // Loads the module content, using the given orphanage to allocate objects if necessary.

  virtual kj::Maybe<Module&> importRelative(kj::StringPtr importPath) = 0;
  // Find another module, relative to this one.  Importing the same logical module twice should
  // produce the exact same object, comparable by identity.  These objects are owned by some
  // outside pool that outlives the Compiler instance.

  virtual kj::Maybe<kj::Array<const byte>> embedRelative(kj::StringPtr embedPath) = 0;
  // Read and return the content of a file specified using `embed`.
};

class Compiler final: private SchemaLoader::LazyLoadCallback {
  // Cross-links separate modules (schema files) and translates them into schema nodes.
  //
  // This class is thread-safe, hence all its methods are const.

public:
  enum AnnotationFlag {
    COMPILE_ANNOTATIONS,
    // Compile annotations normally.

    DROP_ANNOTATIONS
    // Do not compile any annotations, eagerly or lazily.  All "annotations" fields in the schema
    // will be left empty.  This is useful to avoid parsing imports that are used only for
    // annotations which you don't intend to use anyway.
    //
    // Unfortunately annotations cannot simply be compiled lazily because filling in the
    // "annotations" field at the usage site requires knowing the annotation's type, which requires
    // compiling the annotation, and the schema API has no particular way to detect when you
    // try to access the "annotations" field in order to lazily compile the annotations at that
    // point.
  };

  explicit Compiler(AnnotationFlag annotationFlag = COMPILE_ANNOTATIONS);
  ~Compiler() noexcept(false);
  KJ_DISALLOW_COPY(Compiler);

  uint64_t add(Module& module) const;
  // Add a module to the Compiler, returning the module's file ID.  The ID can then be looked up in
  // the `SchemaLoader` returned by `getLoader()`.  However, the SchemaLoader may behave as if the
  // schema node doesn't exist if any compilation errors occur (reported via the module's
  // ErrorReporter).  The module is parsed at the time `add()` is called, but not fully compiled --
  // individual schema nodes are compiled lazily.  If you want to force eager compilation,
  // see `eagerlyCompile()`, below.

  kj::Maybe<uint64_t> lookup(uint64_t parent, kj::StringPtr childName) const;
  // Given the type ID of a schema node, find the ID of a node nested within it.  Throws an
  // exception if the parent ID is not recognized; returns null if the parent has no child of the
  // given name.  Neither the parent nor the child schema node is actually compiled.

  kj::Maybe<schema::Node::SourceInfo::Reader> getSourceInfo(uint64_t id) const;
  // Get the SourceInfo for the given type ID, if available.

  Orphan<List<schema::CodeGeneratorRequest::RequestedFile::Import>>
      getFileImportTable(Module& module, Orphanage orphanage) const;
  // Build the import table for the CodeGeneratorRequest for the given module.

  Orphan<List<schema::Node::SourceInfo>> getAllSourceInfo(Orphanage orphanage) const;
  // Gets the SourceInfo structs for all nodes parsed by the compiler.

  enum Eagerness: uint32_t {
    // Flags specifying how eager to be about compilation.  These are intended to be bitwise OR'd.
    // Used with the method `eagerlyCompile()`.
    //
    // Schema declarations can be compiled upfront, or they can be compiled lazily as they are
    // needed.  Usually, the difference is not observable, but it is not a perfect abstraction.
    // The difference has the following effects:
    // * `getLoader().getAllLoaded()` only returns the schema nodes which have been compiled so
    //   far.
    // * `getLoader().get()` (i.e. searching for a schema by ID) can only find schema nodes that
    //   have either been compiled already, or which are referenced by schema nodes which have been
    //   compiled already.  This means that if the ID you pass in came from another schema node
    //   compiled with the same compiler, there should be no observable difference, but if you
    //   have an ID from elsewhere which you _a priori_ expect is defined in a particular schema
    //   file, you will need to compile that file eagerly before you look up the node by ID.
    // * Errors are reported when they are encountered, so some errors will not be reported until
    //   the node is actually compiled.
    // * If an imported file is not needed, it will never even be read from disk.
    //
    // The last point is the main reason why you might want to prefer lazy compilation:  it allows
    // you to use a schema file with missing imports, so long as those missing imports are not
    // actually needed.
    //
    // For example, the flag combo:
    //     EAGER_NODE | EAGER_CHILDREN | EAGER_DEPENDENCIES | EAGER_DEPENDENCY_PARENTS
    // will compile the entire given module, plus all direct dependencies of anything in that
    // module, plus all lexical ancestors of those dependencies.  This is what the Cap'n Proto
    // compiler uses when building initial code generator requests.

    ALL_RELATED_NODES = ~0u,
    // Compile everything that is in any way related to the target node, including its entire
    // containing file and everything transitively imported by it.

    NODE = 1 << 0,
    // Eagerly compile the requested node, but not necessarily any of its parents, children, or
    // dependencies.

    PARENTS = 1 << 1,
    // Eagerly compile all lexical parents of the requested node.  Only meaningful in conjuction
    // with NODE.

    CHILDREN = 1 << 2,
    // Eagerly compile all of the node's lexically nested nodes.  Only meaningful in conjuction
    // with NODE.

    DEPENDENCIES = NODE << 15,
    // For all nodes compiled as a result of the above flags, also compile their direct
    // dependencies.  E.g. if Foo is a struct which contains a field of type Bar, and Foo is
    // compiled, then also compile Bar.  "Dependencies" are defined as field types, method
    // parameter and return types, and annotation types.  Nested types and outer types are not
    // considered dependencies.

    DEPENDENCY_PARENTS = PARENTS * DEPENDENCIES,
    DEPENDENCY_CHILDREN = CHILDREN * DEPENDENCIES,
    DEPENDENCY_DEPENDENCIES = DEPENDENCIES * DEPENDENCIES,
    // Like PARENTS, CHILDREN, and DEPENDENCIES, but applies relative to dependency nodes rather
    // than the original requested node.  Note that DEPENDENCY_DEPENDENCIES causes all transitive
    // dependencies of the requested node to be compiled.
    //
    // These flags are defined as multiples of the original flag and DEPENDENCIES so that we
    // can form the flags to use when traversing a dependency by shifting bits.
  };

  void eagerlyCompile(uint64_t id, uint eagerness) const;
  // Force eager compilation of schema nodes related to the given ID.  `eagerness` specifies which
  // related nodes should be compiled before returning.  It is a bitwise OR of the possible values
  // of the `Eagerness` enum.
  //
  // If this returns and no errors have been reported, then it is guaranteed that the compiled
  // nodes can be found in the SchemaLoader returned by `getLoader()`.

  const SchemaLoader& getLoader() const { return loader; }
  SchemaLoader& getLoader() { return loader; }
  // Get a SchemaLoader backed by this compiler.  Schema nodes will be lazily constructed as you
  // traverse them using this loader.

  void clearWorkspace() const;
  // The compiler builds a lot of temporary tables and data structures while it works.  It's
  // useful to keep these around if more work is expected (especially if you are using lazy
  // compilation and plan to look up Schema nodes that haven't already been seen), but once
  // the SchemaLoader has everything you need, you can call clearWorkspace() to free up the
  // temporary space.  Note that it's safe to call clearWorkspace() even if you do expect to
  // compile more nodes in the future; it may simply lead to redundant work if the discarded
  // structures are needed again.

private:
  class Impl;
  kj::MutexGuarded<kj::Own<Impl>> impl;
  SchemaLoader loader;

  class CompiledModule;
  class Node;
  class Alias;

  void load(const SchemaLoader& loader, uint64_t id) const override;
};

}  // namespace compiler
}  // namespace capnp

CAPNP_END_HEADER
