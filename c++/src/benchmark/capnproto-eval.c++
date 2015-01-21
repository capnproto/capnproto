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

#include "eval.capnp.h"
#include "capnproto-common.h"

namespace capnp {
namespace benchmark {
namespace capnp {

int32_t makeExpression(Expression::Builder exp, uint depth) {
  exp.setOp((Operation)(fastRand((int)Operation::MODULUS + 1)));

  uint32_t left, right;

  if (fastRand(8) < depth) {
    left = fastRand(128) + 1;
    exp.getLeft().setValue(left);
  } else {
    left = makeExpression(exp.getLeft().initExpression(), depth + 1);
  }

  if (fastRand(8) < depth) {
    right = fastRand(128) + 1;
    exp.getRight().setValue(right);
  } else {
    right = makeExpression(exp.getRight().initExpression(), depth + 1);
  }

  switch (exp.getOp()) {
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

int32_t evaluateExpression(Expression::Reader exp) {
  int32_t left = 0, right = 0;

  switch (exp.getLeft().which()) {
    case Expression::Left::VALUE:
      left = exp.getLeft().getValue();
      break;
    case Expression::Left::EXPRESSION:
      left = evaluateExpression(exp.getLeft().getExpression());
      break;
  }

  switch (exp.getRight().which()) {
    case Expression::Right::VALUE:
      right = exp.getRight().getValue();
      break;
    case Expression::Right::EXPRESSION:
      right = evaluateExpression(exp.getRight().getExpression());
      break;
  }

  switch (exp.getOp()) {
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

  static inline int32_t setupRequest(Expression::Builder request) {
    return makeExpression(request, 0);
  }
  static inline void handleRequest(Expression::Reader request, EvaluationResult::Builder response) {
    response.setValue(evaluateExpression(request));
  }
  static inline bool checkResponse(EvaluationResult::Reader response, int32_t expected) {
    return response.getValue() == expected;
  }
};

}  // namespace capnp
}  // namespace benchmark
}  // namespace capnp

int main(int argc, char* argv[]) {
  return capnp::benchmark::benchmarkMain<
      capnp::benchmark::capnp::BenchmarkTypes,
      capnp::benchmark::capnp::ExpressionTestCase>(argc, argv);
}
