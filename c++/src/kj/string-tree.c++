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

#include "string-tree.h"

namespace kj {

StringTree::StringTree(Array<StringTree>&& pieces, StringPtr delim)
    : size_(0),
      branches(heapArray<Branch>(pieces.size())) {
  if (pieces.size() > 0) {
    if (pieces.size() > 1 && delim.size() > 0) {
      text = heapString((pieces.size() - 1) * delim.size());
      size_ = text.size();
    }

    branches[0].index = 0;
    branches[0].content = kj::mv(pieces[0]);
    size_ += pieces[0].size();

    for (uint i = 1; i < pieces.size(); i++) {
      if (delim.size() > 0) {
        memcpy(text.begin() + (i - 1) * delim.size(), delim.begin(), delim.size());
      }
      branches[i].index = i * delim.size();
      branches[i].content = kj::mv(pieces[i]);
      size_ += pieces[i].size();
    }
  }
}

String StringTree::flatten() const {
  String result = heapString(size());
  flattenTo(result.begin());
  return result;
}

void StringTree::flattenTo(char* __restrict__ target) const {
  visit([&target](ArrayPtr<const char> text) {
    memcpy(target, text.begin(), text.size());
    target += text.size();
  });
}

}  // namespace kj
