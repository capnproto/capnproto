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

template <typename ContentType>
class Module: public ErrorReporter {
public:
  virtual kj::StringPtr getLocalName() const = 0;
  // Typically, the absolute or cwd-relative path name of the module file, used in error messages.
  // This is only for display purposes.

  virtual kj::StringPtr getSourceName() const = 0;
  // The name of the module file relative to the source tree.  Used to decide where to output
  // generated code and to form the `displayName` in the schema.

  virtual ContentType loadContent(Orphanage orphanage) const = 0;
  // Loads the module content, using the given orphanage to allocate objects if necessary.

  virtual kj::Maybe<const Module&> importRelative(kj::StringPtr importPath) const = 0;
  // Find another module, relative to this one.  Importing the same logical module twice should
  // produce the exact same object, comparable by identity.  These objects are owned by some
  // outside pool that outlives the Compiler instance.
};

class Compiler {
  // Cross-links separate modules (schema files) and translates them into schema nodes.

public:
  explicit Compiler();
  ~Compiler();
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

  Schema add(Module<ParsedFile::Reader>& module, Mode mode) const;
  // Add a module to the Compiler, returning its root Schema object.

  const SchemaLoader& getLoader() const;
  // Get a SchemaLoader backed by this compiler.  Schema nodes will be lazily constructed as you
  // traverse them using this loader.

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
