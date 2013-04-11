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

#include "eval.capnp.h"
#include "capnproto-common.h"

namespace capnproto {
namespace benchmark {
namespace capnp {

int32_t makeExpression(Expression::Builder exp, uint depth) {
  // TODO:  Operation_RANGE or something.
  exp.setOp((Operation)(fastRand((int)Operation::MODULUS + 1)));

  uint32_t left, right;

  if (fastRand(8) < depth) {
    left = fastRand(128) + 1;
    exp.setLeftValue(left);
  } else {
    left = makeExpression(exp.initLeftExpression(), depth + 1);
  }

  if (fastRand(8) < depth) {
    right = fastRand(128) + 1;
    exp.setRightValue(right);
  } else {
    right = makeExpression(exp.initRightExpression(), depth + 1);
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

  switch (exp.whichLeft()) {
    case Expression::Left::LEFT_VALUE:
      left = exp.getLeftValue();
      break;
    case Expression::Left::LEFT_EXPRESSION:
      left = evaluateExpression(exp.getLeftExpression());
      break;
  }

  switch (exp.whichRight()) {
    case Expression::Right::RIGHT_VALUE:
      right = exp.getRightValue();
      break;
    case Expression::Right::RIGHT_EXPRESSION:
      right = evaluateExpression(exp.getRightExpression());
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
  ~ExpressionTestCase() {}

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
}  // namespace capnproto

int main(int argc, char* argv[]) {
  return capnproto::benchmark::benchmarkMain<
      capnproto::benchmark::capnp::BenchmarkTypes,
      capnproto::benchmark::capnp::ExpressionTestCase>(argc, argv);
}
