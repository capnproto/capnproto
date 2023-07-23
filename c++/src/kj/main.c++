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
#include "win32-api-version.h"
#endif

#include "main.h"
#include "debug.h"
#include "arena.h"
#include "miniposix.h"
#include <map>
#include <set>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>

#if _WIN32
#include <windows.h>
#include "windows-sanity.h"
#else
#include <sys/uio.h>
#endif

namespace kj {

// =======================================================================================

TopLevelProcessContext::TopLevelProcessContext(StringPtr programName)
    : programName(programName),
      cleanShutdown(getenv("KJ_CLEAN_SHUTDOWN") != nullptr) {
  printStackTraceOnCrash();
}

StringPtr TopLevelProcessContext::getProgramName() {
  return programName;
}

void TopLevelProcessContext::exit() {
  int exitCode = hadErrors ? 1 : 0;
  if (cleanShutdown) {
#if KJ_NO_EXCEPTIONS
    // This is the best we can do.
    warning("warning: KJ_CLEAN_SHUTDOWN may not work correctly when compiled "
            "with -fno-exceptions.");
    ::exit(exitCode);
#else
    throw CleanShutdownException { exitCode };
#endif
  }
  _exit(exitCode);
}

#if _WIN32
void setStandardIoMode(int fd) {
  // Set mode to binary if the fd is not a console.
  HANDLE handle = reinterpret_cast<HANDLE>(_get_osfhandle(fd));
  DWORD consoleMode;
  if (GetConsoleMode(handle, &consoleMode)) {
    // It's a console.
  } else {
    KJ_SYSCALL(_setmode(fd, _O_BINARY));
  }
}
#else
void setStandardIoMode(int fd) {}
#endif

static void writeLineToFd(int fd, StringPtr message) {
  // Write the given message to the given file descriptor with a trailing newline iff the message
  // is non-empty and doesn't already have a trailing newline.  We use writev() to do this in a
  // single system call without any copying (OS permitting).

  if (message.size() == 0) {
    return;
  }

#if _WIN32
  KJ_STACK_ARRAY(char, newlineExpansionBuffer, 2 * (message.size() + 1), 128, 512);
  char* p = newlineExpansionBuffer.begin();
  for(char ch : message) {
    if(ch == '\n') {
      *(p++) = '\r';
    }
    *(p++) = ch;
  }
  if(!message.endsWith("\n")) {
    *(p++) = '\r';
    *(p++) = '\n';
  }

  size_t newlineExpandedSize = p - newlineExpansionBuffer.begin();

  KJ_ASSERT(newlineExpandedSize <= newlineExpansionBuffer.size());

  HANDLE handle = reinterpret_cast<HANDLE>(_get_osfhandle(fd));
  DWORD consoleMode;
  bool redirectedToFile = !GetConsoleMode(handle, &consoleMode);

  DWORD writtenSize;
  if(redirectedToFile) {
    WriteFile(handle, newlineExpansionBuffer.begin(), newlineExpandedSize, &writtenSize, nullptr);
  } else {
    KJ_STACK_ARRAY(wchar_t, buffer, newlineExpandedSize, 128, 512);

    size_t finalSize = MultiByteToWideChar(
      CP_UTF8,
      0,
      newlineExpansionBuffer.begin(),
      newlineExpandedSize,
      buffer.begin(),
      buffer.size());

    KJ_ASSERT(finalSize <= buffer.size());

    WriteConsoleW(handle, buffer.begin(), finalSize, &writtenSize, nullptr);
  }
#else
  // Unfortunately the writev interface requires non-const pointers even though it won't modify
  // the data.
  struct iovec vec[2];
  vec[0].iov_base = const_cast<char*>(message.begin());
  vec[0].iov_len = message.size();
  vec[1].iov_base = const_cast<char*>("\n");
  vec[1].iov_len = 1;

  struct iovec* pos = vec;

  // Only use the second item in the vec if the message doesn't already end with \n.
  uint count = message.endsWith("\n") ? 1 : 2;

  for (;;) {
    ssize_t n = writev(fd, pos, count);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      } else {
        // This function is meant for writing to stdout and stderr.  If writes fail on those FDs
        // there's not a whole lot we can reasonably do, so just ignore it.
        return;
      }
    }

