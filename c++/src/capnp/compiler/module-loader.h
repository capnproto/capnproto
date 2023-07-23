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

#include "compiler.h"
#include "error-reporter.h"
#include <kj/memory.h>
#include <kj/array.h>
#include <kj/string.h>
#include <kj/filesystem.h>

CAPNP_BEGIN_HEADER

namespace capnp {
namespace compiler {

class ModuleLoader {
public:
  explicit ModuleLoader(GlobalErrorReporter& errorReporter);
  // Create a ModuleLoader that reports error messages to the given reporter.

  KJ_DISALLOW_COPY_AND_MOVE(ModuleLoader);

  ~ModuleLoader() noexcept(false);

  void addImportPath(const kj::ReadableDirectory& dir);
  // Add a directory to the list of paths that is searched for imports that start with a '/'.

  kj::Maybe<Module&> loadModule(const kj::ReadableDirectory& dir, kj::PathPtr path);
  // Tries to load a module with the given path inside the given directory. Returns nullptr if the
  // file doesn't exist.

  void setFileIdsRequired(bool value);
  // Same as SchemaParser::setFileIdsRequired(). If set false, files will not be required to have
  // a top-level file ID; if missing a random one will be assigned.

private:
  class Impl;
  kj::Own<Impl> impl;

  class ModuleImpl;
};

}  // namespace compiler
}  // namespace capnp

CAPNP_END_HEADER
