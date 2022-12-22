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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#if _WIN32
#include <kj/win32-api-version.h>
#endif

#include "lexer.h"
#include "parser.h"
#include "compiler.h"
#include "module-loader.h"
#include "node-translator.h"
#include <capnp/pretty-print.h>
#include <capnp/schema.capnp.h>
#include <kj/vector.h>
#include <kj/io.h>
#include <kj/miniposix.h>
#include <kj/debug.h>
#include "../message.h"
#include <iostream>
#include <kj/main.h>
#include <kj/parse/char.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <capnp/serialize.h>
#include <capnp/serialize-packed.h>
#include <capnp/serialize-text.h>
#include <capnp/compat/json.h>
#include <errno.h>
#include <stdlib.h>
#include <kj/map.h>

#if _WIN32
#include <process.h>
#include <windows.h>
#include <kj/windows-sanity.h>
#undef CONST
#else
#include <sys/wait.h>
#endif

#if HAVE_CONFIG_H
#include "config.h"
#endif

#ifndef VERSION
#define VERSION "(unknown)"
#endif

namespace capnp {
namespace compiler {

static const char VERSION_STRING[] = "Cap'n Proto version " VERSION;

class CompilerMain final: public GlobalErrorReporter {
public:
  explicit CompilerMain(kj::ProcessContext& context)
      : context(context), disk(kj::newDiskFilesystem()), loader(*this) {}

  kj::MainFunc getMain() {
    if (context.getProgramName().endsWith("capnpc") || context.getProgramName().endsWith("capnpc.exe")) {
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
             .addSubCommand("convert", KJ_BIND_METHOD(*this, getConvertMain),
                            "Convert messages between binary, text, JSON, etc.")
             .addSubCommand("decode", KJ_BIND_METHOD(*this, getDecodeMain),
                            "DEPRECATED (use `convert`)")
             .addSubCommand("encode", KJ_BIND_METHOD(*this, getEncodeMain),
                            "DEPRECATED (use `convert`)")
             .addSubCommand("eval", KJ_BIND_METHOD(*this, getEvalMain),
                            "Evaluate a const from a schema file.");
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
    return kj::MainBuilder(context, VERSION_STRING,
          "Generates a new 64-bit unique ID for use in a Cap'n Proto schema.")
        .callAfterParsing(KJ_BIND_METHOD(*this, generateId))
        .build();
  }

  kj::MainFunc getConvertMain() {
    // Only parse the schemas we actually need for decoding.
    compileEagerness = Compiler::NODE;

    // Drop annotations since we don't need them.  This avoids importing files like c++.capnp.
    annotationFlag = Compiler::DROP_ANNOTATIONS;

    kj::MainBuilder builder(context, VERSION_STRING,
          "Converts messages between formats. Reads a stream of messages from stdin in format "
          "<from> and writes them to stdout in format <to>. Valid formats are:\n"
          "    binary      standard binary format\n"
          "    packed      packed binary format (deflates zeroes)\n"
          "    flat        binary single segment, no segment table (rare)\n"
          "    flat-packed flat and packed\n"
          "    canonical   canonicalized binary single segment, no segment table\n"
          "    text        schema language struct literal format\n"
          "    json        JSON format\n"
          "When using \"text\" or \"json\" format, you must specify <schema-file> and <type> "
          "(but they are ignored and can be omitted for binary-to-binary conversions). "
          "<type> names names a struct type defined in <schema-file>, which is the root type "
          "of the message(s).");
    addGlobalOptions(builder);
    builder.addOption({"short"}, KJ_BIND_METHOD(*this, printShort),
               "Write text or JSON output in short (non-pretty) format. Each message will "
               "be printed on one line, without using whitespace to improve readability.")
           .addOptionWithArg({"segment-size"}, KJ_BIND_METHOD(*this, setSegmentSize), "<n>",
               "For binary output, sets the preferred segment size on the MallocMessageBuilder to <n> "
               "words and turns off heuristic growth.  This flag is mainly useful "
               "for testing.  Without it, each message will be written as a single "
               "segment.")
           .addOption({"quiet"}, KJ_BIND_METHOD(*this, setQuiet),
               "Do not print warning messages about the input being in the wrong format.  "
               "Use this if you find the warnings are wrong (but also let us know so "
               "we can improve them).")
           .expectArg("<from>:<to>", KJ_BIND_METHOD(*this, setConversion))
           .expectOptionalArg("<schema-file>", KJ_BIND_METHOD(*this, addSource))
           .expectOptionalArg("<type>", KJ_BIND_METHOD(*this, setRootType))
           .callAfterParsing(KJ_BIND_METHOD(*this, convert));
    return builder.build();
  }

  kj::MainFunc getDecodeMain() {
    // Only parse the schemas we actually need for decoding.
    compileEagerness = Compiler::NODE;

    // Drop annotations since we don't need them.  This avoids importing files like c++.capnp.
    annotationFlag = Compiler::DROP_ANNOTATIONS;

    kj::MainBuilder builder(context, VERSION_STRING,
          "Decodes one or more encoded Cap'n Proto messages as text.  The messages have root "
          "type <type> defined in <schema-file>.  Messages are read from standard input and "
          "by default are expected to be in standard Cap'n Proto serialization format.");
    addGlobalOptions(builder);
    builder.addOption({"flat"}, KJ_BIND_METHOD(*this, codeFlat),
                      "Interpret the input as one large single-segment message rather than a "
                      "stream in standard serialization format.  (Rarely used.)")
           .addOption({'p', "packed"}, KJ_BIND_METHOD(*this, codePacked),
                      "Expect the input to be packed using standard Cap'n Proto packing, which "
                      "deflates zero-valued bytes.  (This reads messages written with "
                      "capnp::writePackedMessage*() from <capnp/serialize-packed.h>.  Do not use "
                      "this for messages written with capnp::writeMessage*() from "
                      "<capnp/serialize.h>.)")
           .addOption({"short"}, KJ_BIND_METHOD(*this, printShort),
                      "Print in short (non-pretty) format.  Each message will be printed on one "
                      "line, without using whitespace to improve readability.")
           .addOption({"quiet"}, KJ_BIND_METHOD(*this, setQuiet),
                      "Do not print warning messages about the input being in the wrong format.  "
                      "Use this if you find the warnings are wrong (but also let us know so "
                      "we can improve them).")
           .expectArg("<schema-file>", KJ_BIND_METHOD(*this, addSource))
           .expectArg("<type>", KJ_BIND_METHOD(*this, setRootType))
           .callAfterParsing(KJ_BIND_METHOD(*this, decode));
    return builder.build();
  }

  kj::MainFunc getEncodeMain() {
    // Only parse the schemas we actually need for decoding.
    compileEagerness = Compiler::NODE;

    // Drop annotations since we don't need them.  This avoids importing files like c++.capnp.
    annotationFlag = Compiler::DROP_ANNOTATIONS;

    kj::MainBuilder builder(context, VERSION_STRING,
          "Encodes one or more textual Cap'n Proto messages to binary.  The messages have root "
          "type <type> defined in <schema-file>.  Messages are read from standard input.  Each "
          "message is a parenthesized struct literal, like the format used to specify constants "
          "and default values of struct type in the schema language.  For example:\n"
          "    (foo = 123, bar = \"hello\", baz = [true, false, true])\n"
          "The input may contain any number of such values; each will be encoded as a separate "
          "message.",

          "Note that the current implementation reads the entire input into memory before "
          "beginning to encode.  A better implementation would read and encode one message at "
          "a time.");
    addGlobalOptions(builder);
    builder.addOption({"flat"}, KJ_BIND_METHOD(*this, codeFlat),
                      "Expect only one input value, serializing it as a single-segment message "
                      "with no framing.")
           .addOption({'p', "packed"}, KJ_BIND_METHOD(*this, codePacked),
                      "Pack the output message with standard Cap'n Proto packing, which "
                      "deflates zero-valued bytes.  (This writes messages using "
                      "capnp::writePackedMessage() from <capnp/serialize-packed.h>.  Without "
                      "this, capnp::writeMessage() from <capnp/serialize.h> is used.)")
           .addOptionWithArg({"segment-size"}, KJ_BIND_METHOD(*this, setSegmentSize), "<n>",
                             "Sets the preferred segment size on the MallocMessageBuilder to <n> "
                             "words and turns off heuristic growth.  This flag is mainly useful "
                             "for testing.  Without it, each message will be written as a single "
                             "segment.")
           .expectArg("<schema-file>", KJ_BIND_METHOD(*this, addSource))
           .expectArg("<type>", KJ_BIND_METHOD(*this, setRootType))
           .callAfterParsing(KJ_BIND_METHOD(*this, encode));
    return builder.build();
  }

  kj::MainFunc getEvalMain() {
    // Only parse the schemas we actually need for decoding.
    compileEagerness = Compiler::NODE;

    // Drop annotations since we don't need them.  This avoids importing files like c++.capnp.
    annotationFlag = Compiler::DROP_ANNOTATIONS;

    // Default convert to text unless -o is given.
    convertTo = Format::TEXT;

    // When using `capnp eval`, type IDs don't really matter, because `eval` won't actually use
    // them for anything. When using Cap'n Proto an a config format -- the common use case for
    // `capnp eval` -- the exercise of adding a file ID to every file is pointless busy work. So,
    // we don't require it.
    loader.setFileIdsRequired(false);

    kj::MainBuilder builder(context, VERSION_STRING,
          "Prints (or encodes) the value of <name>, which must be defined in <schema-file>.  "
          "<name> must refer to a const declaration, a field of a struct type (prints the default "
          "value), or a field or list element nested within some other value.  Examples:\n"
          "    capnp eval myschema.capnp MyType.someField\n"
          "    capnp eval myschema.capnp someConstant\n"
          "    capnp eval myschema.capnp someConstant.someField\n"
          "    capnp eval myschema.capnp someConstant.someList[4]\n"
          "    capnp eval myschema.capnp someConstant.someList[4].anotherField[1][2][3]\n"
          "Since consts can have complex struct types, and since you can define a const using "
          "import and variable substitution, this can be a convenient way to write text-format "
          "config files which are compiled to binary before deployment.",

          "By default the value is written in text format and can have any type.  The -b, -p, "
          "and --flat flags specify binary output, in which case the const must be of struct "
          "type.");
    addGlobalOptions(builder);
    builder.addOptionWithArg({'o', "output"}, KJ_BIND_METHOD(*this, setEvalOutputFormat),
                      "<format>", "Encode the output in the given format. See `capnp help convert` "
                      "for a list of formats. Defaults to \"text\".")
           .addOption({'b', "binary"}, KJ_BIND_METHOD(*this, codeBinary),
                      "same as -obinary")
           .addOption({"flat"}, KJ_BIND_METHOD(*this, codeFlat),
                      "same as -oflat")
           .addOption({'p', "packed"}, KJ_BIND_METHOD(*this, codePacked),
                      "same as -opacked")
           .addOption({"short"}, KJ_BIND_METHOD(*this, printShort),
                      "If output format is text or JSON, write in short (non-pretty) format. The "
                      "message will be printed on one line, without using whitespace to improve "
                      "readability.")
           .expectArg("<schema-file>", KJ_BIND_METHOD(*this, addSource))
           .expectArg("<name>", KJ_BIND_METHOD(*this, evalConst));
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
                             "to use.  If <lang> is a simple word, the compiler searches for a plugin "
                             "called 'capnpc-<lang>' in $PATH.  If <lang> is a file path "
                             "containing slashes, it is interpreted as the exact plugin "
                             "executable file name, and $PATH is not searched.  If <lang> is '-', "
                             "the compiler dumps the request to standard output.")
           .addOptionWithArg({"src-prefix"}, KJ_BIND_METHOD(*this, addSourcePrefix), "<prefix>",
                             "If a file specified for compilation starts with <prefix>, remove "
                             "the prefix for the purpose of deciding the names of output files.  "
                             "For example, the following command:\n"
                             "    capnp compile --src-prefix=foo/bar -oc++:corge foo/bar/baz/qux.capnp\n"
                             "would generate the files corge/baz/qux.capnp.{h,c++}.")
           .expectOneOrMoreArgs("<source>", KJ_BIND_METHOD(*this, addSource))
           .callAfterParsing(KJ_BIND_METHOD(*this, generateOutput));
  }