    // Update chunks to discard what was successfully written.
    for (;;) {
      if (count == 0) {
        // Done writing.
        return;
      } else if (pos->iov_len <= implicitCast<size_t>(n)) {
        // Wrote this entire chunk.
        n -= pos->iov_len;
        ++pos;
        --count;
      } else {
        // Wrote only part of this chunk.  Adjust the pointer and then retry.
        pos->iov_base = reinterpret_cast<byte*>(pos->iov_base) + n;
        pos->iov_len -= n;
        break;
      }
    }
  }
#endif
}

void TopLevelProcessContext::warning(StringPtr message) {
  writeLineToFd(STDERR_FILENO, message);
}

void TopLevelProcessContext::error(StringPtr message) {
  hadErrors = true;
  writeLineToFd(STDERR_FILENO, message);
}

void TopLevelProcessContext::exitError(StringPtr message) {
  error(message);
  exit();
}

void TopLevelProcessContext::exitInfo(StringPtr message) {
  writeLineToFd(STDOUT_FILENO, message);
  exit();
}

void TopLevelProcessContext::increaseLoggingVerbosity() {
  // At the moment, there is only one log level that isn't enabled by default.
  _::Debug::setLogLevel(_::Debug::Severity::INFO);
}

// =======================================================================================

int runMainAndExit(ProcessContext& context, MainFunc&& func, int argc, char* argv[]) {
  setStandardIoMode(STDIN_FILENO);
  setStandardIoMode(STDOUT_FILENO);
  setStandardIoMode(STDERR_FILENO);

#if !KJ_NO_EXCEPTIONS
  try {
#endif
    KJ_ASSERT(argc > 0);

    KJ_STACK_ARRAY(StringPtr, params, argc - 1, 8, 32);
    for (int i = 1; i < argc; i++) {
      params[i - 1] = argv[i];
    }

    KJ_IF_MAYBE(exception, runCatchingExceptions([&]() {
      func(argv[0], params);
    })) {
      context.error(str("*** Uncaught exception ***\n", *exception));
    }
    context.exit();
#if !KJ_NO_EXCEPTIONS
  } catch (const TopLevelProcessContext::CleanShutdownException& e) {
    return e.exitCode;
  }
#endif
  KJ_CLANG_KNOWS_THIS_IS_UNREACHABLE_BUT_GCC_DOESNT
}

// =======================================================================================

struct MainBuilder::Impl {
  inline Impl(ProcessContext& context, StringPtr version,
              StringPtr briefDescription, StringPtr extendedDescription)
      : context(context), version(version),
        briefDescription(briefDescription), extendedDescription(extendedDescription) {}

  ProcessContext& context;
  StringPtr version;
  StringPtr briefDescription;
  StringPtr extendedDescription;

  Arena arena;

  struct CharArrayCompare {
    inline bool operator()(const ArrayPtr<const char>& a, const ArrayPtr<const char>& b) const {
      int cmp = memcmp(a.begin(), b.begin(), min(a.size(), b.size()));
      if (cmp == 0) {
        return a.size() < b.size();
      } else {
        return cmp < 0;
      }
    }
  };

  struct Option {
    ArrayPtr<OptionName> names;
    bool hasArg;
    union {
      Function<Validity()>* func;
      Function<Validity(StringPtr)>* funcWithArg;
    };
    StringPtr argTitle;
    StringPtr helpText;
  };

  class OptionDisplayOrder;

  std::map<char, Option*> shortOptions;
  std::map<ArrayPtr<const char>, Option*, CharArrayCompare> longOptions;

  struct SubCommand {
    Function<MainFunc()> func;
    StringPtr helpText;
  };
  std::map<StringPtr, SubCommand> subCommands;

  struct Arg {
    StringPtr title;
    Function<Validity(StringPtr)> callback;
    uint minCount;
    uint maxCount;
  };

  Vector<Arg> args;

  Maybe<Function<Validity()>> finalCallback;

  Option& addOption(std::initializer_list<OptionName> names, bool hasArg, StringPtr helpText) {
    KJ_REQUIRE(names.size() > 0, "option must have at least one name");

    Option& option = arena.allocate<Option>();
    option.names = arena.allocateArray<OptionName>(names.size());
    uint i = 0;
    for (auto& name: names) {
      option.names[i++] = name;
      if (name.isLong) {
        KJ_REQUIRE(
            longOptions.insert(std::make_pair(StringPtr(name.longName).asArray(), &option)).second,
            "duplicate option", name.longName);
      } else {
        KJ_REQUIRE(
            shortOptions.insert(std::make_pair(name.shortName, &option)).second,
            "duplicate option", name.shortName);
      }
    }
    option.hasArg = hasArg;
    option.helpText = helpText;
    return option;
  }

