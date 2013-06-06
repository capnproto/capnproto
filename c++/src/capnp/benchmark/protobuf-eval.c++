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
