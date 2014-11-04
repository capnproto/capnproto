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
#include <capnp/ez-rpc.h>
#include <kj/debug.h>
#include <math.h>
#include <iostream>

class PowerFunction final: public Calculator::Function::Server {
  // An implementation of the Function interface wrapping pow().  Note that
  // we're implementing this on the client side and will pass a reference to
  // the server.  The server will then be able to make calls back to the client.

public:
  kj::Promise<void> call(CallContext context) {
    auto params = context.getParams().getParams();
    KJ_REQUIRE(params.size() == 2, "Wrong number of parameters.");
    context.getResults().setValue(pow(params[0], params[1]));
    return kj::READY_NOW;
  }
};

int main(int argc, const char* argv[]) {
  if (argc != 2) {
    std::cerr << "usage: " << argv[0] << " HOST:PORT\n"
        "Connects to the Calculator server at the given address and "
        "does some RPCs." << std::endl;
    return 1;
  }

  capnp::EzRpcClient client(argv[1]);
  Calculator::Client calculator = client.getMain<Calculator>();

  // Keep an eye on `waitScope`.  Whenever you see it used is a place where we
  // stop and wait for the server to respond.  If a line of code does not use
  // `waitScope`, then it does not block!
  auto& waitScope = client.getWaitScope();

  {
    // Make a request that just evaluates the literal value 123.
    //
    // What's interesting here is that evaluate() returns a "Value", which is
    // another interface and therefore points back to an object living on the
    // server.  We then have to call read() on that object to read it.
    // However, even though we are making two RPC's, this block executes in
    // *one* network round trip because of promise pipelining:  we do not wait
    // for the first call to complete before we send the second call to the
    // server.

    std::cout << "Evaluating a literal... ";
    std::cout.flush();

    // Set up the request.
    auto request = calculator.evaluateRequest();
    request.getExpression().setLiteral(123);

    // Send it, which returns a promise for the result (without blocking).
    auto evalPromise = request.send();

    // Using the promise, create a pipelined request to call read() on the
    // returned object, and then send that.
    auto readPromise = evalPromise.getValue().readRequest().send();

    // Now that we've sent all the requests, wait for the response.  Until this
    // point, we haven't waited at all!
    auto response = readPromise.wait(waitScope);
    KJ_ASSERT(response.getValue() == 123);

    std::cout << "PASS" << std::endl;
  }

  {
    // Make a request to evaluate 123 + 45 - 67.
    //
    // The Calculator interface requires that we first call getOperator() to
    // get the addition and subtraction functions, then call evaluate() to use
    // them.  But, once again, we can get both functions, call evaluate(), and
    // then read() the result -- four RPCs -- in the time of *one* network
    // round trip, because of promise pipelining.

    std::cout << "Using add and subtract... ";
    std::cout.flush();

    Calculator::Function::Client add = nullptr;
    Calculator::Function::Client subtract = nullptr;

    {
      // Get the "add" function from the server.
      auto request = calculator.getOperatorRequest();
      request.setOp(Calculator::Operator::ADD);
      add = request.send().getFunc();
    }

    {
      // Get the "subtract" function from the server.
      auto request = calculator.getOperatorRequest();
      request.setOp(Calculator::Operator::SUBTRACT);
      subtract = request.send().getFunc();
    }

    // Build the request to evaluate 123 + 45 - 67.
    auto request = calculator.evaluateRequest();

    auto subtractCall = request.getExpression().initCall();
    subtractCall.setFunction(subtract);
    auto subtractParams = subtractCall.initParams(2);
    subtractParams[1].setLiteral(67);

    auto addCall = subtractParams[0].initCall();
    addCall.setFunction(add);
    auto addParams = addCall.initParams(2);
    addParams[0].setLiteral(123);
    addParams[1].setLiteral(45);

    // Send the evaluate() request, read() the result, and wait for read() to
    // finish.
    auto evalPromise = request.send();
    auto readPromise = evalPromise.getValue().readRequest().send();

    auto response = readPromise.wait(waitScope);
    KJ_ASSERT(response.getValue() == 101);

    std::cout << "PASS" << std::endl;
  }

  {
    // Make a request to evaluate 4 * 6, then use the result in two more
    // requests that add 3 and 5.
    //
    // Since evaluate() returns its result wrapped in a `Value`, we can pass
    // that `Value` back to the server in subsequent requests before the first
    // `evaluate()` has actually returned.  Thus, this example again does only
    // one network round trip.

    std::cout << "Pipelining eval() calls... ";
    std::cout.flush();

    Calculator::Function::Client add = nullptr;
    Calculator::Function::Client multiply = nullptr;

    {
      // Get the "add" function from the server.
      auto request = calculator.getOperatorRequest();
      request.setOp(Calculator::Operator::ADD);
      add = request.send().getFunc();
    }

    {
      // Get the "multiply" function from the server.
      auto request = calculator.getOperatorRequest();
      request.setOp(Calculator::Operator::MULTIPLY);
      multiply = request.send().getFunc();
    }

    // Build the request to evaluate 4 * 6
    auto request = calculator.evaluateRequest();

    auto multiplyCall = request.getExpression().initCall();
    multiplyCall.setFunction(multiply);
    auto multiplyParams = multiplyCall.initParams(2);
    multiplyParams[0].setLiteral(4);
    multiplyParams[1].setLiteral(6);

    auto multiplyResult = request.send().getValue();

    // Use the result in two calls that add 3 and add 5.

    auto add3Request = calculator.evaluateRequest();
    auto add3Call = add3Request.getExpression().initCall();
    add3Call.setFunction(add);
    auto add3Params = add3Call.initParams(2);
    add3Params[0].setPreviousResult(multiplyResult);
    add3Params[1].setLiteral(3);
    auto add3Promise = add3Request.send().getValue().readRequest().send();

    auto add5Request = calculator.evaluateRequest();
    auto add5Call = add5Request.getExpression().initCall();
    add5Call.setFunction(add);
    auto add5Params = add5Call.initParams(2);
    add5Params[0].setPreviousResult(multiplyResult);
    add5Params[1].setLiteral(5);
    auto add5Promise = add5Request.send().getValue().readRequest().send();

    // Now wait for the results.
    KJ_ASSERT(add3Promise.wait(waitScope).getValue() == 27);
    KJ_ASSERT(add5Promise.wait(waitScope).getValue() == 29);

    std::cout << "PASS" << std::endl;
  }

  {
    // Our calculator interface supports defining functions.  Here we use it
    // to define two functions and then make calls to them as follows:
    //
    //   f(x, y) = x * 100 + y
    //   g(x) = f(x, x + 1) * 2;
    //   f(12, 34)
    //   g(21)
    //
    // Once again, the whole thing takes only one network round trip.

    std::cout << "Defining functions... ";
    std::cout.flush();

    Calculator::Function::Client add = nullptr;
    Calculator::Function::Client multiply = nullptr;
    Calculator::Function::Client f = nullptr;
    Calculator::Function::Client g = nullptr;

    {
      // Get the "add" function from the server.
      auto request = calculator.getOperatorRequest();
      request.setOp(Calculator::Operator::ADD);
      add = request.send().getFunc();
    }

    {
      // Get the "multiply" function from the server.
      auto request = calculator.getOperatorRequest();
      request.setOp(Calculator::Operator::MULTIPLY);
      multiply = request.send().getFunc();
    }

    {
      // Define f.
      auto request = calculator.defFunctionRequest();
      request.setParamCount(2);

      {
        // Build the function body.
        auto addCall = request.getBody().initCall();
        addCall.setFunction(add);
        auto addParams = addCall.initParams(2);
        addParams[1].setParameter(1);  // y

        auto multiplyCall = addParams[0].initCall();
        multiplyCall.setFunction(multiply);
        auto multiplyParams = multiplyCall.initParams(2);
        multiplyParams[0].setParameter(0);  // x
        multiplyParams[1].setLiteral(100);
      }

      f = request.send().getFunc();
    }

    {
      // Define g.
      auto request = calculator.defFunctionRequest();
      request.setParamCount(1);

      {
        // Build the function body.
        auto multiplyCall = request.getBody().initCall();
        multiplyCall.setFunction(multiply);
        auto multiplyParams = multiplyCall.initParams(2);
        multiplyParams[1].setLiteral(2);

        auto fCall = multiplyParams[0].initCall();
        fCall.setFunction(f);
        auto fParams = fCall.initParams(2);
        fParams[0].setParameter(0);

        auto addCall = fParams[1].initCall();
        addCall.setFunction(add);
        auto addParams = addCall.initParams(2);
        addParams[0].setParameter(0);
        addParams[1].setLiteral(1);
      }

      g = request.send().getFunc();
    }

    // OK, we've defined all our functions.  Now create our eval requests.

    // f(12, 34)
    auto fEvalRequest = calculator.evaluateRequest();
    auto fCall = fEvalRequest.initExpression().initCall();
    fCall.setFunction(f);
    auto fParams = fCall.initParams(2);
    fParams[0].setLiteral(12);
    fParams[1].setLiteral(34);
    auto fEvalPromise = fEvalRequest.send().getValue().readRequest().send();

    // g(21)
    auto gEvalRequest = calculator.evaluateRequest();
    auto gCall = gEvalRequest.initExpression().initCall();
    gCall.setFunction(g);
    gCall.initParams(1)[0].setLiteral(21);
    auto gEvalPromise = gEvalRequest.send().getValue().readRequest().send();

    // Wait for the results.
    KJ_ASSERT(fEvalPromise.wait(waitScope).getValue() == 1234);
    KJ_ASSERT(gEvalPromise.wait(waitScope).getValue() == 4244);

    std::cout << "PASS" << std::endl;
  }

  {
    // Make a request that will call back to a function defined locally.
    //
    // Specifically, we will compute 2^(4 + 5).  However, exponent is not
    // defined by the Calculator server.  So, we'll implement the Function
    // interface locally and pass it to the server for it to use when
    // evaluating the expression.
    //
    // This example requires two network round trips to complete, because the
    // server calls back to the client once before finishing.  In this
    // particular case, this could potentially be optimized by using a tail
    // call on the server side -- see CallContext::tailCall().  However, to
    // keep the example simpler, we haven't implemented this optimization in
    // the sample server.

    std::cout << "Using a callback... ";
    std::cout.flush();

    Calculator::Function::Client add = nullptr;

    {
      // Get the "add" function from the server.
      auto request = calculator.getOperatorRequest();
      request.setOp(Calculator::Operator::ADD);
      add = request.send().getFunc();
    }

    // Build the eval request for 2^(4+5).
    auto request = calculator.evaluateRequest();

    auto powCall = request.getExpression().initCall();
    powCall.setFunction(kj::heap<PowerFunction>());
    auto powParams = powCall.initParams(2);
    powParams[0].setLiteral(2);

    auto addCall = powParams[1].initCall();
    addCall.setFunction(add);
    auto addParams = addCall.initParams(2);
    addParams[0].setLiteral(4);
    addParams[1].setLiteral(5);

    // Send the request and wait.
    auto response = request.send().getValue().readRequest()
                           .send().wait(waitScope);
    KJ_ASSERT(response.getValue() == 512);

    std::cout << "PASS" << std::endl;
  }

  return 0;
}
