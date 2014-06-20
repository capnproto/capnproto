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
