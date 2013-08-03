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
#include <capnp/serialize-packed.h>
#include <limits>

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

static const char VERSION_STRING[] = "Cap'n Proto version 0.2";

class CompilerMain final: public GlobalErrorReporter {
public:
  explicit CompilerMain(kj::ProcessContext& context)
      : context(context), loader(*this) {}

  kj::MainFunc getMain() {
    if (context.getProgramName().endsWith("capnpc")) {
      kj::MainBuilder builder(context, VERSION_STRING,
            "Compiles Cap'n Proto schema files and generates corresponding source code in one or "
            "more languages.");
      addGlobalOptions(builder);
      addCompileOptions(builder);
      builder.addOption({'i', "generate-id"}, KJ_BIND_METHOD(*this, generateId),
                        "Generate a new 64-bit unique ID for use in a Cap'n Proto schema.");
      return builder.build();
    } else {
      kj::MainBuilder builder(context, VERSION_STRING,
            "Command-line tool for Cap'n Proto development and debugging.");
      builder.addSubCommand("compile", KJ_BIND_METHOD(*this, getCompileMain),
                            "Generate source code from schema files.")
             .addSubCommand("id", KJ_BIND_METHOD(*this, getGenIdMain),
                            "Generate a new unique ID.")
             .addSubCommand("decode", KJ_BIND_METHOD(*this, getDecodeMain),
                            "Decode binary Cap'n Proto message to text.");
      // TODO(someday):  "encode" -- requires the ability to parse text format.
      addGlobalOptions(builder);
      return builder.build();
    }
  }

  kj::MainFunc getCompileMain() {
    kj::MainBuilder builder(context, VERSION_STRING,
          "Compiles Cap'n Proto schema files and generates corresponding source code in one or "
          "more languages.");
    addGlobalOptions(builder);
    addCompileOptions(builder);
    return builder.build();
  }

  kj::MainFunc getGenIdMain() {
    return kj::MainBuilder(
          context, "Cap'n Proto multi-tool 0.2",
          "Generates a new 64-bit unique ID for use in a Cap'n Proto schema.")
        .callAfterParsing(KJ_BIND_METHOD(*this, generateId))
        .build();
  }

  kj::MainFunc getDecodeMain() {
    // Only parse the schemas we actually need for decoding.
    compileMode = Compiler::LAZY;

    kj::MainBuilder builder(context, VERSION_STRING,
          "Decodes one or more encoded Cap'n Proto messages as text.  The messages have root "
          "type <type> defined in <schema-file>.  Messages are read from standard input and "
          "by default are expected to be in standard Cap'n Proto serialization format.");
    addGlobalOptions(builder);
    builder.addOption({'f', "flat"}, KJ_BIND_METHOD(*this, codeFlat),
                      "Interpret the input as one large single-segment message rather than a "
                      "stream in standard serialization format.")
           .addOption({'p', "packed"}, KJ_BIND_METHOD(*this, codePacked),
                      "Expect the input to be packed using standard Cap'n Proto packing, which "
                      "deflates zero-valued bytes.")
           .addOption({"short"}, KJ_BIND_METHOD(*this, printShort),
                      "Print in short (non-pretty) format.  Each message will be printed on one "
                      "line, without using whitespace to improve readability.")
           .expectArg("<schema-file>", KJ_BIND_METHOD(*this, addSource))
           .expectArg("<type>", KJ_BIND_METHOD(*this, setRootType))
           .callAfterParsing(KJ_BIND_METHOD(*this, decode));
    return builder.build();
  }

  void addGlobalOptions(kj::MainBuilder& builder) {
    builder.addOptionWithArg({'I', "import-path"}, KJ_BIND_METHOD(*this, addImportPath), "<dir>",
                             "Add <dir> to the list of directories searched for non-relative "
                             "imports (ones that start with a '/').")
           .addOption({"no-standard-import"}, KJ_BIND_METHOD(*this, noStandardImport),
                      "Do not add any default import paths; use only those specified by -I.  "
                      "Otherwise, typically /usr/include and /usr/local/include are added by "
                      "default.");
  }