  Validity printVersion() {
    context.exitInfo(version);
    return true;
  }

  Validity increaseVerbosity() {
    context.increaseLoggingVerbosity();
    return true;
  }
};

MainBuilder::MainBuilder(ProcessContext& context, StringPtr version,
                         StringPtr briefDescription, StringPtr extendedDescription)
    : impl(heap<Impl>(context, version, briefDescription, extendedDescription)) {
  addOption({"verbose"}, KJ_BIND_METHOD(*impl, increaseVerbosity),
            "Log informational messages to stderr; useful for debugging.");
  addOption({"version"}, KJ_BIND_METHOD(*impl, printVersion),
            "Print version information and exit.");
}

MainBuilder::~MainBuilder() noexcept(false) {}

MainBuilder& MainBuilder::addOption(std::initializer_list<OptionName> names,
                                    Function<Validity()> callback,
                                    StringPtr helpText) {
  impl->addOption(names, false, helpText).func = &impl->arena.copy(kj::mv(callback));
  return *this;
}

MainBuilder& MainBuilder::addOptionWithArg(std::initializer_list<OptionName> names,
                                           Function<Validity(StringPtr)> callback,
                                           StringPtr argumentTitle, StringPtr helpText) {
  auto& opt = impl->addOption(names, true, helpText);
  opt.funcWithArg = &impl->arena.copy(kj::mv(callback));
  opt.argTitle = argumentTitle;
  return *this;
}

MainBuilder& MainBuilder::addSubCommand(StringPtr name, Function<MainFunc()> getSubParser,
                                        StringPtr helpText) {
  KJ_REQUIRE(impl->args.size() == 0, "cannot have sub-commands when expecting arguments");
  KJ_REQUIRE(impl->finalCallback == nullptr,
             "cannot have a final callback when accepting sub-commands");
  KJ_REQUIRE(
      impl->subCommands.insert(std::make_pair(
          name, Impl::SubCommand { kj::mv(getSubParser), helpText })).second,
      "duplicate sub-command", name);
  return *this;
}

MainBuilder& MainBuilder::expectArg(StringPtr title, Function<Validity(StringPtr)> callback) {
  KJ_REQUIRE(impl->subCommands.empty(), "cannot have sub-commands when expecting arguments");
  impl->args.add(Impl::Arg { title, kj::mv(callback), 1, 1 });
  return *this;
}
MainBuilder& MainBuilder::expectOptionalArg(
    StringPtr title, Function<Validity(StringPtr)> callback) {
  KJ_REQUIRE(impl->subCommands.empty(), "cannot have sub-commands when expecting arguments");
  impl->args.add(Impl::Arg { title, kj::mv(callback), 0, 1 });
  return *this;
}
MainBuilder& MainBuilder::expectZeroOrMoreArgs(
    StringPtr title, Function<Validity(StringPtr)> callback) {
  KJ_REQUIRE(impl->subCommands.empty(), "cannot have sub-commands when expecting arguments");
  impl->args.add(Impl::Arg { title, kj::mv(callback), 0, UINT_MAX });
  return *this;
}
MainBuilder& MainBuilder::expectOneOrMoreArgs(
    StringPtr title, Function<Validity(StringPtr)> callback) {
  KJ_REQUIRE(impl->subCommands.empty(), "cannot have sub-commands when expecting arguments");
  impl->args.add(Impl::Arg { title, kj::mv(callback), 1, UINT_MAX });
  return *this;
}

MainBuilder& MainBuilder::callAfterParsing(Function<Validity()> callback) {
  KJ_REQUIRE(impl->finalCallback == nullptr, "callAfterParsing() can only be called once");
  KJ_REQUIRE(impl->subCommands.empty(), "cannot have a final callback when accepting sub-commands");
  impl->finalCallback = kj::mv(callback);
  return *this;
}

class MainBuilder::MainImpl {
public:
  MainImpl(Own<Impl>&& impl): impl(kj::mv(impl)) {}

  void operator()(StringPtr programName, ArrayPtr<const StringPtr> params);

private:
  Own<Impl> impl;

