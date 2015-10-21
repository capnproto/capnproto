// Copyright (c) 2015 Philip Quinn.
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

#include "serialize-text.h"

#include <kj/debug.h>

#include "pretty-print.h"
#include "compiler/lexer.capnp.h"
#include "compiler/lexer.h"
#include "compiler/node-translator.h"
#include "compiler/parser.h"

namespace {

class ThrowingErrorReporter final: public capnp::compiler::ErrorReporter {
  // Throws all errors as assertion failures.
public:
  void addError(uint32_t startByte, uint32_t endByte, kj::StringPtr message) override {
    KJ_FAIL_REQUIRE(kj::str(message, " (", startByte, ":", endByte, ")."));
  }

  bool hadErrors() override { return false; }
};

class ExternalResolver final: public capnp::compiler::ValueTranslator::Resolver {
  // Throws all external resolution requests as assertion failures.
public:
  kj::Maybe<capnp::DynamicValue::Reader>
  resolveConstant(capnp::compiler::Expression::Reader name) override {
    KJ_FAIL_REQUIRE("External constants not allowed.");
  }

  kj::Maybe<kj::Array<const capnp::byte>>
  readEmbed(capnp::compiler::LocatedText::Reader filename) override {
    KJ_FAIL_REQUIRE("External embeds not allowed.");
  }
};

template <typename Function>
void lexAndParseExpression(kj::StringPtr input, Function f) {
  // Parses a single expression from the input and calls `f(expression)`.

  ThrowingErrorReporter errorReporter;

  capnp::MallocMessageBuilder tokenArena;
  auto lexedTokens = tokenArena.initRoot<capnp::compiler::LexedTokens>();
  capnp::compiler::lex(input, lexedTokens, errorReporter);

  capnp::compiler::CapnpParser parser(tokenArena.getOrphanage(), errorReporter);
  auto tokens = lexedTokens.asReader().getTokens();
  capnp::compiler::CapnpParser::ParserInput parserInput(tokens.begin(), tokens.end());

  if (parserInput.getPosition() != tokens.end()) {
    KJ_IF_MAYBE(expression, parser.getParsers().expression(parserInput)) {
      // The input is expected to contain a *single* message.
      KJ_REQUIRE(parserInput.getPosition() == tokens.end(), "Extra tokens in input.");

      f(expression->getReader());
    } else {
      auto best = parserInput.getBest();
      if (best == tokens.end()) {
        KJ_FAIL_REQUIRE("Premature end of input.");
      } else {
        errorReporter.addErrorOn(*best, "Parse error");
      }
    }
  } else {
    KJ_FAIL_REQUIRE("Failed to read input.");
  }
}

}  // namespace

namespace capnp {

TextCodec::TextCodec() : prettyPrint(false) {}
TextCodec::~TextCodec() noexcept(true) {}

void TextCodec::setPrettyPrint(bool enabled) { prettyPrint = enabled; }

kj::String TextCodec::encode(DynamicValue::Reader value) const {
  if (!prettyPrint) {
    return kj::str(value);
  } else {
    if (value.getType() == DynamicValue::Type::STRUCT) {
      return capnp::prettyPrint(value.as<DynamicStruct>()).flatten();
    } else if (value.getType() == DynamicValue::Type::LIST) {
      return capnp::prettyPrint(value.as<DynamicList>()).flatten();
    } else {
      return kj::str(value);
    }
  }
}

void TextCodec::decode(kj::StringPtr input, DynamicStruct::Builder output) const {
  lexAndParseExpression(input, [&output](compiler::Expression::Reader expression) {
    KJ_REQUIRE(expression.isTuple(), "Input does not contain a struct.");

    ThrowingErrorReporter errorReporter;
    ExternalResolver nullResolver;

    Orphanage orphanage = Orphanage::getForMessageContaining(output);
    compiler::ValueTranslator translator(nullResolver, errorReporter, orphanage);
    translator.fillStructValue(output, expression.getTuple());
  });
}

Orphan<DynamicValue> TextCodec::decode(kj::StringPtr input, Type type, Orphanage orphanage) const {
  Orphan<DynamicValue> output;
  
  lexAndParseExpression(input, [&type, &orphanage, &output](compiler::Expression::Reader expression) {
    ThrowingErrorReporter errorReporter;
    ExternalResolver nullResolver;

    compiler::ValueTranslator translator(nullResolver, errorReporter, orphanage);
    KJ_IF_MAYBE(value, translator.compileValue(expression, type)) {
      output = *kj::mv(value);
    } else {
      // An error should have already been given to the errorReporter.
    }
  });
  
  return output;
}

}  // namespace capnp
