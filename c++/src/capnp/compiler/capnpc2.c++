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

#include "lexer.h"
#include "parser.h"
#include "compiler.h"
#include "module-loader.h"
#include <capnp/pretty-print.h>
#include <kj/vector.h>
#include <kj/io.h>
#include <unistd.h>
#include <kj/debug.h>
#include "../message.h"
#include <iostream>
#include <kj/main.h>

namespace capnp {
namespace compiler {

class DummyModule: public capnp::compiler::Module {
public:
  capnp::compiler::ParsedFile::Reader content;

  kj::StringPtr getLocalName() const {
    return "(stdin)";
  }
  kj::StringPtr getSourceName() const {
    return "(stdin)";
  }
  capnp::Orphan<capnp::compiler::ParsedFile> loadContent(capnp::Orphanage orphanage) const {
    return orphanage.newOrphanCopy(content);
  }
  kj::Maybe<const Module&> importRelative(kj::StringPtr importPath) const {
    return nullptr;
  }
  void addError(uint32_t startByte, uint32_t endByte, kj::StringPtr message) const override {
    std::cerr << "input:" << startByte << "-" << endByte << ": " << message.cStr() << std::endl;
  }
};

class CompilerMain {
public:
  explicit CompilerMain(kj::ProcessContext& context)
      : context(context), loader(STDERR_FILENO) {}

  kj::MainFunc getMain() {
    return kj::MainBuilder(
          context, "Cap'n Proto compiler version 0.2",
          "Compiles Cap'n Proto schema files and generates corresponding source code in one or "
          "more languages.")
        .expectOneOrMoreArgs("source", KJ_BIND_METHOD(*this, addSource))
        .build();
  }

  kj::MainBuilder::Validity addSource(kj::StringPtr file) {
    KJ_IF_MAYBE(module, loader.loadModule(file, file)) {
      compiler.add(*module, Compiler::EAGER);
    } else {
      return "no such file";
    }

    return true;
  }

private:
  kj::ProcessContext& context;
  ModuleLoader loader;
  Compiler compiler;
};

int main(int argc, char* argv[]) {
  // Eventually this will be capnpc.  For now it's just a dummy program that tests parsing.

//  kj::Vector<char> input;
//  char buffer[4096];
//  for (;;) {
//    ssize_t n;
//    KJ_SYSCALL(n = read(STDIN_FILENO, buffer, sizeof(buffer)));
//    if (n == 0) {
//      break;
//    }
//    input.addAll(buffer, buffer + n);
//  }

  kj::StringPtr input =
      "@0x8e001c75f6ff54c8;\n"
      "struct Foo { bar @0 :Int32 = 123; baz @1 :Text; }\n"
      "struct Qux { foo @0 :List(Int32) = [12, 34]; }";

  DummyModule module;

  std::cout << "=========================================================================\n"
            << "lex\n"
            << "========================================================================="
            << std::endl;

  capnp::MallocMessageBuilder lexerArena;
  auto lexedFile = lexerArena.initRoot<capnp::compiler::LexedStatements>();
  capnp::compiler::lex(input, lexedFile, module);
  std::cout << capnp::prettyPrint(lexedFile).cStr() << std::endl;

  std::cout << "=========================================================================\n"
            << "parse\n"
            << "========================================================================="
            << std::endl;

  capnp::MallocMessageBuilder parserArena;
  auto parsedFile = parserArena.initRoot<capnp::compiler::ParsedFile>();
  capnp::compiler::parseFile(lexedFile.getStatements(), parsedFile, module);
  std::cout << capnp::prettyPrint(parsedFile).cStr() << std::endl;

  std::cout << "=========================================================================\n"
            << "compile\n"
            << "========================================================================="
            << std::endl;

  module.content = parsedFile.asReader();
  capnp::compiler::Compiler compiler;
  compiler.add(module, capnp::compiler::Compiler::EAGER);

  for (auto schema: compiler.getLoader().getAllLoaded()) {
    std::cout << capnp::prettyPrint(schema.getProto()).cStr() << std::endl;
  }

  return 0;
}

}  // namespace compiler
}  // namespace capnp

KJ_MAIN(capnp::compiler::CompilerMain);