  KJ_NORETURN(void usageError(StringPtr programName, StringPtr message));
  KJ_NORETURN(void printHelp(StringPtr programName));
  void wrapText(Vector<char>& output, StringPtr indent, StringPtr text);
};

MainFunc MainBuilder::build() {
  return MainImpl(kj::mv(impl));
}

void MainBuilder::MainImpl::operator()(StringPtr programName, ArrayPtr<const StringPtr> params) {
  Vector<StringPtr> arguments;

  for (size_t i = 0; i < params.size(); i++) {
    StringPtr param = params[i];
    if (param == "--") {
      // "--" ends option parsing.
      arguments.addAll(params.begin() + i + 1, params.end());
      break;
    } else if (param.startsWith("--")) {
      // Long option.
      ArrayPtr<const char> name;
      Maybe<StringPtr> maybeArg;
      KJ_IF_MAYBE(pos, param.findFirst('=')) {
        name = param.slice(2, *pos);
        maybeArg = param.slice(*pos + 1);
      } else {
        name = param.slice(2);
      }
      auto iter = impl->longOptions.find(name);
      if (iter == impl->longOptions.end()) {
        if (param == "--help") {
          printHelp(programName);
        } else {
          usageError(programName, str("--", name, ": unrecognized option"));
        }
      } else {
        const Impl::Option& option = *iter->second;
        if (option.hasArg) {
          // Argument expected.
          KJ_IF_MAYBE(arg, maybeArg) {
            // "--foo=blah": "blah" is the argument.
            KJ_IF_MAYBE(error, (*option.funcWithArg)(*arg).releaseError()) {
              usageError(programName, str(param, ": ", *error));
            }
          } else if (i + 1 < params.size() &&
                     !(params[i + 1].startsWith("-") && params[i + 1].size() > 1)) {
            // "--foo blah": "blah" is the argument.
            ++i;
            KJ_IF_MAYBE(error, (*option.funcWithArg)(params[i]).releaseError()) {
              usageError(programName, str(param, "=", params[i], ": ", *error));
            }
          } else {
            usageError(programName, str("--", name, ": missing argument"));
          }
        } else {
          // No argument expected.
          if (maybeArg == nullptr) {
            KJ_IF_MAYBE(error, (*option.func)().releaseError()) {
              usageError(programName, str(param, ": ", *error));
            }
          } else {
            usageError(programName, str("--", name, ": option does not accept an argument"));
          }
        }
      }
    } else if (param.startsWith("-") && param.size() > 1) {
      // Short option(s).
      for (uint j = 1; j < param.size(); j++) {
        char c = param[j];
        auto iter = impl->shortOptions.find(c);
        if (iter == impl->shortOptions.end()) {
          usageError(programName, str("-", c, ": unrecognized option"));
        } else {
          const Impl::Option& option = *iter->second;
          if (option.hasArg) {
            // Argument expected.
            if (j + 1 < param.size()) {
              // Rest of flag is argument.
              StringPtr arg = param.slice(j + 1);
              KJ_IF_MAYBE(error, (*option.funcWithArg)(arg).releaseError()) {
                usageError(programName, str("-", c, " ", arg, ": ", *error));
              }
              break;
            } else if (i + 1 < params.size() &&
                       !(params[i + 1].startsWith("-") && params[i + 1].size() > 1)) {
              // Next parameter is argument.
              ++i;
              KJ_IF_MAYBE(error, (*option.funcWithArg)(params[i]).releaseError()) {
                usageError(programName, str("-", c, " ", params[i], ": ", *error));
              }
              break;
            } else {
              usageError(programName, str("-", c, ": missing argument"));
            }
          } else {
            // No argument expected.
            KJ_IF_MAYBE(error, (*option.func)().releaseError()) {
              usageError(programName, str("-", c, ": ", *error));
            }
          }
        }
      }
    } else if (!impl->subCommands.empty()) {
      // A sub-command name.
      auto iter = impl->subCommands.find(param);
      if (iter != impl->subCommands.end()) {
        MainFunc subMain = iter->second.func();
        subMain(str(programName, ' ', param), params.slice(i + 1, params.size()));
        return;
      } else if (param == "help") {
        if (i + 1 < params.size()) {
          iter = impl->subCommands.find(params[i + 1]);
          if (iter != impl->subCommands.end()) {
            // Run the sub-command with "--help" as the argument.
            MainFunc subMain = iter->second.func();
            StringPtr dummyArg = "--help";
            subMain(str(programName, ' ', params[i + 1]), arrayPtr(&dummyArg, 1));
            return;
          } else if (params[i + 1] == "help") {
            uint count = 0;
            for (uint j = i + 2;
                 j < params.size() && (params[j] == "help" || params[j] == "--help");
                 j++) {
              ++count;
            }

            switch (count) {
              case 0:
                impl->context.exitInfo("Help about help?  We must go deeper...");
                break;
              case 1:
                impl->context.exitInfo(
                    "Yo dawg, I heard you like help.  So I wrote you some help about how to use "
                    "help so you can get help on help.");
                break;
              case 2:
                impl->context.exitInfo("Help, I'm trapped in a help text factory!");
                break;
              default:
                if (count < 10) {
                  impl->context.exitError("Killed by signal 911 (SIGHELP)");
                } else {
                  impl->context.exitInfo("How to keep an idiot busy...");
                }
                break;
            }
          } else {
            usageError(programName, str(params[i + 1], ": unknown command"));
          }
        } else {
          printHelp(programName);
        }
      } else {
        // Arguments are not accepted, so this is an error.
        usageError(programName, str(param, ": unknown command"));
      }
    } else {
      // Just a regular argument.
      arguments.add(param);
    }
  }

  // ------------------------------------
  // Handle arguments.
  // ------------------------------------

  if (!impl->subCommands.empty()) {
    usageError(programName, "missing command");
  }

  // Count the number of required arguments, so that we know how to distribute the optional args.
  uint requiredArgCount = 0;
  for (auto& argSpec: impl->args) {
    requiredArgCount += argSpec.minCount;
  }

  // Now go through each argument spec and consume arguments with it.
  StringPtr* argPos = arguments.begin();
  for (auto& argSpec: impl->args) {
    uint i = 0;
    for (; i < argSpec.minCount; i++) {
      if (argPos == arguments.end()) {
        usageError(programName, str("missing argument ", argSpec.title));
      } else {
        KJ_IF_MAYBE(error, argSpec.callback(*argPos).releaseError()) {
          usageError(programName, str(*argPos, ": ", *error));
        }
        ++argPos;
        --requiredArgCount;
      }
    }

    // If we have more arguments than we need, and this argument spec will accept extras, give
    // them to it.
    for (; i < argSpec.maxCount && arguments.end() - argPos > requiredArgCount; ++i) {
      KJ_IF_MAYBE(error, argSpec.callback(*argPos).releaseError()) {
        usageError(programName, str(*argPos, ": ", *error));
      }
      ++argPos;
    }
  }

  // Did we consume all the arguments?
  while (argPos < arguments.end()) {
    usageError(programName, str(*argPos++, ": too many arguments"));
  }

  // Run the final callback, if any.
  KJ_IF_MAYBE(f, impl->finalCallback) {
    KJ_IF_MAYBE(error, (*f)().releaseError()) {
      usageError(programName, *error);
    }
  }
}

