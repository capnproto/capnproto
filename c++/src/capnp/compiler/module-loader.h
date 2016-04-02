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

#ifndef CAPNP_COMPILER_MODULE_LOADER_H_
#define CAPNP_COMPILER_MODULE_LOADER_H_

#if defined(__GNUC__) && !defined(CAPNP_HEADER_WARNINGS)
#pragma GCC system_header
#endif

#include "compiler.h"
#include "error-reporter.h"
#include <kj/memory.h>
#include <kj/array.h>
#include <kj/string.h>

namespace capnp {
namespace compiler {

class ModuleLoader {
public:
  explicit ModuleLoader(GlobalErrorReporter& errorReporter);
  // Create a ModuleLoader that reports error messages to the given reporter.

  KJ_DISALLOW_COPY(ModuleLoader);

  ~ModuleLoader() noexcept(false);

  void addImportPath(kj::String path);
  // Add a directory to the list of paths that is searched for imports that start with a '/'.

  kj::Maybe<Module&> loadModule(kj::StringPtr localName, kj::StringPtr sourceName);
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
