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

#include "null-common.h"

namespace capnp {
namespace benchmark {
namespace null {

enum class Operation {
  ADD,
  SUBTRACT,
  MULTIPLY,
  DIVIDE,
  MODULUS
};
uint OPERATION_RANGE = static_cast<uint>(Operation::MODULUS) + 1;

struct Expression {
  Operation op;

  bool leftIsValue;
  bool rightIsValue;

  union {
    int32_t leftValue;
    Expression* leftExpression;
  };

  union {
    int32_t rightValue;
    Expression* rightExpression;
  };
};

int32_t makeExpression(Expression* exp, uint depth) {
  exp->op = (Operation)(fastRand(OPERATION_RANGE));

  int32_t left, right;

  if (fastRand(8) < depth) {
    exp->leftIsValue = true;
    left = fastRand(128) + 1;
    exp->leftValue = left;
  } else {
    exp->leftIsValue = false;
    exp->leftExpression = allocate<Expression>();
    left = makeExpression(exp->leftExpression, depth + 1);
  }

  if (fastRand(8) < depth) {
    exp->rightIsValue = true;
    right = fastRand(128) + 1;
    exp->rightValue = right;
  } else {
    exp->rightIsValue = false;
    exp->rightExpression = allocate<Expression>();
    right = makeExpression(exp->rightExpression, depth + 1);
  }

  switch (exp->op) {
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

  if (exp.leftIsValue) {
    left = exp.leftValue;
  } else {
    left = evaluateExpression(*exp.leftExpression);
  }

  if (exp.rightIsValue) {
    right = exp.rightValue;
  } else {
    right = evaluateExpression(*exp.rightExpression);
  }

  switch (exp.op) {
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
  typedef int32_t Response;
  typedef int32_t Expectation;

  static inline int32_t setupRequest(Expression* request) {
    return makeExpression(request, 0);
  }
  static inline void handleRequest(const Expression& request, int32_t* response) {
    *response = evaluateExpression(request);
  }
  static inline bool checkResponse(int32_t response, int32_t expected) {
    return response == expected;
  }

  static size_t spaceUsed(const Expression& expression) {
    return sizeof(Expression) +
        (expression.leftExpression == nullptr ? 0 : spaceUsed(*expression.leftExpression)) +
        (expression.rightExpression == nullptr ? 0 : spaceUsed(*expression.rightExpression));
  }
};

}  // namespace null
}  // namespace benchmark
}  // namespace capnp

int main(int argc, char* argv[]) {
  return capnp::benchmark::benchmarkMain<
      capnp::benchmark::null::BenchmarkTypes,
      capnp::benchmark::null::ExpressionTestCase>(argc, argv);
}
