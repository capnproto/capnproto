# Copyright (c) 2013-2014 Sandstorm Development Group, Inc. and contributors
# Licensed under the MIT License:
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.

@0xc64a3bf0338a124a;

using Cxx = import "/capnp/c++.capnp";
using Import1 = import "/capnp/schema.capnp";
using Import2 = import "test-import.capnp";
using Import3 = import "test.capnp";

$Cxx.omitSchemas;

struct TestImport2 {
  foo @0 :Import3.TestAllTypes;
  bar @1 :Import1.Node;
  baz @2 :Import2.TestImport;
}

interface TestImport2Interface {
  testInterfaceMethod @0 () -> (result :Bool);
}

interface TestImport2ExtendingInterface extends(TestImport2Interface) {
  testInterfaceMethodTwo @0 () -> (result :Bool);
}

enum TestImport2Enum {
  zero @0;
  one @1;
  two @2;
  three @3;
  four @4;
}

struct TestImport2Structure {
  enumValue @0 :TestImport2Enum;
  anInterface @1 :TestImport2ExtendingInterface;
}
