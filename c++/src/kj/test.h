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

#ifndef KJ_TEST_H_
#define KJ_TEST_H_

#if defined(__GNUC__) && !KJ_HEADER_WARNINGS
#pragma GCC system_header
#endif

#include "debug.h"
#include "vector.h"

namespace kj {

class TestRunner;

class TestCase {
public:
  TestCase(const char* file, uint line, const char* description);
  ~TestCase();

  virtual void run() = 0;

private:
  const char* file;
  uint line;
  const char* description;
  TestCase* next;
  TestCase** prev;
  bool matchedFilter;

  friend class TestRunner;
};

#define KJ_TEST(description) \
  /* Make sure the linker fails if tests are not in anonymous namespaces. */ \
  extern int KJ_CONCAT(YouMustWrapTestsInAnonymousNamespace, __COUNTER__) KJ_UNUSED; \
  class KJ_UNIQUE_NAME(TestCase): public ::kj::TestCase { \
  public: \
    KJ_UNIQUE_NAME(TestCase)(): ::kj::TestCase(__FILE__, __LINE__, description) {} \
    void run() override; \
  } KJ_UNIQUE_NAME(testCase); \
  void KJ_UNIQUE_NAME(TestCase)::run()

#if _MSC_VER
#define KJ_FAIL_EXPECT(...) \
  KJ_EXPAND(KJ_LOG(ERROR, __VA_ARGS__));
#define KJ_EXPECT(cond, ...) \
  if (cond); else KJ_EXPAND(KJ_FAIL_EXPECT("failed: expected " #cond, __VA_ARGS__))
#else
#define KJ_FAIL_EXPECT(...) \
  KJ_LOG(ERROR, ##__VA_ARGS__);
#define KJ_EXPECT(cond, ...) \
  if (cond); else KJ_FAIL_EXPECT("failed: expected " #cond, ##__VA_ARGS__)
#endif

// =======================================================================================

namespace _ {  // private

class GlobFilter {
  // Implements glob filters for the --filter flag.
  //
  // Exposed in header only for testing.

public:
  explicit GlobFilter(const char* pattern);
  explicit GlobFilter(ArrayPtr<const char> pattern);

  bool matches(StringPtr name);

private:
  String pattern;
  Vector<uint> states;

  void applyState(char c, int state);
};

}  // namespace _ (private)
}  // namespace kj

#endif // KJ_TEST_H_