  // =====================================================================================
  // shared options

  kj::MainBuilder::Validity addImportPath(kj::StringPtr path) {
    KJ_IF_MAYBE(dir, getSourceDirectory(path, false)) {
      loader.addImportPath(*dir);
      return true;
    } else {
      return "no such directory";
    }
  }

  kj::MainBuilder::Validity noStandardImport() {
    addStandardImportPaths = false;
    return true;
  }

  kj::MainBuilder::Validity addSource(kj::StringPtr file) {
    if (!compilerConstructed) {
      compiler = compilerSpace.construct(annotationFlag);
      compilerConstructed = true;
    }

    if (addStandardImportPaths) {
      static constexpr kj::StringPtr STANDARD_IMPORT_PATHS[] = {
        "/usr/local/include"_kj,
        "/usr/include"_kj,
#ifdef CAPNP_INCLUDE_DIR
        KJ_CONCAT(CAPNP_INCLUDE_DIR, _kj),
#endif
      };
      for (auto path: STANDARD_IMPORT_PATHS) {
        KJ_IF_MAYBE(dir, getSourceDirectory(path, false)) {
          loader.addImportPath(*dir);
        } else {
          // ignore standard path that doesn't exist
        }
      }

      addStandardImportPaths = false;
    }

    auto dirPathPair = interpretSourceFile(file);
    KJ_IF_MAYBE(module, loader.loadModule(dirPathPair.dir, dirPathPair.path)) {
      auto compiled = compiler->add(*module);
      compiler->eagerlyCompile(compiled.getId(), compileEagerness);
      sourceFiles.add(SourceFile { compiled.getId(), compiled, module->getSourceName(), &*module });
    } else {
      return "no such file";
    }

    return true;
  }

public:
  // =====================================================================================
  // "id" command

  kj::MainBuilder::Validity generateId() {
    context.exitInfo(kj::str("@0x", kj::hex(generateRandomId())));
    KJ_CLANG_KNOWS_THIS_IS_UNREACHABLE_BUT_GCC_DOESNT;
  }

  // =====================================================================================
  // "compile" command

  kj::MainBuilder::Validity addOutput(kj::StringPtr spec) {
    KJ_IF_MAYBE(split, spec.findFirst(':')) {
      kj::StringPtr dir = spec.slice(*split + 1);
      auto plugin = spec.slice(0, *split);

      if (*split == 1 && (dir.startsWith("/") || dir.startsWith("\\"))) {
        // The colon is the second character and is immediately followed by a slash or backslash.
        // So, the user passed something like `-o c:/foo`. Is this a request to run the C plugin
        // and output to `/foo`? Or are we on Windows, and is this a request to run the plugin
        // `c:/foo`?
        KJ_IF_MAYBE(split2, dir.findFirst(':')) {
          // There are two colons. The first ':' was the second char, and was followed by '/' or
          // '\', e.g.:
          //     capnp compile -o c:/foo.exe:bar
          //
          // In this case we can conclude that the second colon is actually meant to be the
          // plugin/location separator, and the first colon was simply signifying a drive letter.
          //
          // Proof by contradiction:
          // - Say that none of the colons were meant to be plugin/location separators; i.e. the
          //   whole argument is meant to be a plugin indicator and the location defaults to ".".
          //   -> In this case, the plugin path has two colons, which is not valid.
          //   -> CONTRADICTION
          // - Say that the first colon was meant to be the plugin/location separator.
          //   -> In this case, the second colon must be the drive letter separator for the
          //      output location.
          //   -> However, the output location begins with '/' or '\', which is not a drive letter.
          //   -> CONTRADICTION
          // - Say that there are more colons beyond the first two, and one of these is meant to
          //   be the plugin/location separator.
          //   -> In this case, the plugin path has two or more colons, which is not valid.
          //   -> CONTRADICTION
          //
          // We therefore conclude that the *second* colon is in fact the plugin/location separator.

          dir = dir.slice(*split2 + 1);
          plugin = spec.slice(0, *split2 + 2);
#if _WIN32
        } else {
          // The user wrote something like:
          //
          //     capnp compile -o c:/foo/bar
          //
          // What does this mean? It depends on what system we're on. On a Unix system, the above
          // clearly is a request to run the `capnpc-c` plugin (perhaps to output C code) and write
          // to the directory /foo/bar. But on Windows, absolute paths do not start with '/', and
          // the above is actually a request to run the plugin `c:/foo/bar`, outputting to the
          // current directory.

          outputs.add(OutputDirective { spec.asArray(), nullptr });
          return true;
#endif
        }
      }

      struct stat stats;
      if (stat(dir.cStr(), &stats) < 0 || !S_ISDIR(stats.st_mode)) {
        return "output location is inaccessible or is not a directory";
      }
      outputs.add(OutputDirective { plugin, disk->getCurrentPath().evalNative(dir) });
    } else {
      outputs.add(OutputDirective { spec.asArray(), nullptr });
    }

    return true;
  }

