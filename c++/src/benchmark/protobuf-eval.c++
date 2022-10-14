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

#if !CAPNP_NO_PROTOBUF_BENCHMARK

#include "eval.pb.h"
#include "protobuf-common.h"

namespace capnp {
namespace benchmark {
namespace protobuf {

int32_t makeExpression(Expression* exp, uint depth) {
  exp->set_op((Operation)(fastRand(Operation_MAX + 1)));

  int32_t left, right;

  if (fastRand(8) < depth) {
    left = fastRand(128) + 1;
    exp->set_left_value(left);
  } else {
    left = makeExpression(exp->mutable_left_expression(), depth + 1);
  }

  if (fastRand(8) < depth) {
    right = fastRand(128) + 1;
    exp->set_right_value(right);
  } else {
    right = makeExpression(exp->mutable_right_expression(), depth + 1);
  }

  switch (exp->op()) {
    case Operation::ADD:
      return left + right;
    case Operation::SUBTRACT:
      return left - right;
    case Operation::MULTIPLY:
      return left * right;
    case Operation::DIVIDE:
      return div(left, right);
    case Operation::MODULUS:
      return mod(left, right);
  }
  throw std::logic_error("Can't get here.");
}

int32_t evaluateExpression(const Expression& exp) {
  uint32_t left, right;

  if (exp.has_left_value()) {
    left = exp.left_value();
  } else {
    left = evaluateExpression(exp.left_expression());
  }

  if (exp.has_right_value()) {
    right = exp.right_value();
  } else {
    right = evaluateExpression(exp.right_expression());
  }

  switch (exp.op()) {
    case Operation::ADD:
      return left + right;
    case Operation::SUBTRACT:
      return left - right;
    case Operation::MULTIPLY:
      return left * right;
    case Operation::DIVIDE:
      return div(left, right);
    case Operation::MODULUS:
      return mod(left, right);
  }
  throw std::logic_error("Can't get here.");
}

class ExpressionTestCase {
public:
  typedef Expression Request;
  typedef EvaluationResult Response;
  typedef int32_t Expectation;

  static inline int32_t setupRequest(Expression* request) {
    return makeExpression(request, 0);
  }
  static inline void handleRequest(const Expression& request, EvaluationResult* response) {
    response->set_value(evaluateExpression(request));
  }
  static inline bool checkResponse(const EvaluationResult& response, int32_t expected) {
    return response.value() == expected;
  }
};

}  // namespace protobuf
}  // namespace benchmark
}  // namespace capnp

int main(int argc, char* argv[]) {
  return capnp::benchmark::benchmarkMain<
      capnp::benchmark::protobuf::BenchmarkTypes,
      capnp::benchmark::protobuf::ExpressionTestCase>(argc, argv);
}

#endif  // !CAPNP_NO_PROTOBUF_BENCHMARK
