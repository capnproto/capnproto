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

#ifndef CAPNP_COMPILER_COMPILER_H_
#define CAPNP_COMPILER_COMPILER_H_

#include <capnp/compiler/grammar.capnp.h>
#include <capnp/schema.capnp.h>
#include <capnp/schema-loader.h>
#include "error-reporter.h"

namespace capnp {
namespace compiler {

class Module: public ErrorReporter {
public:
  virtual kj::StringPtr getLocalName() const = 0;
  // Typically, the absolute or cwd-relative path name of the module file, used in error messages.
  // This is only for display purposes.

  virtual kj::StringPtr getSourceName() const = 0;
  // The name of the module file relative to the source tree.  Used to decide where to output
  // generated code and to form the `displayName` in the schema.

  virtual Orphan<ParsedFile> loadContent(Orphanage orphanage) const = 0;
  // Loads the module content, using the given orphanage to allocate objects if necessary.

  virtual kj::Maybe<const Module&> importRelative(kj::StringPtr importPath) const = 0;
  // Find another module, relative to this one.  Importing the same logical module twice should
  // produce the exact same object, comparable by identity.  These objects are owned by some
  // outside pool that outlives the Compiler instance.
};

class Compiler {
  // Cross-links separate modules (schema files) and translates them into schema nodes.

public:
  Compiler();
  ~Compiler() noexcept(false);
  KJ_DISALLOW_COPY(Compiler);

  enum Mode {
    EAGER,
    // Completely traverse the module's parse tree and translate it into schema nodes before
    // returning from add().

    LAZY
    // Only interpret the module's definitions when they are requested.  The main advantage of this
    // mode is that imports will only be loaded if they are actually needed.
    //
    // Since the parse tree is traversed lazily, any particular schema node only becomes findable
    // by ID (using the SchemaLoader) once one of its neighbors in the graph has been examined.
    // As long as you are only traversing the graph -- only looking up IDs that you obtained from
    // other schema nodes from the same loader -- you shouldn't be able to tell the difference.
    // But if you receive IDs from some external source and want to look those up, you'd better
    // use EAGER mode.
  };

  uint64_t add(const Module& module, Mode mode) const;
  // Add a module to the Compiler, returning the module's file ID.  The ID can then be used to
  // look up the schema in the SchemaLoader returned by `getLoader()`.  However, if there were any
  // errors while compiling (reported via `module.addError()`), then the SchemaLoader may behave as
  // if the node doesn't exist, or may return an invalid partial Schema.

  kj::Maybe<uint64_t> lookup(uint64_t parent, kj::StringPtr childName) const;
  // Given the type ID of a schema node, find the ID of a node nested within it, without actually
  // building either node.  Throws an exception if the parent ID is not recognized; returns null
  // if the parent has no child of the given name.

  const SchemaLoader& getLoader() const;
  // Get a SchemaLoader backed by this compiler.  Schema nodes will be lazily constructed as you
  // traverse them using this loader.

  void clearWorkspace();
  // The compiler builds a lot of temporary tables and data structures while it works.  It's
  // useful to keep these around if more work is expected (especially if you are using lazy
  // compilation and plan to look up Schema nodes that haven't already been seen), but once
  // the SchemaLoader has everything you need, you can call clearWorkspace() to free up the
  // temporary space.  Note that it's safe to call clearWorkspace() even if you do expect to
  // compile more nodes in the future; it may simply lead to redundant work if the discarded
  // structures are needed again.

private:
  class Impl;
  kj::Own<Impl> impl;

  class CompiledModule;
  class Node;
  class Alias;
};

}  // namespace compiler
}  // namespace capnp

#endif  // CAPNP_COMPILER_COMPILER_H_