  kj::MainBuilder::Validity addSourcePrefix(kj::StringPtr prefix) {
    if (getSourceDirectory(prefix, true) == nullptr) {
      return "no such directory";
    } else {
      return true;
    }
  }

  kj::MainBuilder::Validity generateOutput() {
    if (hadErrors()) {
      // Skip output if we had any errors.
      return true;
    }

    // We require one or more sources and if they failed to compile we quit above, so this should
    // pass.  (This assertion also guarantees that `compiler` has been initialized.)
    KJ_ASSERT(sourceFiles.size() > 0, "Shouldn't have gotten here without sources.");

    if (outputs.size() == 0) {
      return "no outputs specified";
    }

    MallocMessageBuilder message;
    auto request = message.initRoot<schema::CodeGeneratorRequest>();

    auto version = request.getCapnpVersion();
    version.setMajor(CAPNP_VERSION_MAJOR);
    version.setMinor(CAPNP_VERSION_MINOR);
    version.setMicro(CAPNP_VERSION_MICRO);

    auto schemas = compiler->getLoader().getAllLoaded();
    auto nodes = request.initNodes(schemas.size());
    for (size_t i = 0; i < schemas.size(); i++) {
      nodes.setWithCaveats(i, schemas[i].getProto());
    }

    request.adoptSourceInfo(compiler->getAllSourceInfo(message.getOrphanage()));

    auto requestedFiles = request.initRequestedFiles(sourceFiles.size());
    for (size_t i = 0; i < sourceFiles.size(); i++) {
      auto requestedFile = requestedFiles[i];
      requestedFile.setId(sourceFiles[i].id);
      requestedFile.setFilename(sourceFiles[i].name);
      requestedFile.adoptImports(compiler->getFileImportTable(
          *sourceFiles[i].module, Orphanage::getForMessageContaining(requestedFile)));
    }

    for (auto& output: outputs) {
      if (kj::str(output.name) == "-") {
        writeMessageToFd(STDOUT_FILENO, message);
        continue;
      }

      int pipeFds[2];
      KJ_SYSCALL(kj::miniposix::pipe(pipeFds));

      kj::String exeName;
      bool shouldSearchPath = true;
      for (char c: output.name) {
#if _WIN32
        if (c == '/' || c == '\\') {
#else
        if (c == '/') {
#endif
          shouldSearchPath = false;
          break;
        }
      }
      if (shouldSearchPath) {
        exeName = kj::str("capnpc-", output.name);
      } else {
        exeName = kj::heapString(output.name);
      }

      kj::Array<char> pwd = kj::heapArray<char>(256);
      while (getcwd(pwd.begin(), pwd.size()) == nullptr) {
        KJ_REQUIRE(pwd.size() < 8192, "WTF your working directory path is more than 8k?");
        pwd = kj::heapArray<char>(pwd.size() * 2);
      }

#if _WIN32
      int oldStdin;
      KJ_SYSCALL(oldStdin = dup(STDIN_FILENO));
      intptr_t child;

#else  // _WIN32
      pid_t child;
      KJ_SYSCALL(child = fork());
      if (child == 0) {
        // I am the child!

        KJ_SYSCALL(close(pipeFds[1]));
#endif  // _WIN32, else

        KJ_SYSCALL(dup2(pipeFds[0], STDIN_FILENO));
        KJ_SYSCALL(close(pipeFds[0]));

        KJ_IF_MAYBE(d, output.dir) {
#if _WIN32
          KJ_SYSCALL(SetCurrentDirectoryW(d->forWin32Api(true).begin()), d->toWin32String(true));
#else
          auto wd = d->toString(true);
          KJ_SYSCALL(chdir(wd.cStr()), wd);
          KJ_SYSCALL(setenv("PWD", wd.cStr(), true));
#endif
        }

#if _WIN32
        // MSVCRT's spawn*() don't correctly escape arguments, which is necessary on Windows
        // since the underlying system call takes a single command line string rather than
        // an arg list. We do the escaping ourselves by wrapping the name in quotes. We know
        // that exeName itself can't contain quotes (since filenames aren't allowed to contain
        // quotes on Windows), so we don't have to account for those.
        KJ_ASSERT(exeName.findFirst('\"') == nullptr,
            "Windows filenames can't contain quotes", exeName);
        auto escapedExeName = kj::str("\"", exeName, "\"");
#endif

        if (shouldSearchPath) {
#if _WIN32
          child = _spawnlp(_P_NOWAIT, exeName.cStr(), escapedExeName.cStr(), nullptr);
#else
          execlp(exeName.cStr(), exeName.cStr(), nullptr);
#endif
        } else {
#if _WIN32
          if (!exeName.startsWith("/") && !exeName.startsWith("\\") &&
              !(exeName.size() >= 2 && exeName[1] == ':')) {
#else
          if (!exeName.startsWith("/")) {
#endif
            // The name is relative.  Prefix it with our original working directory path.
            exeName = kj::str(pwd.begin(), "/", exeName);
          }

#if _WIN32
          child = _spawnl(_P_NOWAIT, exeName.cStr(), escapedExeName.cStr(), nullptr);
#else
          execl(exeName.cStr(), exeName.cStr(), nullptr);
#endif
        }

#if _WIN32
        if (child == -1) {
#endif
          int error = errno;
          if (error == ENOENT) {
            context.exitError(kj::str(output.name, ": no such plugin (executable should be '",
                                      exeName, "')"));
          } else {
#if _WIN32
            KJ_FAIL_SYSCALL("spawn()", error);
#else
            KJ_FAIL_SYSCALL("exec()", error);
#endif
          }
#if _WIN32
        }

        // Restore stdin.
        KJ_SYSCALL(dup2(oldStdin, STDIN_FILENO));
        KJ_SYSCALL(close(oldStdin));

        // Restore current directory.
        KJ_SYSCALL(chdir(pwd.begin()), pwd.begin());
#else  // _WIN32
      }

      KJ_SYSCALL(close(pipeFds[0]));
#endif  // _WIN32, else

      writeMessageToFd(pipeFds[1], message);
      KJ_SYSCALL(close(pipeFds[1]));

#if _WIN32
      int status;
      if (_cwait(&status, child, 0) == -1) {
        KJ_FAIL_SYSCALL("_cwait()", errno);
      }

      if (status != 0) {
        context.error(kj::str(output.name, ": plugin failed: exit code ", status));
      }

#else  // _WIN32
      int status;
      KJ_SYSCALL(waitpid(child, &status, 0));
      if (WIFSIGNALED(status)) {
        context.error(kj::str(output.name, ": plugin failed: ", strsignal(WTERMSIG(status))));
      } else if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
        context.error(kj::str(output.name, ": plugin failed: exit code ", WEXITSTATUS(status)));
      }
#endif  // _WIN32, else
    }

    return true;
  }

  // =====================================================================================
  // "convert" command

private:
  enum class Format {
    BINARY,
    PACKED,
    FLAT,
    FLAT_PACKED,
    CANONICAL,
    TEXT,
    JSON
  };

  kj::Maybe<Format> parseFormatName(kj::StringPtr name) {
    if (name == "binary"     ) return Format::BINARY;
    if (name == "packed"     ) return Format::PACKED;
    if (name == "flat"       ) return Format::FLAT;
    if (name == "flat-packed") return Format::FLAT_PACKED;
    if (name == "canonical"  ) return Format::CANONICAL;
    if (name == "text"       ) return Format::TEXT;
    if (name == "json"       ) return Format::JSON;

    return nullptr;
  }

  kj::StringPtr toString(Format format) {
    switch (format) {
      case Format::BINARY     : return "binary";
      case Format::PACKED     : return "packed";
      case Format::FLAT       : return "flat";
      case Format::FLAT_PACKED: return "flat-packed";
      case Format::CANONICAL  : return "canonical";
      case Format::TEXT       : return "text";
      case Format::JSON       : return "json";
    }
    KJ_UNREACHABLE;
  }

  Format formatFromDeprecatedFlags(Format defaultFormat) {
    // For deprecated commands "decode" and "encode".
    if (flat) {
      if (packed) {
        return Format::FLAT_PACKED;
      } else {
        return Format::FLAT;
      }
    } if (packed) {
      return Format::PACKED;
    } else if (binary) {
      return Format::BINARY;
    } else {
      return defaultFormat;
    }
  }

  kj::MainBuilder::Validity verifyRequirements(Format format) {
    if ((format == Format::TEXT || format == Format::JSON) && rootType == StructSchema()) {
      return kj::str("format requires schema: ", toString(format));
    } else {
      return true;
    }
  }

public:
  kj::MainBuilder::Validity setConversion(kj::StringPtr conversion) {
    KJ_IF_MAYBE(colon, conversion.findFirst(':')) {
      auto from = kj::str(conversion.slice(0, *colon));
      auto to = conversion.slice(*colon + 1);

      KJ_IF_MAYBE(f, parseFormatName(from)) {
        convertFrom = *f;
      } else {
        return kj::str("unknown format: ", from);
      }

      KJ_IF_MAYBE(t, parseFormatName(to)) {
        convertTo = *t;
      } else {
        return kj::str("unknown format: ", to);
      }

      if (convertFrom == Format::JSON || convertTo == Format::JSON) {
        // We need annotations to process JSON.
        // TODO(someday): Find a way that we can process annotations from json.capnp without
        //   requiring other annotation-only imports like c++.capnp
        annotationFlag = Compiler::COMPILE_ANNOTATIONS;
      }

      return true;
    } else {
      return "invalid conversion, format is: <from>:<to>";
    }
  }

  kj::MainBuilder::Validity convert() {
    {
      auto result = verifyRequirements(convertFrom);
      if (result.getError() != nullptr) return result;
    }
    {
      auto result = verifyRequirements(convertTo);
      if (result.getError() != nullptr) return result;
    }

    kj::FdInputStream rawInput(STDIN_FILENO);
    kj::BufferedInputStreamWrapper input(rawInput);

    kj::FdOutputStream output(STDOUT_FILENO);

    if (!quiet) {
      auto result = checkPlausibility(convertFrom, input.getReadBuffer());
      if (result.getError() != nullptr) {
        return kj::mv(result);
      }
    }

    while (input.tryGetReadBuffer().size() > 0) {
      readOneAndConvert(input, output);
    }

    context.exit();
    KJ_CLANG_KNOWS_THIS_IS_UNREACHABLE_BUT_GCC_DOESNT;
  }

private:
  kj::Vector<byte> readAll(kj::BufferedInputStreamWrapper& input) {
    kj::Vector<byte> allBytes;
    for (;;) {
      auto buffer = input.tryGetReadBuffer();
      if (buffer.size() == 0) break;
      allBytes.addAll(buffer);
      input.skip(buffer.size());
    }
    return allBytes;
  }

  kj::String readOneText(kj::BufferedInputStreamWrapper& input) {
    // Consume and return one parentheses-delimited message from the input.
    //
    // Accounts for nested parentheses, comments, and string literals.

    enum {
      NORMAL,
      COMMENT,
      QUOTE,
      QUOTE_ESCAPE,
      DQUOTE,
      DQUOTE_ESCAPE
    } state = NORMAL;
    uint depth = 0;
    bool sawClose = false;

    kj::Vector<char> chars;

    for (;;) {
      auto buffer = input.tryGetReadBuffer();

      if (buffer == nullptr) {
        // EOF
        chars.add('\0');
        return kj::String(chars.releaseAsArray());
      }

      for (auto i: kj::indices(buffer)) {
        char c = buffer[i];
        switch (state) {
          case NORMAL:
            switch (c) {
              case '#': state = COMMENT; break;
              case '(':
                if (depth == 0 && sawClose) {
                  // We already got one complete message. This is the start of the next message.
                  // Stop here.
                  chars.addAll(buffer.slice(0, i));
                  chars.add('\0');
                  input.skip(i);
                  return kj::String(chars.releaseAsArray());
                }
                ++depth;
                break;
              case ')':
                if (depth > 0) {
                  if (--depth == 0) {
                    sawClose = true;
                  }
                }
                break;
              default: break;
            }
            break;
          case COMMENT:
            switch (c) {
              case '\n': state = NORMAL; break;
              default: break;
            }
            break;
          case QUOTE:
            switch (c) {
              case '\'': state = NORMAL; break;
              case '\\': state = QUOTE_ESCAPE; break;
              default: break;
            }
            break;
          case QUOTE_ESCAPE:
            break;
          case DQUOTE:
            switch (c) {
              case '\"': state = NORMAL; break;
              case '\\': state = DQUOTE_ESCAPE; break;
              default: break;
            }
            break;
          case DQUOTE_ESCAPE:
            break;
        }
      }

      chars.addAll(buffer);
      input.skip(buffer.size());
    }
  }

  kj::String readOneJson(kj::BufferedInputStreamWrapper& input) {
    // Consume and return one brace-delimited message from the input.
    //
    // Accounts for nested braces, string literals, and comments starting with # or //. Technically
    // JSON does not permit comments but this code is lenient in case we change things later.

    enum {
      NORMAL,
      SLASH,
      COMMENT,
      QUOTE,
      QUOTE_ESCAPE,
      DQUOTE,
      DQUOTE_ESCAPE
    } state = NORMAL;
    uint depth = 0;
    bool sawClose = false;

    kj::Vector<char> chars;

    for (;;) {
      auto buffer = input.tryGetReadBuffer();

      if (buffer == nullptr) {
        // EOF
        chars.add('\0');
        return kj::String(chars.releaseAsArray());
      }

      for (auto i: kj::indices(buffer)) {
        char c = buffer[i];
        switch (state) {
          case SLASH:
            if (c == '/') {
              state = COMMENT;
              break;
            }
            KJ_FALLTHROUGH;
          case NORMAL:
            switch (c) {
              case '#': state = COMMENT; break;
              case '/': state = SLASH; break;
              case '{':
                if (depth == 0 && sawClose) {
                  // We already got one complete message. This is the start of the next message.
                  // Stop here.
                  chars.addAll(buffer.slice(0, i));
                  chars.add('\0');
                  input.skip(i);
                  return kj::String(chars.releaseAsArray());
                }
                ++depth;
                break;
              case '}':
                if (depth > 0) {
                  if (--depth == 0) {
                    sawClose = true;
                  }
                }
                break;
              default: break;
            }
            break;
          case COMMENT:
            switch (c) {
              case '\n': state = NORMAL; break;
              default: break;
            }
            break;
          case QUOTE:
            switch (c) {
              case '\'': state = NORMAL; break;
              case '\\': state = QUOTE_ESCAPE; break;
              default: break;
            }
            break;
          case QUOTE_ESCAPE:
            break;
          case DQUOTE:
            switch (c) {
              case '\"': state = NORMAL; break;
              case '\\': state = DQUOTE_ESCAPE; break;
              default: break;
            }
            break;
          case DQUOTE_ESCAPE:
            break;
        }
      }

      chars.addAll(buffer);
      input.skip(buffer.size());
    }
  }

  class ParseErrorCatcher: public kj::ExceptionCallback {
  public:
    ParseErrorCatcher(kj::ProcessContext& context): context(context) {}
    ~ParseErrorCatcher() noexcept(false) {
      if (!unwindDetector.isUnwinding()) {
        KJ_IF_MAYBE(e, exception) {
          context.error(kj::str(
              "*** ERROR CONVERTING PREVIOUS MESSAGE ***\n"
              "The following error occurred while converting the message above.\n"
              "This probably means the input data is invalid/corrupted.\n",
              "Exception description: ", e->getDescription(), "\n"
              "Code location: ", e->getFile(), ":", e->getLine(), "\n"
              "*** END ERROR ***"));
        }
      }
    }

    void onRecoverableException(kj::Exception&& e) {
      // Only capture the first exception, on the assumption that later exceptions are probably
      // just cascading problems.
      if (exception == nullptr) {
        exception = kj::mv(e);
      }
    }

  private:
    kj::ProcessContext& context;
    kj::Maybe<kj::Exception> exception;
    kj::UnwindDetector unwindDetector;
  };

  void readOneAndConvert(kj::BufferedInputStreamWrapper& input, kj::OutputStream& output) {
    // Since this is a debug tool, lift the usual security limits.  Worse case is the process
    // crashes or has to be killed.
    ReaderOptions options;
    options.nestingLimit = kj::maxValue;
    options.traversalLimitInWords = kj::maxValue;

    ParseErrorCatcher parseErrorCatcher(context);

    switch (convertFrom) {
      case Format::BINARY: {
        capnp::InputStreamMessageReader message(input, options);
        return writeConversion(message.getRoot<AnyStruct>(), output);
      }
      case Format::PACKED: {
        capnp::PackedMessageReader message(input, options);
        return writeConversion(message.getRoot<AnyStruct>(), output);
      }
      case Format::FLAT:
      case Format::CANONICAL: {
        auto allBytes = readAll(input);

        // Technically we don't know if the bytes are aligned so we'd better copy them to a new
        // array. Note that if we have a non-whole number of words we chop off the straggler
        // bytes. This is fine because if those bytes are actually part of the message we will
        // hit an error later and if they are not then who cares?
        auto words = kj::heapArray<word>(allBytes.size() / sizeof(word));
        memcpy(words.begin(), allBytes.begin(), words.size() * sizeof(word));

        kj::ArrayPtr<const word> segments[1] = { words };
        SegmentArrayMessageReader message(segments, options);
        if (convertFrom == Format::CANONICAL) {
          KJ_REQUIRE(message.isCanonical());
        }
        return writeConversion(message.getRoot<AnyStruct>(), output);
      }
      case Format::FLAT_PACKED: {
        auto allBytes = readAll(input);

        auto words = kj::heapArray<word>(computeUnpackedSizeInWords(allBytes));
        kj::ArrayInputStream input(allBytes);
        capnp::_::PackedInputStream unpacker(input);
        unpacker.read(words.asBytes().begin(), words.asBytes().size());
        word dummy;
        KJ_ASSERT(unpacker.tryRead(&dummy, sizeof(dummy), sizeof(dummy)) == 0);

        kj::ArrayPtr<const word> segments[1] = { words };
        SegmentArrayMessageReader message(segments, options);
        return writeConversion(message.getRoot<AnyStruct>(), output);
      }
      case Format::TEXT: {
        auto text = readOneText(input);
        MallocMessageBuilder message;
        TextCodec codec;
        codec.setPrettyPrint(pretty);
        auto root = message.initRoot<DynamicStruct>(rootType);
        codec.decode(text, root);
        return writeConversion(root.asReader(), output);
      }
      case Format::JSON: {
        auto text = readOneJson(input);
        MallocMessageBuilder message;
        JsonCodec codec;
        codec.setPrettyPrint(pretty);
        codec.handleByAnnotation(rootType);
        auto root = message.initRoot<DynamicStruct>(rootType);
        codec.decode(text, root);
        return writeConversion(root.asReader(), output);
      }
    }

    KJ_UNREACHABLE;
  }

  void writeConversion(AnyStruct::Reader reader, kj::OutputStream& output) {
    switch (convertTo) {
      case Format::BINARY: {
        MallocMessageBuilder message(
            segmentSize == 0 ? SUGGESTED_FIRST_SEGMENT_WORDS : segmentSize,
            segmentSize == 0 ? SUGGESTED_ALLOCATION_STRATEGY : AllocationStrategy::FIXED_SIZE);
        message.setRoot(reader);
        capnp::writeMessage(output, message);
        return;
      }
      case Format::PACKED: {
        MallocMessageBuilder message(
            segmentSize == 0 ? SUGGESTED_FIRST_SEGMENT_WORDS : segmentSize,
            segmentSize == 0 ? SUGGESTED_ALLOCATION_STRATEGY : AllocationStrategy::FIXED_SIZE);
        message.setRoot(reader);
        capnp::writePackedMessage(output, message);
        return;
      }
      case Format::FLAT: {
        auto words = kj::heapArray<word>(reader.totalSize().wordCount + 1);
        memset(words.begin(), 0, words.asBytes().size());
        copyToUnchecked(reader, words);
        output.write(words.begin(), words.asBytes().size());
        return;
      }
      case Format::FLAT_PACKED: {
        auto words = kj::heapArray<word>(reader.totalSize().wordCount + 1);
        memset(words.begin(), 0, words.asBytes().size());
        copyToUnchecked(reader, words);
        kj::BufferedOutputStreamWrapper buffered(output);
        capnp::_::PackedOutputStream packed(buffered);
        packed.write(words.begin(), words.asBytes().size());
        return;
      }
      case Format::CANONICAL: {
        auto words = reader.canonicalize();
        output.write(words.begin(), words.asBytes().size());
        return;
      }
      case Format::TEXT: {
        TextCodec codec;
        codec.setPrettyPrint(pretty);
        auto text = codec.encode(reader.as<DynamicStruct>(rootType));
        output.write({text.asBytes(), kj::StringPtr("\n").asBytes()});
        return;
      }
      case Format::JSON: {
        JsonCodec codec;
        codec.setPrettyPrint(pretty);
        codec.handleByAnnotation(rootType);
        auto text = codec.encode(reader.as<DynamicStruct>(rootType));
        output.write({text.asBytes(), kj::StringPtr("\n").asBytes()});
        return;
      }
    }

    KJ_UNREACHABLE;
  }

public:

  // =====================================================================================
  // "decode" command

  kj::MainBuilder::Validity codeBinary() {
    if (packed) return "cannot be used with --packed";
    if (flat) return "cannot be used with --flat";
    binary = true;
    return true;
  }
  kj::MainBuilder::Validity codeFlat() {
    if (binary) return "cannot be used with --binary";
    flat = true;
    return true;
  }
  kj::MainBuilder::Validity codePacked() {
    if (binary) return "cannot be used with --binary";
    packed = true;
    return true;
  }
  kj::MainBuilder::Validity printShort() {
    pretty = false;
    return true;
  }
  kj::MainBuilder::Validity setQuiet() {
    quiet = true;
    return true;
  }
  kj::MainBuilder::Validity setSegmentSize(kj::StringPtr size) {
    if (flat) return "cannot be used with --flat";
    char* end;
    segmentSize = strtol(size.cStr(), &end, 0);
    if (size.size() == 0 || *end != '\0') {
      return "not an integer";
    }
    return true;
  }

  kj::MainBuilder::Validity setRootType(kj::StringPtr input) {
    KJ_ASSERT(sourceFiles.size() == 1);

    class CliArgumentErrorReporter: public ErrorReporter {
    public:
      void addError(uint32_t startByte, uint32_t endByte, kj::StringPtr message) override {
        if (startByte < endByte) {
          error = kj::str(startByte + 1, "-", endByte, ": ", message);
        } else if (startByte > 0) {
          error = kj::str(startByte + 1, ": ", message);
        } else {
          error = kj::str(message);
        }
      }

      bool hadErrors() override {
        return error != nullptr;
      }

      kj::MainBuilder::Validity getValidity() {
        KJ_IF_MAYBE(e, error) {
          return kj::mv(*e);
        } else {
          return true;
        }
      }

    private:
      kj::Maybe<kj::String> error;
    };

    CliArgumentErrorReporter errorReporter;

    capnp::MallocMessageBuilder tokenArena;
    auto lexedTokens = tokenArena.initRoot<capnp::compiler::LexedTokens>();
    lex(input, lexedTokens, errorReporter);

    CapnpParser parser(tokenArena.getOrphanage(), errorReporter);
    auto tokens = lexedTokens.asReader().getTokens();
    CapnpParser::ParserInput parserInput(tokens.begin(), tokens.end());

    bool success = false;

    if (parserInput.getPosition() == tokens.end()) {
      // Empty argument?
      errorReporter.addError(0, 0, "Couldn't parse type name.");
    } else {
      KJ_IF_MAYBE(expression, parser.getParsers().expression(parserInput)) {
        // The input is expected to contain a *single* expression.
        if (parserInput.getPosition() == tokens.end()) {
          // Hooray, now parse it.
          KJ_IF_MAYBE(compiledType,
              sourceFiles[0].compiled.evalType(expression->getReader(), errorReporter)) {
            KJ_IF_MAYBE(type, compiledType->getSchema()) {
              if (type->isStruct()) {
                rootType = type->asStruct();
                success = true;
              } else {
                errorReporter.addError(0, 0, "Type is not a struct.");
              }
            } else {
              // Apparently named a file scope.
              errorReporter.addError(0, 0, "Type is not a struct.");
            }
          }
        } else {
          errorReporter.addErrorOn(parserInput.current(), "Couldn't parse type name.");
        }
      } else {
        auto best = parserInput.getBest();
        if (best == tokens.end()) {
          errorReporter.addError(input.size(), input.size(), "Couldn't parse type name.");
        } else {
          errorReporter.addErrorOn(*best, "Couldn't parse type name.");
        }
      }
    }

    KJ_ASSERT(success || errorReporter.hadErrors());
    return errorReporter.getValidity();
  }

  kj::MainBuilder::Validity decode() {
    convertTo = Format::TEXT;
    convertFrom = formatFromDeprecatedFlags(Format::BINARY);
    return convert();
  }

private:
  enum Plausibility {
    IMPOSSIBLE,
    IMPLAUSIBLE,
    WRONG_TYPE,
    PLAUSIBLE
  };

  bool plausibleOrWrongType(Plausibility p) {
    return p == PLAUSIBLE || p == WRONG_TYPE;
  }

  Plausibility isPlausiblyFlat(kj::ArrayPtr<const byte> prefix, uint segmentCount = 1) {
    if (prefix.size() < 8) {
      // Not enough prefix to say.
      return PLAUSIBLE;
    }

    if ((prefix[0] & 3) == 2) {
      // Far pointer.  Verify the segment ID.
      uint32_t segmentId = prefix[4] | (prefix[5] << 8)
                         | (prefix[6] << 16) | (prefix[7] << 24);
      if (segmentId == 0 || segmentId >= segmentCount) {
        return IMPOSSIBLE;
      } else {
        return PLAUSIBLE;
      }
    }

    if ((prefix[0] & 3) != 0) {
      // Not a struct pointer.
      return IMPOSSIBLE;
    }
    if ((prefix[3] & 0x80) != 0) {
      if (prefix[0] == 0xff && prefix[1] == 0xff && prefix[2] == 0xff && prefix[3] == 0xff &&
          prefix[4] == 0    && prefix[5] == 0    && prefix[6] == 0    && prefix[7] == 0) {
        // This is an empty struct with offset of -1. That's valid.
      } else {
        // Offset is negative (invalid).
        return IMPOSSIBLE;
      }
    }
    if ((prefix[3] & 0xe0) != 0) {
      // Offset is over a gigabyte (implausible).
      return IMPLAUSIBLE;
    }

    uint data = prefix[4] | (prefix[5] << 8);
    uint pointers = prefix[6] | (prefix[7] << 8);

    if (data + pointers > 2048) {
      // Root struct is huge (over 16 KiB).
      return IMPLAUSIBLE;
    }

    auto structSchema = rootType.getProto().getStruct();
    if ((data < structSchema.getDataWordCount() && pointers > structSchema.getPointerCount()) ||
        (data > structSchema.getDataWordCount() && pointers < structSchema.getPointerCount())) {
      // Struct is neither older nor newer than the schema.
      return WRONG_TYPE;
    }

    if (data > structSchema.getDataWordCount() &&
        data - structSchema.getDataWordCount() > 128) {
      // Data section appears to have grown by 1k (128 words).  This seems implausible.
      return WRONG_TYPE;
    }
    if (pointers > structSchema.getPointerCount() &&
        pointers - structSchema.getPointerCount() > 128) {
      // Pointer section appears to have grown by 1k (128 words).  This seems implausible.
      return WRONG_TYPE;
    }

    return PLAUSIBLE;
  }

  Plausibility isPlausiblyBinary(kj::ArrayPtr<const byte> prefix) {
    if (prefix.size() < 8) {
      // Not enough prefix to say.
      return PLAUSIBLE;
    }

    uint32_t segmentCount = prefix[0] | (prefix[1] << 8)
                          | (prefix[2] << 16) | (prefix[3] << 24);

    // Actually, the bytes store segmentCount - 1.
    ++segmentCount;

    if (segmentCount > 65536) {
      // While technically possible, this is so implausible that we should mark it impossible.
      // This helps to make sure we fail fast on packed input.
      return IMPOSSIBLE;
    } else if (segmentCount > 256) {
      // Implausible segment count.
      return IMPLAUSIBLE;
    }

    uint32_t segment0Size = prefix[4] | (prefix[5] << 8)
                          | (prefix[6] << 16) | (prefix[7] << 24);

    if (segment0Size > (1 << 27)) {
      // Segment larger than 1G seems implausible.
      return IMPLAUSIBLE;
    }

    uint32_t segment0Offset = 4 + segmentCount * 4;
    if (segment0Offset % 8 != 0) {
      segment0Offset += 4;
    }
    KJ_ASSERT(segment0Offset % 8 == 0);

    if (prefix.size() < segment0Offset + 8) {
      // Segment 0 is past our prefix, so we can't check it.
      return PLAUSIBLE;
    }

    return isPlausiblyFlat(prefix.slice(segment0Offset, prefix.size()), segmentCount);
  }

  Plausibility isPlausiblyPacked(kj::ArrayPtr<const byte> prefix,
      kj::Function<Plausibility(kj::ArrayPtr<const byte>)> checkUnpacked) {
    kj::Vector<byte> unpacked;

    // Try to unpack a prefix so that we can check it.
    const byte* pos = prefix.begin();
    const byte* end = prefix.end();
    if (end - pos > 1024) {
      // Don't bother unpacking more than 1k.
      end = pos + 1024;
    }
    while (pos < end) {
      byte tag = *pos++;
      for (uint i = 0; i < 8 && pos < end; i++) {
        if (tag & (1 << i)) {
          byte b = *pos++;
          if (b == 0) {
            // A zero byte should have been deflated away.
            return IMPOSSIBLE;
          }
          unpacked.add(b);
        } else {
          unpacked.add(0);
        }
      }

      if (pos == end) {
        break;
      }

      if (tag == 0) {
        uint count = *pos++ * 8;
        unpacked.addAll(kj::repeat(byte(0), count));
      } else if (tag == 0xff) {
        uint count = *pos++ * 8;
        size_t available = end - pos;
        uint n = kj::min(count, available);
        unpacked.addAll(pos, pos + n);
        pos += n;
      }
    }

    return checkUnpacked(unpacked);
  }

  Plausibility isPlausiblyPacked(kj::ArrayPtr<const byte> prefix) {
    return isPlausiblyPacked(prefix, KJ_BIND_METHOD(*this, isPlausiblyBinary));
  }

  Plausibility isPlausiblyPackedFlat(kj::ArrayPtr<const byte> prefix) {
    return isPlausiblyPacked(prefix, [this](kj::ArrayPtr<const byte> prefix) {
      return isPlausiblyFlat(prefix);
    });
  }

  Plausibility isPlausiblyText(kj::ArrayPtr<const byte> prefix) {
    enum { PREAMBLE, COMMENT, BODY } state = PREAMBLE;

    for (char c: prefix.asChars()) {
      switch (state) {
        case PREAMBLE:
          // Before opening parenthesis.
          switch (c) {
            case '(': state = BODY; continue;
            case '#': state = COMMENT; continue;
            case ' ':
            case '\n':
            case '\r':
            case '\t':
            case '\v':
              // whitespace
              break;
            default:
              // Not whitespace, not comment, not open parenthesis. Impossible!
              return IMPOSSIBLE;
          }
          break;
        case COMMENT:
          switch (c) {
            case '\n': state = PREAMBLE; continue;
            default: break;
          }
          break;
        case BODY:
          switch (c) {
            case '\"':
            case '\'':
              // String literal. Let's stop here before things get complicated.
              return PLAUSIBLE;
            default:
              break;
          }
          break;
      }

      if ((static_cast<uint8_t>(c) < 0x20 && c != '\n' && c != '\r' && c != '\t' && c != '\v')
          || c == 0x7f) {
        // Unprintable character.
        return IMPOSSIBLE;
      }
    }

    return PLAUSIBLE;
  }

  Plausibility isPlausiblyJson(kj::ArrayPtr<const byte> prefix) {
    enum { PREAMBLE, COMMENT, BODY } state = PREAMBLE;

    for (char c: prefix.asChars()) {
      switch (state) {
        case PREAMBLE:
          // Before opening parenthesis.
          switch (c) {
            case '{': state = BODY; continue;
            case '#': state = COMMENT; continue;
            case '/': state = COMMENT; continue;
            case ' ':
            case '\n':
            case '\r':
            case '\t':
            case '\v':
              // whitespace
              break;
            default:
              // Not whitespace, not comment, not open brace. Impossible!
              return IMPOSSIBLE;
          }
          break;
        case COMMENT:
          switch (c) {
            case '\n': state = PREAMBLE; continue;
            default: break;
          }
          break;
        case BODY:
          switch (c) {
            case '\"':
              // String literal. Let's stop here before things get complicated.
              return PLAUSIBLE;
            default:
              break;
          }
          break;
      }

      if ((c > 0 && c < ' ' && c != '\n' && c != '\r' && c != '\t' && c != '\v') || c == 0x7f) {
        // Unprintable character.
        return IMPOSSIBLE;
      }
    }

    return PLAUSIBLE;
  }

  Plausibility isPlausibly(Format format, kj::ArrayPtr<const byte> prefix) {
    switch (format) {
      case Format::BINARY     : return isPlausiblyBinary    (prefix);
      case Format::PACKED     : return isPlausiblyPacked    (prefix);
      case Format::FLAT       : return isPlausiblyFlat      (prefix);
      case Format::FLAT_PACKED: return isPlausiblyPackedFlat(prefix);
      case Format::CANONICAL  : return isPlausiblyFlat      (prefix);
      case Format::TEXT       : return isPlausiblyText      (prefix);
      case Format::JSON       : return isPlausiblyJson      (prefix);
    }
    KJ_UNREACHABLE;
  }

  kj::Maybe<Format> guessFormat(kj::ArrayPtr<const byte> prefix) {
    Format candidates[] = {
      Format::BINARY,
      Format::TEXT,
      Format::PACKED,
      Format::JSON,
      Format::FLAT,
      Format::FLAT_PACKED
    };

    for (Format candidate: candidates) {
      if (plausibleOrWrongType(isPlausibly(candidate, prefix))) {
        return candidate;
      }
    }

    return nullptr;
  }

  kj::MainBuilder::Validity checkPlausibility(Format format, kj::ArrayPtr<const byte> prefix) {
    switch (isPlausibly(format, prefix)) {
      case PLAUSIBLE:
        return true;

      case IMPOSSIBLE:
        KJ_IF_MAYBE(guess, guessFormat(prefix)) {
          return kj::str(
             "The input is not in \"", toString(format), "\" format. It looks like it is in \"",
             toString(*guess), "\" format. Try that instead.");
        } else {
          return kj::str(
             "The input is not in \"", toString(format), "\" format.");
        }

      case IMPLAUSIBLE:
        KJ_IF_MAYBE(guess, guessFormat(prefix)) {
          context.warning(kj::str(
              "*** WARNING ***\n"
              "The input data does not appear to be in \"", toString(format), "\" format. It\n"
              "looks like it may be in \"", toString(*guess), "\" format. I'll try to parse\n"
              "it in \"", toString(format), "\" format as you requested, but if it doesn't work,\n"
              "try \"", toString(format), "\" instead. Use --quiet to suppress this warning.\n"
              "*** END WARNING ***\n"));
        } else {
          context.warning(kj::str(
              "*** WARNING ***\n"
              "The input data does not appear to be in \"", toString(format), "\" format, nor\n"
              "in any other known format. I'll try to parse it in \"", toString(format), "\"\n"
              "format anyway, as you requested. Use --quiet to suppress this warning.\n"
              "*** END WARNING ***\n"));
        }
        return true;

      case WRONG_TYPE:
        if (format == Format::FLAT && plausibleOrWrongType(isPlausiblyBinary(prefix))) {
          context.warning(
              "*** WARNING ***\n"
              "The input data does not appear to match the schema that you specified. I'll try\n"
              "to parse it anyway, but if it doesn't look right, please verify that you\n"
              "have the right schema. This could also be because the input is not in \"flat\"\n"
              "format; indeed, it looks like this input may be in regular binary format,\n"
              "so you might want to try \"binary\" instead.  Use --quiet to suppress this\n"
              "warning.\n"
              "*** END WARNING ***\n");
        } else if (format == Format::FLAT_PACKED &&
                   plausibleOrWrongType(isPlausiblyPacked(prefix))) {
          context.warning(
              "*** WARNING ***\n"
              "The input data does not appear to match the schema that you specified. I'll try\n"
              "to parse it anyway, but if it doesn't look right, please verify that you\n"
              "have the right schema. This could also be because the input is not in \"flat-packed\"\n"
              "format; indeed, it looks like this input may be in regular packed format,\n"
              "so you might want to try \"packed\" instead.  Use --quiet to suppress this\n"
              "warning.\n"
              "*** END WARNING ***\n");
        } else {
          context.warning(
              "*** WARNING ***\n"
              "The input data does not appear to be the type that you specified.  I'll try\n"
              "to parse it anyway, but if it doesn't look right, please verify that you\n"
              "have the right type.  Use --quiet to suppress this warning.\n"
              "*** END WARNING ***\n");
        }
        return true;
    }

    KJ_UNREACHABLE;
  }

public:
  // -----------------------------------------------------------------

  kj::MainBuilder::Validity encode() {
    convertFrom = Format::TEXT;
    convertTo = formatFromDeprecatedFlags(Format::BINARY);
    return convert();
  }

  kj::MainBuilder::Validity setEvalOutputFormat(kj::StringPtr format) {
    KJ_IF_MAYBE(f, parseFormatName(format)) {
      convertTo = *f;
      return true;
    } else {
      return kj::str("unknown format: ", format);
    }
  }

  kj::MainBuilder::Validity evalConst(kj::StringPtr name) {
    convertTo = formatFromDeprecatedFlags(convertTo);

    KJ_ASSERT(sourceFiles.size() == 1);

    auto parser = kj::parse::sequence(
        kj::parse::many(
            kj::parse::sequence(
                kj::parse::identifier,
                kj::parse::many(
                    kj::parse::sequence(
                        kj::parse::exactChar<'['>(),
                        kj::parse::integer,
                        kj::parse::exactChar<']'>())),
                kj::parse::oneOf(
                    kj::parse::endOfInput,
                    kj::parse::sequence(
                        kj::parse::exactChar<'.'>(),
                        kj::parse::notLookingAt(kj::parse::endOfInput))))),
        kj::parse::endOfInput);

    kj::parse::IteratorInput<char, const char*> input(name.begin(), name.end());

    kj::Array<kj::Tuple<kj::String, kj::Array<uint64_t>>> nameParts;
    KJ_IF_MAYBE(p, parser(input)) {
      nameParts = kj::mv(*p);
    } else {
      return "invalid syntax";
    }

    auto pos = nameParts.begin();

    // Traverse the path to find a schema.
    uint64_t scopeId = sourceFiles[0].id;
    bool stoppedAtSubscript = false;
    for (; pos != nameParts.end(); ++pos) {
      kj::StringPtr part = kj::get<0>(*pos);

      KJ_IF_MAYBE(childId, compiler->lookup(scopeId, part)) {
        scopeId = *childId;

        if (kj::get<1>(*pos).size() > 0) {
          stoppedAtSubscript = true;
          break;
        }
      } else {
        break;
      }
    }
    Schema schema = compiler->getLoader().get(scopeId);

    // Evaluate this schema to a DynamicValue.
    DynamicValue::Reader value;
    word zeroWord[1];
    memset(&zeroWord, 0, sizeof(zeroWord));
    kj::ArrayPtr<const word> segments[1] = { kj::arrayPtr(zeroWord, 1) };
    SegmentArrayMessageReader emptyMessage(segments);
    switch (schema.getProto().which()) {
      case schema::Node::CONST:
        value = schema.asConst();
        break;

      case schema::Node::STRUCT:
        if (pos == nameParts.end()) {
          return kj::str("'", schema.getShortDisplayName(), "' cannot be evaluated.");
        }

        // Use the struct's default value.
        value = emptyMessage.getRoot<DynamicStruct>(schema.asStruct());
        break;

      default:
        if (stoppedAtSubscript) {
          return kj::str("'", schema.getShortDisplayName(), "' is not a list.");
        } else if (pos != nameParts.end()) {
          return kj::str("'", kj::get<0>(*pos), "' is not defined.");
        } else {
          return kj::str("'", schema.getShortDisplayName(), "' cannot be evaluated.");
        }
    }

    // Traverse the rest of the path as struct fields.
    for (; pos != nameParts.end(); ++pos) {
      kj::StringPtr partName = kj::get<0>(*pos);

      if (!stoppedAtSubscript) {
        if (value.getType() == DynamicValue::STRUCT) {
          auto structValue = value.as<DynamicStruct>();
          KJ_IF_MAYBE(field, structValue.getSchema().findFieldByName(partName)) {
            value = structValue.get(*field);
          } else {
            return kj::str("'", kj::get<0>(pos[-1]), "' has no member '", partName, "'.");
          }
        } else {
          return kj::str("'", kj::get<0>(pos[-1]), "' is not a struct.");
        }
      }

      auto& subscripts = kj::get<1>(*pos);
      for (uint i = 0; i < subscripts.size(); i++) {
        uint64_t subscript = subscripts[i];
        if (value.getType() == DynamicValue::LIST) {
          auto listValue = value.as<DynamicList>();
          if (subscript < listValue.size()) {
            value = listValue[subscript];
          } else {
            return kj::str("'", partName, "[", kj::strArray(subscripts.slice(0, i + 1), "]["),
                           "]' is out-of-bounds.");
          }
        } else {
          if (i > 0) {
            return kj::str("'", partName, "[", kj::strArray(subscripts.slice(0, i), "]["),
                           "]' is not a list.");
          } else {
            return kj::str("'", partName, "' is not a list.");
          }
        }
      }

      stoppedAtSubscript = false;
    }

    // OK, we have a value.  Print it.
    if (convertTo != Format::TEXT) {
      if (value.getType() != DynamicValue::STRUCT) {
        return "not a struct; binary output is only available on structs";
      }

      auto structValue = value.as<DynamicStruct>();
      rootType = structValue.getSchema();
      kj::FdOutputStream output(STDOUT_FILENO);
      writeConversion(structValue, output);
      context.exit();
    } else {
      if (pretty && value.getType() == DynamicValue::STRUCT) {
        context.exitInfo(prettyPrint(value.as<DynamicStruct>()).flatten());
      } else if (pretty && value.getType() == DynamicValue::LIST) {
        context.exitInfo(prettyPrint(value.as<DynamicList>()).flatten());
      } else {
        context.exitInfo(kj::str(value));
      }
    }

    KJ_CLANG_KNOWS_THIS_IS_UNREACHABLE_BUT_GCC_DOESNT;
  }

public:
  // =====================================================================================

  void addError(const kj::ReadableDirectory& directory, kj::PathPtr path,
                SourcePos start, SourcePos end,
                kj::StringPtr message) override {
    auto file = getDisplayName(directory, path);

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
    hadErrors_ = true;
  }

  bool hadErrors() override {
    return hadErrors_;
  }

private:
  kj::ProcessContext& context;
  kj::Own<kj::Filesystem> disk;
  ModuleLoader loader;
  kj::SpaceFor<Compiler> compilerSpace;
  bool compilerConstructed = false;
  kj::Own<Compiler> compiler;

  Compiler::AnnotationFlag annotationFlag = Compiler::COMPILE_ANNOTATIONS;

  uint compileEagerness = Compiler::NODE | Compiler::CHILDREN |
                          Compiler::DEPENDENCIES | Compiler::DEPENDENCY_PARENTS;
  // By default we compile each explicitly listed schema in full, plus first-level dependencies
  // of those schemas, plus the parent nodes of any dependencies.  This is what most code generators
  // require to function.

  struct SourceDirectory {
    kj::Own<const kj::ReadableDirectory> dir;
    bool isSourcePrefix;
  };

  kj::HashMap<kj::Path, SourceDirectory> sourceDirectories;
  // For each import path and source prefix, tracks the directory object we opened for it.
  //
  // Use via getSourceDirectory().

  kj::HashMap<const kj::ReadableDirectory*, kj::String> dirPrefixes;
  // For each open directory object, maps to a path prefix to add when displaying this path in
  // error messages. This keeps track of the original directory name as given by the user, before
  // canonicalization.
  //
  // Use via getDisplayName().

  bool addStandardImportPaths = true;

  Format convertFrom = Format::BINARY;
  Format convertTo = Format::BINARY;
  // For the "convert" command.

  bool binary = false;
  bool flat = false;
  bool packed = false;
  bool pretty = true;
  bool quiet = false;
  uint segmentSize = 0;
  StructSchema rootType;
  // For the "decode" and "encode" commands.

  struct SourceFile {
    uint64_t id;
    Compiler::ModuleScope compiled;
    kj::StringPtr name;
    Module* module;
  };

  kj::Vector<SourceFile> sourceFiles;

  struct OutputDirective {
    kj::ArrayPtr<const char> name;
    kj::Maybe<kj::Path> dir;

    KJ_DISALLOW_COPY(OutputDirective);
    OutputDirective(OutputDirective&&) = default;
    OutputDirective(kj::ArrayPtr<const char> name, kj::Maybe<kj::Path> dir)
        : name(name), dir(kj::mv(dir)) {}
  };
  kj::Vector<OutputDirective> outputs;

  bool hadErrors_ = false;

  kj::Maybe<const kj::ReadableDirectory&> getSourceDirectory(
      kj::StringPtr pathStr, bool isSourcePrefix) {
    auto cwd = disk->getCurrentPath();
    auto path = cwd.evalNative(pathStr);

    if (path.size() == 0) return disk->getRoot();

    KJ_IF_MAYBE(sdir, sourceDirectories.find(path)) {
      sdir->isSourcePrefix = sdir->isSourcePrefix || isSourcePrefix;
      return *sdir->dir;
    }

    if (path == cwd) {
      // Slight hack if the working directory is explicitly specified:
      // - We want to avoid opening a new copy of the working directory, as tryOpenSubdir() would
      //   do.
      // - If isSourcePrefix is true, we need to add it to sourceDirectories to track that.
      //   Otherwise we don't need to add it at all.
      // - We do not need to add it to dirPrefixes since the cwd is already handled in
      //   getDisplayName().
      auto& result = disk->getCurrent();
      if (isSourcePrefix) {
        kj::Own<const kj::ReadableDirectory> fakeOwn(&result, kj::NullDisposer::instance);
        sourceDirectories.insert(kj::mv(path), { kj::mv(fakeOwn), isSourcePrefix });
      }
      return result;
    }

    KJ_IF_MAYBE(dir, disk->getRoot().tryOpenSubdir(path)) {
      auto& result = *dir->get();
      sourceDirectories.insert(kj::mv(path), { kj::mv(*dir), isSourcePrefix });
#if _WIN32
      kj::String prefix = pathStr.endsWith("/") || pathStr.endsWith("\\")
                        ? kj::str(pathStr) : kj::str(pathStr, '\\');
#else
      kj::String prefix = pathStr.endsWith("/") ? kj::str(pathStr) : kj::str(pathStr, '/');
#endif
      dirPrefixes.insert(&result, kj::mv(prefix));
      return result;
    } else {
      return nullptr;
    }
  }

  struct DirPathPair {
    const kj::ReadableDirectory& dir;
    kj::Path path;
  };

  DirPathPair interpretSourceFile(kj::StringPtr pathStr) {
    auto cwd = disk->getCurrentPath();
    auto path = cwd.evalNative(pathStr);

    KJ_REQUIRE(path.size() > 0);
    for (size_t i = path.size() - 1; i > 0; i--) {
      auto prefix = path.slice(0, i);
      auto remainder = path.slice(i, path.size());

      KJ_IF_MAYBE(sdir, sourceDirectories.find(prefix)) {
        if (sdir->isSourcePrefix) {
          return { *sdir->dir, remainder.clone() };
        }
      }
    }

    // No source prefix matched. Fall back to heuristic: try stripping the current directory,
    // otherwise don't strip anything.
    if (path.startsWith(cwd)) {
      return { disk->getCurrent(), path.slice(cwd.size(), path.size()).clone() };
    } else {
      // Hmm, no src-prefix matched and the file isn't even in the current directory. This might
      // be OK if we aren't generating any output anyway, but otherwise the results will almost
      // certainly not be what the user wanted. Let's print a warning, unless the output directives
      // are ones which we know do not produce output files. This is a hack.
      for (auto& output: outputs) {
        auto name = kj::str(output.name);
        if (name != "-" && name != "capnp") {
          context.warning(kj::str(pathStr,
              ": File is not in the current directory and does not match any prefix defined with "
              "--src-prefix. Please pass an appropriate --src-prefix so I can figure out where to "
              "write the output for this file."));
          break;
        }
      }

      return { disk->getRoot(), kj::mv(path) };
    }
  }

  kj::String getDisplayName(const kj::ReadableDirectory& dir, kj::PathPtr path) {
    KJ_IF_MAYBE(prefix, dirPrefixes.find(&dir)) {
      return kj::str(*prefix, path.toNativeString());
    } else if (&dir == &disk->getRoot()) {
      return path.toNativeString(true);
    } else if (&dir == &disk->getCurrent()) {
      return path.toNativeString(false);
    } else {
      KJ_FAIL_ASSERT("unrecognized directory");
    }
  }
};

}  // namespace compiler
}  // namespace capnp

KJ_MAIN(capnp::compiler::CompilerMain);
