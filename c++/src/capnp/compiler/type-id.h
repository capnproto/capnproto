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

#pragma once

#include <kj/string.h>
#include <kj/array.h>
#include <capnp/common.h>

CAPNP_BEGIN_HEADER

namespace capnp {
namespace compiler {

uint64_t generateChildId(uint64_t parentId, kj::StringPtr childName);
uint64_t generateGroupId(uint64_t parentId, uint16_t groupIndex);
uint64_t generateMethodParamsId(uint64_t parentId, uint16_t methodOrdinal, bool isResults);
// Generate a default type ID for various symbols. These are used only if the developer did not
// specify an ID explicitly.
//
// The returned ID always has the most-significant bit set. The remaining bits are generated
// pseudo-randomly from the input using an algorithm that should produce a uniform distribution of
// IDs.

}  // namespace compiler
}  // namespace capnp

CAPNP_END_HEADER
