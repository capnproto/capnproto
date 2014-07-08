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

@0xa73956d2621fc3ee;

using Cxx = import "/capnp/c++.capnp";

$Cxx.namespace("capnp::compiler");

struct Token {
  union {
    identifier @0 :Text;
    stringLiteral @1 :Text;
    binaryLiteral @9 :Data;
    integerLiteral @2 :UInt64;
    floatLiteral @3 :Float64;
    operator @4 :Text;
    parenthesizedList @5 :List(List(Token));
    bracketedList @6 :List(List(Token));
  }

  startByte @7 :UInt32;
  endByte @8 :UInt32;
}

struct Statement {
  tokens @0 :List(Token);
  union {
    line @1 :Void;
    block @2 :List(Statement);
  }

  docComment @3 :Text;

  startByte @4 :UInt32;
  endByte @5 :UInt32;
}

struct LexedTokens {
  # Lexer output when asked to parse tokens that don't form statements.

  tokens @0 :List(Token);
}

struct LexedStatements {
  # Lexer output when asked to parse statements.

  statements @0 :List(Statement);
}