  void addCompileOptions(kj::MainBuilder& builder) {
    builder.addOptionWithArg({'o', "output"}, KJ_BIND_METHOD(*this, addOutput), "<lang>[:<dir>]",
                             "Generate source code for language <lang> in directory <dir> "
                             "(default: current directory).  <lang> actually specifies a plugin "
                             "to use.  If <lang> is a simple word, the compiler for a plugin "
                             "called 'capnpc-<lang>' in $PATH.  If <lang> is a file path "
                             "containing slashes, it is interpreted as the exact plugin "
                             "executable file name, and $PATH is not searched.")
           .addOptionWithArg({"src-prefix"}, KJ_BIND_METHOD(*this, addSourcePrefix), "<prefix>",
                             "If a file specified for compilation starts with <prefix>, remove "
                             "the prefix for the purpose of deciding the names of output files.  "
                             "For example, the following command:\n"
                             "    capnp --src-prefix=foo/bar -oc++:corge foo/bar/baz/qux.capnp\n"
                             "would generate the files corge/baz/qux.capnp.{h,c++}.")
           .expectOneOrMoreArgs("<source>", KJ_BIND_METHOD(*this, addSource))
           .callAfterParsing(KJ_BIND_METHOD(*this, generateOutput));
  }

  // =====================================================================================
  // shared options

  kj::MainBuilder::Validity addImportPath(kj::StringPtr path) {
    loader.addImportPath(kj::heapString(path));
    return true;
  }

  kj::MainBuilder::Validity noStandardImport() {
    addStandardImportPaths = false;
    return true;
  }

  kj::MainBuilder::Validity addSource(kj::StringPtr file) {
    if (addStandardImportPaths) {
      loader.addImportPath(kj::heapString("/usr/local/include"));
      loader.addImportPath(kj::heapString("/usr/include"));
      addStandardImportPaths = false;
    }

    size_t longestPrefix = 0;

    for (auto& prefix: sourcePrefixes) {
      if (file.startsWith(prefix)) {
        longestPrefix = kj::max(longestPrefix, prefix.size());
      }
    }

    KJ_IF_MAYBE(module, loader.loadModule(file, file.slice(longestPrefix))) {
      sourceIds.add(compiler.add(*module, Compiler::EAGER));
    } else {
      return "no such file";
    }

    return true;
  }

  // =====================================================================================
  // "id" command

  kj::MainBuilder::Validity generateId() {
    context.exitInfo(kj::str("@0x", kj::hex(generateRandomId())));
  }

  // =====================================================================================
  // "compile" command

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

