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
#include <capnp/schema.capnp.h>
#include <kj/vector.h>
#include <kj/io.h>
#include <unistd.h>
#include <kj/debug.h>
#include "../message.h"
#include <iostream>
#include <kj/main.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <capnp/serialize.h>

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

class CompilerMain final: public GlobalErrorReporter {
public:
  explicit CompilerMain(kj::ProcessContext& context)
      : context(context), loader(*this) {}

  kj::MainFunc getMain() {
    return kj::MainBuilder(
          context, "Cap'n Proto compiler version 0.2",
          "Compiles Cap'n Proto schema files and generates corresponding source code in one or "
          "more languages.")
        .addOptionWithArg({'o', "output"}, KJ_BIND_METHOD(*this, addOutput), "<lang>[:<dir>]",
                          "Generate source code for language <lang> in directory <dir> (default: "
                          "current directory).  <lang> actually specifies a plugin to use.  If "
                          "<lang> is a simple word, the compiler for a plugin called "
                          "'capnpc-<lang>' in $PATH.  If <lang> is a file path containing slashes, "
                          "it is interpreted as the exact plugin executable file name, and $PATH "
                          "is not searched.")
        .expectOneOrMoreArgs("source", KJ_BIND_METHOD(*this, addSource))
        .callAfterParsing(KJ_BIND_METHOD(*this, generateOutput))
        .build();
  }

  kj::MainBuilder::Validity addOutput(kj::StringPtr spec) {
    KJ_IF_MAYBE(split, spec.findFirst(':')) {
      kj::StringPtr dir = spec.slice(*split + 1);
      struct stat stats;
      if (stat(dir.cStr(), &stats) < 0 || !S_ISDIR(stats.st_mode)) {
        return "output location is inaccessible or is not a directory";
      }
      outputs.add(OutputDirective { spec.slice(0, *split), dir });
    } else {
      outputs.add(OutputDirective { spec.asArray(), nullptr });
    }

    return true;
  }

  kj::MainBuilder::Validity addSource(kj::StringPtr file) {
    KJ_IF_MAYBE(module, loader.loadModule(file, file)) {
      sourceIds.add(compiler.add(*module, Compiler::EAGER));
    } else {
      return "no such file";
    }

    return true;
  }

  kj::MainBuilder::Validity generateOutput() {
    if (hadErrors()) {
      // Skip output if we had any errors.
      return true;
    }

    if (outputs.size() == 0) {
      return "no outputs specified";
    }

    MallocMessageBuilder message;
    auto request = message.initRoot<schema::CodeGeneratorRequest>();

    auto schemas = compiler.getLoader().getAllLoaded();
    auto nodes = request.initNodes(schemas.size());
    for (size_t i = 0; i < schemas.size(); i++) {
      nodes.setWithCaveats(i, schemas[i].getProto());
    }

    auto requestedFiles = request.initRequestedFiles(sourceIds.size());
    for (size_t i = 0; i < sourceIds.size(); i++) {
      requestedFiles.set(i, sourceIds[i]);
    }

    for (auto& output: outputs) {
      int pipeFds[2];
      KJ_SYSCALL(pipe(pipeFds));

      kj::String exeName;
      bool shouldSearchPath = true;
      for (char c: output.name) {
        if (c == '/') {
          shouldSearchPath = false;
          break;
        }
      }
      if (shouldSearchPath) {
        exeName = kj::str("capnpc-", output.name);
      } else {
        exeName = kj::heapString(output.name);
      }

      pid_t child;
      KJ_SYSCALL(child = fork());
      if (child == 0) {
        // I am the child!
        KJ_SYSCALL(close(pipeFds[1]));
        KJ_SYSCALL(dup2(pipeFds[0], STDIN_FILENO));
        KJ_SYSCALL(close(pipeFds[0]));

        if (output.dir != nullptr) {
          KJ_SYSCALL(chdir(output.dir.cStr()), output.dir);
        }

        if (shouldSearchPath) {
          KJ_SYSCALL(execlp(exeName.cStr(), exeName.cStr(), nullptr));
        } else {
          KJ_SYSCALL(execl(exeName.cStr(), exeName.cStr(), nullptr));
        }

        KJ_FAIL_ASSERT("execlp() returned?");
      }

      KJ_SYSCALL(close(pipeFds[0]));

      writeMessageToFd(pipeFds[1], message);
      KJ_SYSCALL(close(pipeFds[1]));

      int status;
      KJ_SYSCALL(waitpid(child, &status, 0));
      if (WIFSIGNALED(status)) {
        context.error(kj::str(exeName, ": plugin failed: ", strsignal(WTERMSIG(status))));
      } else if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
        context.error(kj::str(exeName, ": plugin failed: exit code ", WEXITSTATUS(status)));
      }
    }

    return true;
  }

  void addError(kj::StringPtr file, SourcePos start, SourcePos end,
                kj::StringPtr message) const override {
    kj::String wholeMessage;
    if (end.line == start.line) {
      if (end.column == start.column) {
        wholeMessage = kj::str(file, ":", start.line + 1, ":", start.column + 1,
                               ": error: ", message, "\n");
      } else {
        wholeMessage = kj::str(file, ":", start.line + 1, ":", start.column + 1,
                               "-", end.column + 1, ": error: ", message, "\n");
      }
    } else {
      // The error spans multiple lines, so just report it on the first such line.
      wholeMessage = kj::str(file, ":", start.line + 1, ": error: ", message, "\n");
    }

    context.error(wholeMessage);
    __atomic_store_n(&hadErrors_, true, __ATOMIC_RELAXED);
  }

  bool hadErrors() const override {
    return __atomic_load_n(&hadErrors_, __ATOMIC_RELAXED);
  }

private:
  kj::ProcessContext& context;
  ModuleLoader loader;
  Compiler compiler;

  kj::Vector<uint64_t> sourceIds;

  struct OutputDirective {
    kj::ArrayPtr<const char> name;
    kj::StringPtr dir;
  };
  kj::Vector<OutputDirective> outputs;

  mutable bool hadErrors_ = false;
};

}  // namespace compiler
}  // namespace capnp

KJ_MAIN(capnp::compiler::CompilerMain);
