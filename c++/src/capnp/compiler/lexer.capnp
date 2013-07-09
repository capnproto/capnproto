@0xa73956d2621fc3ee;

using Cxx = import "/capnp/c++.capnp";

$Cxx.namespace("capnp::compiler");

struct Token {
  body @0 union {
    identifier @1 :Text;
    stringLiteral @2 :Text;
    integerLiteral @3 :UInt64;
    floatLiteral @4 :Float64;
    operator @5 :Text;
    parenthesizedList @6 :List(List(TokenPointer));
    bracketedList @7 :List(List(TokenPointer));
  }

  startByte @8 :UInt32;
  endByte @9 :UInt32;
}

struct TokenPointer {
  # Hack to deal with the fact that struct lists cannot adopt elements.
  #
  # TODO(cleanup):  Find a better approach.

  token @0 :Token;
}

struct Statement {
  tokens @0 :List(TokenPointer);
  block @1 union {
    none @2 :Void;
    statements @3 :List(StatementPointer);
  }

  docComment @4 :Text;
}

struct StatementPointer {
  # Hack to deal with the fact that struct lists cannot adopt elements.
  #
  # TODO(cleanup):  Find a better approach.

  statement @0 :Statement;
}

struct LexedTokens {
  # Lexer output when asked to parse tokens that don't form statements.

  tokens @0 :List(TokenPointer);
}

struct LexedStatements {
  # Lexer output when asked to parse statements.

  statements @0 :List(StatementPointer);
}