void MainBuilder::MainImpl::usageError(StringPtr programName, StringPtr message) {
  impl->context.exitError(kj::str(
      programName, ": ", message,
      "\nTry '", programName, " --help' for more information."));
  KJ_CLANG_KNOWS_THIS_IS_UNREACHABLE_BUT_GCC_DOESNT
}

class MainBuilder::Impl::OptionDisplayOrder {
public:
  bool operator()(const Option* a, const Option* b) const {
    if (a == b) return false;

    char aShort = '\0';
    char bShort = '\0';

    for (auto& name: a->names) {
      if (name.isLong) {
        if (aShort == '\0') {
          aShort = name.longName[0];
        }
      } else {
        aShort = name.shortName;
        break;
      }
    }
    for (auto& name: b->names) {
      if (name.isLong) {
        if (bShort == '\0') {
          bShort = name.longName[0];
        }
      } else {
        bShort = name.shortName;
        break;
      }
    }

    if (aShort < bShort) return true;
    if (aShort > bShort) return false;

    StringPtr aLong;
    StringPtr bLong;

    for (auto& name: a->names) {
      if (name.isLong) {
        aLong = name.longName;
        break;
      }
    }
    for (auto& name: b->names) {
      if (name.isLong) {
        bLong = name.longName;
        break;
      }
    }

    return aLong < bLong;
  }
};

