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

#include "calculator.capnp.h"
#include <kj/debug.h>
#include <capnp/ez-rpc.h>
#include <capnp/message.h>
#include <iostream>

typedef unsigned int uint;

kj::Promise<double> readValue(Calculator::Value::Client value) {
  // Helper function to asynchronously call read() on a Calculator::Value and
  // return a promise for the result.  (In the future, the generated code might
  // include something like this automatically.)

  return value.readRequest().send()
      .then([](capnp::Response<Calculator::Value::ReadResults> result) {
    return result.getValue();
  });
}

kj::Promise<double> evaluateImpl(
    Calculator::Expression::Reader expression,
    capnp::List<double>::Reader params = capnp::List<double>::Reader()) {
  // Implementation of CalculatorImpl::evaluate(), also shared by
  // FunctionImpl::call().  In the latter case, `params` are the parameter
  // values passed to the function; in the former case, `params` is just an
  // empty list.

  switch (expression.which()) {
    case Calculator::Expression::LITERAL:
      return expression.getLiteral();

    case Calculator::Expression::PREVIOUS_RESULT:
      return readValue(expression.getPreviousResult());

    case Calculator::Expression::PARAMETER: {
      KJ_REQUIRE(expression.getParameter() < params.size(),
                 "Parameter index out-of-range.");
      return params[expression.getParameter()];
    }

    case Calculator::Expression::CALL: {
      auto call = expression.getCall();
      auto func = call.getFunction();

      // Evaluate each parameter.
      kj::Array<kj::Promise<double>> paramPromises =
          KJ_MAP(param, call.getParams()) {
            return evaluateImpl(param, params);
          };

      // Join the array of promises into a promise for an array.
      kj::Promise<kj::Array<double>> joinedParams =
          kj::joinPromises(kj::mv(paramPromises));

      // When the parameters are complete, call the function.
      return joinedParams.then([KJ_CPCAP(func)](kj::Array<double>&& paramValues) mutable {
        auto request = func.callRequest();
        request.setParams(paramValues);
        return request.send().then(
            [](capnp::Response<Calculator::Function::CallResults>&& result) {
          return result.getValue();
        });
      });
    }

    default:
      // Throw an exception.
      KJ_FAIL_REQUIRE("Unknown expression type.");
  }
}

class ValueImpl final: public Calculator::Value::Server {
  // Simple implementation of the Calculator.Value Cap'n Proto interface.

public:
  ValueImpl(double value): value(value) {}

  kj::Promise<void> read(ReadContext context) {
    context.getResults().setValue(value);
    return kj::READY_NOW;
  }

private:
  double value;
};

class FunctionImpl final: public Calculator::Function::Server {
  // Implementation of the Calculator.Function Cap'n Proto interface, where the
  // function is defined by a Calculator.Expression.

public:
  FunctionImpl(uint paramCount, Calculator::Expression::Reader body)
      : paramCount(paramCount) {
    this->body.setRoot(body);
  }

  kj::Promise<void> call(CallContext context) {
    auto params = context.getParams().getParams();
    KJ_REQUIRE(params.size() == paramCount, "Wrong number of parameters.");

    return evaluateImpl(body.getRoot<Calculator::Expression>(), params)
        .then([KJ_CPCAP(context)](double value) mutable {
      context.getResults().setValue(value);
    });
  }

private:
  uint paramCount;
  // The function's arity.

  capnp::MallocMessageBuilder body;
  // Stores a permanent copy of the function body.
};

class OperatorImpl final: public Calculator::Function::Server {
  // Implementation of the Calculator.Function Cap'n Proto interface, wrapping
  // basic binary arithmetic operators.

public:
  OperatorImpl(Calculator::Operator op): op(op) {}

  kj::Promise<void> call(CallContext context) {
    auto params = context.getParams().getParams();
    KJ_REQUIRE(params.size() == 2, "Wrong number of parameters.");

    double result;
    switch (op) {
      case Calculator::Operator::ADD:     result = params[0] + params[1]; break;
      case Calculator::Operator::SUBTRACT:result = params[0] - params[1]; break;
      case Calculator::Operator::MULTIPLY:result = params[0] * params[1]; break;
      case Calculator::Operator::DIVIDE:  result = params[0] / params[1]; break;
      default:
        KJ_FAIL_REQUIRE("Unknown operator.");
    }

    context.getResults().setValue(result);
    return kj::READY_NOW;
  }

private:
  Calculator::Operator op;
};

class CalculatorImpl final: public Calculator::Server {
  // Implementation of the Calculator Cap'n Proto interface.

public:
  kj::Promise<void> evaluate(EvaluateContext context) override {
    return evaluateImpl(context.getParams().getExpression())
        .then([KJ_CPCAP(context)](double value) mutable {
      context.getResults().setValue(kj::heap<ValueImpl>(value));
    });
  }

  kj::Promise<void> defFunction(DefFunctionContext context) override {
    auto params = context.getParams();
    context.getResults().setFunc(kj::heap<FunctionImpl>(
        params.getParamCount(), params.getBody()));
    return kj::READY_NOW;
  }

  kj::Promise<void> getOperator(GetOperatorContext context) override {
    context.getResults().setFunc(kj::heap<OperatorImpl>(
        context.getParams().getOp()));
    return kj::READY_NOW;
  }
};

int main(int argc, const char* argv[]) {
  if (argc != 2) {
    std::cerr << "usage: " << argv[0] << " ADDRESS[:PORT]\n"
        "Runs the server bound to the given address/port.\n"
        "ADDRESS may be '*' to bind to all local addresses.\n"
        ":PORT may be omitted to choose a port automatically." << std::endl;
    return 1;
  }

  // Set up a server.
  capnp::EzRpcServer server(kj::heap<CalculatorImpl>(), argv[1]);

  // Write the port number to stdout, in case it was chosen automatically.
  auto& waitScope = server.getWaitScope();
  uint port = server.getPort().wait(waitScope);
  if (port == 0) {
    // The address format "unix:/path/to/socket" opens a unix domain socket,
    // in which case the port will be zero.
    std::cout << "Listening on Unix socket..." << std::endl;
  } else {
    std::cout << "Listening on port " << port << "..." << std::endl;
  }

  // Run forever, accepting connections and handling requests.
  kj::NEVER_DONE.wait(waitScope);
}
