# Copyright (c) 2013-2014 Sandstorm Development Group, Inc. and contributors
# Licensed under the MIT License:
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.

@0x85150b117366d14b;

interface Calculator {
  # A "simple" mathematical calculator, callable via RPC.
  #
  # But, to show off Cap'n Proto, we add some twists:
  #
  # - You can use the result from one call as the input to the next
  #   without a network round trip.  To accomplish this, evaluate()
  #   returns a `Value` object wrapping the actual numeric value.
  #   This object may be used in a subsequent expression.  With
  #   promise pipelining, the Value can actually be used before
  #   the evaluate() call that creates it returns!
  #
  # - You can define new functions, and then call them.  This again
  #   shows off pipelining, but it also gives the client the
  #   opportunity to define a function on the client side and have
  #   the server call back to it.
  #
  # - The basic arithmetic operators are exposed as Functions, and
  #   you have to call getOperator() to obtain them from the server.
  #   This again demonstrates pipelining -- using getOperator() to
  #   get each operator and then using them in evaluate() still
  #   only takes one network round trip.

  evaluate @0 (expression :Expression) -> (value :Value);
  # Evaluate the given expression and return the result.  The
  # result is returned wrapped in a Value interface so that you
  # may pass it back to the server in a pipelined request.  To
  # actually get the numeric value, you must call read() on the
  # Value -- but again, this can be pipelined so that it incurs
  # no additional latency.

  struct Expression {
    # A numeric expression.

    union {
      literal @0 :Float64;
      # A literal numeric value.

      previousResult @1 :Value;
      # A value that was (or, will be) returned by a previous
      # evaluate().

      parameter @2 :UInt32;
      # A parameter to the function (only valid in function bodies;
      # see defFunction).

      call :group {
        # Call a function on a list of parameters.
        function @3 :Function;
        params @4 :List(Expression);
      }
    }
  }

  interface Value {
    # Wraps a numeric value in an RPC object.  This allows the value
    # to be used in subsequent evaluate() requests without the client
    # waiting for the evaluate() that returns the Value to finish.

    read @0 () -> (value :Float64);
    # Read back the raw numeric value.
  }

  defFunction @1 (paramCount :Int32, body :Expression)
              -> (func :Function);
  # Define a function that takes `paramCount` parameters and returns the
  # evaluation of `body` after substituting these parameters.

  interface Function {
    # An algebraic function.  Can be called directly, or can be used inside
    # an Expression.
    #
    # A client can create a Function that runs on the server side using
    # `defFunction()` or `getOperator()`.  Alternatively, a client can
    # implement a Function on the client side and the server will call back
    # to it.  However, a function defined on the client side will require a
    # network round trip whenever the server needs to call it, whereas
    # functions defined on the server and then passed back to it are called
    # locally.

    call @0 (params :List(Float64)) -> (value :Float64);
    # Call the function on the given parameters.
  }

  getOperator @2 (op :Operator) -> (func :Function);
  # Get a Function representing an arithmetic operator, which can then be
  # used in Expressions.

  enum Operator {
    add @0;
    subtract @1;
    multiply @2;
    divide @3;
  }
}