void MainBuilder::MainImpl::printHelp(StringPtr programName) {
  Vector<char> text(1024);

  std::set<const Impl::Option*, Impl::OptionDisplayOrder> sortedOptions;

  for (auto& entry: impl->shortOptions) {
    sortedOptions.insert(entry.second);
  }
  for (auto& entry: impl->longOptions) {
    sortedOptions.insert(entry.second);
  }

  text.addAll(str("Usage: ", programName, sortedOptions.empty() ? "" : " [<option>...]"));

  if (impl->subCommands.empty()) {
    for (auto& arg: impl->args) {
      text.add(' ');
      if (arg.minCount == 0) {
        text.addAll(str("[", arg.title, arg.maxCount > 1 ? "...]" : "]"));
      } else {
        text.addAll(str(arg.title, arg.maxCount > 1 ? "..." : ""));
      }
    }
  } else {
    text.addAll(StringPtr(" <command> [<arg>...]"));
  }
  text.addAll(StringPtr("\n\n"));

  wrapText(text, "", impl->briefDescription);

  if (!impl->subCommands.empty()) {
    text.addAll(StringPtr("\nCommands:\n"));
    size_t maxLen = 0;
    for (auto& command: impl->subCommands) {
      maxLen = kj::max(maxLen, command.first.size());
    }
    for (auto& command: impl->subCommands) {
      text.addAll(StringPtr("  "));
      text.addAll(command.first);
      for (size_t i = command.first.size(); i < maxLen; i++) {
        text.add(' ');
      }
      text.addAll(StringPtr("  "));
      text.addAll(command.second.helpText);
      text.add('\n');
    }
    text.addAll(str(
        "\nSee '", programName, " help <command>' for more information on a specific command.\n"));
  }

  if (!sortedOptions.empty()) {
    text.addAll(StringPtr("\nOptions:\n"));

    for (auto opt: sortedOptions) {
      text.addAll(StringPtr("    "));
      bool isFirst = true;
      for (auto& name: opt->names) {
        if (isFirst) {
          isFirst = false;
        } else {
          text.addAll(StringPtr(", "));
        }
        if (name.isLong) {
          text.addAll(str("--", name.longName));
          if (opt->hasArg) {
            text.addAll(str("=", opt->argTitle));
          }
        } else {
          text.addAll(str("-", name.shortName));
          if (opt->hasArg) {
            text.addAll(opt->argTitle);
          }
        }
      }
      text.add('\n');
      wrapText(text, "        ", opt->helpText);
    }

    text.addAll(StringPtr("    --help\n        Display this help text and exit.\n"));
  }

  if (impl->extendedDescription.size() > 0) {
    text.add('\n');
    wrapText(text, "", impl->extendedDescription);
  }

  text.add('\0');
  impl->context.exitInfo(String(text.releaseAsArray()));
  KJ_CLANG_KNOWS_THIS_IS_UNREACHABLE_BUT_GCC_DOESNT
}

void MainBuilder::MainImpl::wrapText(Vector<char>& output, StringPtr indent, StringPtr text) {
  uint width = 80 - indent.size();

  while (text.size() > 0) {
    output.addAll(indent);

    KJ_IF_MAYBE(lineEnd, text.findFirst('\n')) {
      if (*lineEnd <= width) {
        output.addAll(text.slice(0, *lineEnd + 1));
        text = text.slice(*lineEnd + 1);
        continue;
      }
    }

    if (text.size() <= width) {
      output.addAll(text);
      output.add('\n');
      break;
    }

    uint wrapPos = width;
    for (;; wrapPos--) {
      if (wrapPos == 0) {
        // Hmm, no good place to break words.  Just break in the middle.
        wrapPos = width;
        break;
      } else if (text[wrapPos] == ' ' && text[wrapPos - 1] != ' ') {
        // This position is a space and is preceded by a non-space.  Wrap here.
        break;
      }
    }

    output.addAll(text.slice(0, wrapPos));
    output.add('\n');

    // Skip spaces after the text that was printed.
    while (text[wrapPos] == ' ') {
      ++wrapPos;
    }
    if (text[wrapPos] == '\n') {
      // Huh, apparently there were a whole bunch of spaces at the end of the line followed by a
      // newline.  Skip the newline as well so we don't print a blank line.
      ++wrapPos;
    }
    text = text.slice(wrapPos);
  }
}

}  // namespace kj
