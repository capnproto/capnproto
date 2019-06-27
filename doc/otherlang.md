---
layout: page
title: Other Languages
---

# Other Languages

Cap'n Proto's reference implementation is in C++.  Implementations in other languages are
maintained by respective authors and have not been reviewed by me
([@kentonv](https://github.com/kentonv)). Below are the implementations I'm aware
of. Some of these projects are more "ready" than others; please consult each
project's documentation for details.

##### Serialization + RPC

* [C++](cxx.html) by [@kentonv](https://github.com/kentonv)
* [C# (.NET Core)](https://github.com/c80k/capnproto-dotnetcore) by [@c80k](https://github.com/c80k)
* [Erlang](http://ecapnp.astekk.se/) by [@kaos](https://github.com/kaos)
* [Go](https://github.com/zombiezen/go-capnproto2) by [@zombiezen](https://github.com/zombiezen) (forked from [@glycerine](https://github.com/glycerine)'s serialization-only version, below)
* [Haskell](https://github.com/zenhack/haskell-capnp) by [@zenhack](https://github.com/zenhack)
* [Javascript (Node.js only)](https://github.com/capnproto/node-capnp) by [@kentonv](https://github.com/kentonv)
* [OCaml](https://github.com/capnproto/capnp-ocaml) by [@pelzlpj](https://github.com/pelzlpj) with [RPC](https://github.com/mirage/capnp-rpc) by [@talex5](https://github.com/talex5)
* [Python](http://capnproto.github.io/pycapnp/) by [@jparyani](https://github.com/jparyani)
* [Rust](https://github.com/dwrensha/capnproto-rust) by [@dwrensha](https://github.com/dwrensha)

##### Serialization only

* [C](https://github.com/opensourcerouting/c-capnproto) by [OpenSourceRouting](https://www.opensourcerouting.org/) / [@eqvinox](https://github.com/eqvinox) (originally by [@jmckaskill](https://github.com/jmckaskill))
* [D](https://github.com/capnproto/capnproto-dlang) by [@ThomasBrixLarsen](https://github.com/ThomasBrixLarsen)
* [Go](https://github.com/glycerine/go-capnproto) by [@glycerine](https://github.com/glycerine) (originally by [@jmckaskill](https://github.com/jmckaskill))
* [Java](https://github.com/capnproto/capnproto-java/) by [@dwrensha](https://github.com/dwrensha)
* [Javascript](https://github.com/capnp-js/plugin/) by [@popham](https://github.com/popham)
* [Javascript](https://github.com/jscheid/capnproto-js) (older, abandoned) by [@jscheid](https://github.com/jscheid)
* [Lua](https://github.com/cloudflare/lua-capnproto) by [CloudFlare](http://www.cloudflare.com/) / [@calio](https://github.com/calio)
* [Nim](https://github.com/zielmicha/capnp.nim) by [@zielmicha](https://github.com/zielmicha)
* [Ruby](https://github.com/cstrahan/capnp-ruby) by [@cstrahan](https://github.com/cstrahan)
* [Scala](https://github.com/katis/capnp-scala) by [@katis](https://github.com/katis)

##### Tools

These are other misc projects related to Cap'n Proto that are not actually implementations in
new languages.

* [Common Test Framework](https://github.com/kaos/capnp_test) by [@kaos](https://github.com/kaos)
* [Sublime Syntax Highlighting](https://github.com/joshuawarner32/capnproto-sublime) by
  [@joshuawarner32](https://github.com/joshuawarner32)
* [Vim Syntax Highlighting](https://github.com/cstrahan/vim-capnp) by [@cstrahan](https://github.com/cstrahan)
* [Wireshark Dissector Plugin](https://github.com/kaos/wireshark-plugins) by [@kaos](https://github.com/kaos)
* [VS Code Syntax Highlighter](https://marketplace.visualstudio.com/items?itemName=xmonader.vscode-capnp) by [@xmonader](https://github.com/xmonader)
* [IntelliJ Syntax Highlighter](https://github.com/xmonader/sercapnp) by [@xmonader](https://github.com/xmonader)

## Contribute Your Own!

We'd like to support many more languages in the future!

If you'd like to own the implementation of Cap'n Proto in some particular language,
[let us know](https://groups.google.com/group/capnproto)!

**You should e-mail the list _before_ you start hacking.**  We don't bite, and we'll probably have
useful tips that will save you time.  :)

**Do not implement your own schema parser.**  The schema language is more complicated than it
looks, and the algorithm to determine offsets of fields is subtle.  If you reuse the official
parser, you won't risk getting these wrong, and you won't have to spend time keeping your parser
up-to-date.  In fact, you can still write your code generator in any language you want, using
compiler plugins!

### How to Write Compiler Plugins

The Cap'n Proto tool, `capnp`, does not actually know how to generate code.  It only parses schemas,
then hands the parse tree off to another binary -- known as a "plugin" -- which generates the code.
Plugins are independent executables (written in any language) which read a description of the
schema from standard input and then generate the necessary code.  The description is itself a
Cap'n Proto message, defined by
[schema.capnp](https://github.com/sandstorm-io/capnproto/blob/master/c%2B%2B/src/capnp/schema.capnp).
Specifically, the plugin receives a `CodeGeneratorRequest`, using
[standard serialization](encoding.html#serialization-over-a-stream)
(not packed).  (Note that installing the C++ runtime causes schema.capnp to be placed in
`$PREFIX/include/capnp` -- `/usr/local/include/capnp` by default).

Of course, because the input to a plugin is itself in Cap'n Proto format, if you write your
plugin directly in the language you wish to support, you may have a bootstrapping problem:  you
somehow need to generate code for `schema.capnp` before you write your code generator.  Luckily,
because of the simplicity of the Cap'n Proto format, it is generally not too hard to do this by
hand.  Remember that you can use `capnp compile -ocapnp schema.capnp` to get a dump of the sizes
and offsets of all structs and fields defined in the file.

`capnp compile` normally looks for plugins in `$PATH` with the name `capnpc-[language]`, e.g.
`capnpc-c++` or `capnpc-capnp`.  However, if the language name given on the command line contains
a slash character, `capnp` assumes that it is an exact path to the plugin executable, and does not
search `$PATH`.  Examples:

    # Searches $PATH for executable "capnpc-mylang".
    capnp compile -o mylang addressbook.capnp

    # Uses plugin executable "myplugin" from the current directory.
    capnp compile -o ./myplugin addressbook.capnp

If the user specifies an output directory, the compiler will run the plugin with that directory
as the working directory, so you do not need to worry about this.

For examples of plugins, take a look at
[capnpc-capnp](https://github.com/sandstorm-io/capnproto/blob/master/c%2B%2B/src/capnp/compiler/capnpc-capnp.c%2B%2B)
or [capnpc-c++](https://github.com/sandstorm-io/capnproto/blob/master/c%2B%2B/src/capnp/compiler/capnpc-c%2B%2B.c%2B%2B).

### Supporting Dynamic Languages

Dynamic languages have no compile step.  This makes it difficult to work `capnp compile` into the
workflow for such languages.  Additionally, dynamic languages are often scripting languages that do
not support pointer arithmetic or any reasonably-performant alternative.

Fortunately, dynamic languages usually have facilities for calling native code.  The best way to
support Cap'n Proto in a dynamic language, then, is to wrap the C++ library, in particular the
[C++ dynamic API](cxx.html#dynamic-reflection).  This way you get reasonable performance while
still avoiding the need to generate any code specific to each schema.

To parse the schema files, use the `capnp::SchemaParser` class (defined in `capnp/schema-parser.h`).
This way, schemas are loaded at the same time as all the rest of the program's code -- at startup.
An advanced implementation might consider caching the compiled schemas in binary format, then
loading the cached version using `capnp::SchemaLoader`, similar to the way e.g. Python caches
compiled source files as `.pyc` bytecode, but that's up to you.

### Testing Your Implementation

The easiest way to test that you've implemented the spec correctly is to use the `capnp` tool
to [encode](capnp-tool.html#encoding-messages) test inputs and
[decode](capnp-tool.html#decoding-messages) outputs.