  kj::MainBuilder::Validity addSourcePrefix(kj::StringPtr prefix) {
    if (prefix.endsWith("/")) {
      sourcePrefixes.add(kj::heapString(prefix));
    } else {
      sourcePrefixes.add(kj::str(prefix, '/'));
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

  // =====================================================================================
  // "decode" command

  kj::MainBuilder::Validity codeFlat() {
    if (packed) return "cannot be used with --packed";
    flat = true;
    return true;
  }
  kj::MainBuilder::Validity codePacked() {
    if (flat) return "cannot be used with --flat";
    packed = true;
    return true;
  }
  kj::MainBuilder::Validity printShort() {
    pretty = false;
    return true;
  }

  kj::MainBuilder::Validity setRootType(kj::StringPtr type) {
    KJ_ASSERT(sourceIds.size() == 1);
    uint64_t id = sourceIds[0];

    while (type.size() > 0) {
      kj::String temp;
      kj::StringPtr part;
      KJ_IF_MAYBE(dotpos, type.findFirst('.')) {
        temp = kj::heapString(type.slice(0, *dotpos));
        part = temp;
        type = type.slice(*dotpos + 1);
      } else {
        part = type;
        type = nullptr;
      }

      KJ_IF_MAYBE(childId, compiler.lookup(id, part)) {
        id = *childId;
      } else {
        return "no such type";
      }
    }

    Schema schema = compiler.getLoader().get(id);
    if (schema.getProto().getBody().which() != schema::Node::Body::STRUCT_NODE) {
      return "not a struct type";
    }
    rootType = schema.asStruct();

    return true;
  }

  kj::MainBuilder::Validity decode() {
    kj::FdInputStream rawInput(STDIN_FILENO);
    kj::BufferedInputStreamWrapper input(rawInput);

    if (flat) {
      // Read in the whole input to decode as one segment.
      kj::Array<word> words;

      {
        kj::Vector<byte> allBytes;
        for (;;) {
          auto buffer = input.tryGetReadBuffer();
          if (buffer.size() == 0) break;
          allBytes.addAll(buffer);
          input.skip(buffer.size());
        }

        // Technically we don't know if the bytes are aligned so we'd better copy them to a new
        // array.  Note that if we have a non-whole number of words we chop off the straggler bytes.
        // This is fine because if those bytes are actually part of the message we will hit an error
        // later and if they are not then who cares?
        words = kj::heapArray<word>(allBytes.size() / sizeof(word));
        memcpy(words.begin(), allBytes.begin(), words.size() * sizeof(word));
      }

      kj::ArrayPtr<const word> segments = words;
      decodeInner<SegmentArrayMessageReader>(arrayPtr(&segments, 1));
    } else {
      while (input.tryGetReadBuffer().size() > 0) {
        if (packed) {
          decodeInner<PackedMessageReader>(input);
        } else {
          decodeInner<InputStreamMessageReader>(input);
        }
      }
    }

    return true;
  }

private:
  struct ParseErrorCatcher: public kj::ExceptionCallback {
    void onRecoverableException(kj::Exception&& e) {
      // Only capture the first exception, on the assumption that later exceptions are probably
      // just cascading problems.
      if (exception == nullptr) {
        exception = kj::mv(e);
      }
    }

    kj::Maybe<kj::Exception> exception;
  };

  template <typename MessageReaderType, typename Input>
  void decodeInner(Input&& input) {
    // Since this is a debug tool, lift the usual security limits.  Worse case is the process
    // crashes or has to be killed.
    ReaderOptions options;
    options.nestingLimit = std::numeric_limits<decltype(options.nestingLimit)>::max() >> 1;
    options.traversalLimitInWords =
        std::numeric_limits<decltype(options.traversalLimitInWords)>::max();

    MessageReaderType reader(input, options);
    auto root = reader.template getRoot<DynamicStruct>(rootType);
    kj::String text;
    kj::Maybe<kj::Exception> exception;

    {
      ParseErrorCatcher catcher;
      if (pretty) {
        text = prettyPrint(root);
      } else {
        text = kj::str(root);
      }
      exception = kj::mv(catcher.exception);
    }

    kj::ArrayPtr<const byte> pieces[2];
    pieces[0] = kj::arrayPtr(reinterpret_cast<const byte*>(text.begin()), text.size());
    pieces[1] = kj::arrayPtr(reinterpret_cast<const byte*>("\n"), 1);

    kj::FdOutputStream(STDOUT_FILENO).write(kj::arrayPtr(pieces, KJ_ARRAY_SIZE(pieces)));

    KJ_IF_MAYBE(e, exception) {
      context.error(kj::str("*** error in previous message ***\n", *e, "\n*** end error ***"));
    }
  }

public:
  // =====================================================================================

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
  Compiler::Mode compileMode = Compiler::EAGER;

  kj::Vector<kj::String> sourcePrefixes;
  bool addStandardImportPaths = true;

  bool flat = false;
  bool packed = false;
  bool pretty = true;
  StructSchema rootType;
  // For the "decode" command.

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
