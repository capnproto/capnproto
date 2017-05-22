// Copyright (c) 2017 Sandstorm Development Group, Inc. and contributors
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

#include "type-id.h"
#include <capnp/schema.capnp.h>
#include <kj/test.h>

namespace capnp {
namespace compiler {
namespace {

KJ_TEST("type ID generation hasn't changed") {
  KJ_EXPECT(generateChildId(0xa93fc509624c72d9ull, "Node") == 0xe682ab4cf923a417ull);
  KJ_EXPECT(generateChildId(0xe682ab4cf923a417ull, "NestedNode") == 0xdebf55bbfa0fc242ull);
  KJ_EXPECT(generateGroupId(0xe682ab4cf923a417ull, 7) == 0x9ea0b19b37fb4435ull);

  KJ_EXPECT(typeId<schema::Node>() == 0xe682ab4cf923a417ull);
  KJ_EXPECT(typeId<schema::Node::NestedNode>() == 0xdebf55bbfa0fc242ull);
  KJ_EXPECT(typeId<schema::Node::Struct>() == 0x9ea0b19b37fb4435ull);

  // Methods of TestInterface.
  KJ_EXPECT(generateMethodParamsId(0x88eb12a0e0af92b2ull, 0, false) == 0xb874edc0d559b391ull);
  KJ_EXPECT(generateMethodParamsId(0x88eb12a0e0af92b2ull, 0, true) == 0xb04fcaddab714ba4ull);
  KJ_EXPECT(generateMethodParamsId(0x88eb12a0e0af92b2ull, 1, false) == 0xd044893357b42568ull);
  KJ_EXPECT(generateMethodParamsId(0x88eb12a0e0af92b2ull, 1, true) == 0x9bf141df4247d52full);
}

}  // namespace
}  // namespace compiler
}  // namespace capnp
