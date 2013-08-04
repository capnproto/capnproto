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

#ifndef CAPNP_COMPILER_MODULE_LOADER_H_
#define CAPNP_COMPILER_MODULE_LOADER_H_

#include "compiler.h"
#include "error-reporter.h"
#include <kj/memory.h>
#include <kj/array.h>
#include <kj/string.h>

namespace capnp {
namespace compiler {

class ModuleLoader {
public:
  explicit ModuleLoader(const GlobalErrorReporter& errorReporter);
  // Create a ModuleLoader that reports error messages to the given reporter.

  KJ_DISALLOW_COPY(ModuleLoader);

  ~ModuleLoader() noexcept(false);

  void addImportPath(kj::String path);
  // Add a directory to the list of paths that is searched for imports that start with a '/'.

  kj::Maybe<const Module&> loadModule(kj::StringPtr localName, kj::StringPtr sourceName) const;
  // Tries to load the module with the given filename.  `localName` is the path to the file on
  // disk (as you'd pass to open(2)), and `sourceName` is the canonical name it should be given
  // in the schema (this is used e.g. to decide output file locations).  Often, these are the same.

private:
  class Impl;
  kj::Own<Impl> impl;

  class ModuleImpl;
};

}  // namespace compiler
}  // namespace capnp

#endif  // CAPNP_COMPILER_MODULE_LOADER_H_
