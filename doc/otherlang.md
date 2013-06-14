---
layout: page
---

# Other Languages

Cap'n Proto's reference implementation is in C++.  Implementations in other languages are
maintained by respective authors and have not been reviewed by me
([@kentonv](https://github.com/kentonv)).  Here are some implementations I'm aware of:

* [C and Go](https://github.com/jmckaskill/go-capnproto) by
  [@jmckaskill](https://github.com/jmckaskill)
* [Boost Python wrappers for Cap'n Proto C++](https://github.com/jparyani/capnproto) by
  [@jparyani](https://github.com/jparyani)

## Contribute Your Own!

We'd like to support many more languages in the future!

If you'd like to own the implementation of Cap'n Proto in some particular language,
[let us know](https://groups.google.com/group/capnproto)!

**You should e-mail the list _before_ you start hacking.**  We don't bite, and we'll probably have
useful tips that will save you time.  :)

**Do not implement your own schema parser.**  The schema language is more complicated than it
looks, and the algorithm to determine offsets of fields is subtle.  If you reuse `capnpc`'s parser,
you won't risk getting these wrong, and you won't have to spend time keeping your parser up-to-date.
In fact, you can still write your code generator in any language you want, using compiler plugins!

### How to Write Compiler Plugins

The Cap'n Proto compiler / code generator binary, `capnpc`, supports a "plugin" interface for
custom code generators.  Plugins are independent executables (written in any language) which read
a description of the schema from standard input and then generate the necessary code.  The
description is itself a Cap'n Proto message, defined by
[schema.capnp](https://github.com/kentonv/capnproto/blob/master/c%2B%2B/src/capnproto/schema.capnp)
(the file will move to a better location eventually).  Specifically, the plugin receives a
`CodeGeneratorRequest`, using
[standard serialization](http://kentonv.github.io/capnproto/encoding.html#serialization_over_a_stream)
(not packed).

Of course, because the input to a plugin is itself in Cap'n Proto format, if you write your
plugin directly in the language you wish to support, you may have a bootstrapping problem:  you
somehow need to generate code for `schema.capnp` before you write your code generator.  Luckily,
because of the simplicity of the Cap'n Proto format, it is generally not too hard to do this by
hand.  Remember that you can use `capnpc -v schema.capnp` to get a dump of the sizes and offsets
of all structs and fields defined in the file.

`capnpc` normally looks for plugins in `$PATH` with the name `capnpc-[language]`.  However, if the
language name given on the command line contains a slash character, `capnpc` assumes that it is an
exact path to the plugin executable, and does not search `$PATH`.  Examples:

    # Searches $PATH for executable "capnpc-mylang".
    capnpc -o mylang addressbook.capnp

    # Uses plugin executable "myplugin" from the current directory.
    capnpc -o ./myplugin addressbook.capnp

If the user specifies an output directory, the compiler will run the plugin with that directory
as the working directory, so you do not need to worry about this.

[This example plugin](https://github.com/kentonv/capnproto/blob/master/c%2B%2B/src/capnp/compiler/capnpc-capnp.c%2B%2B)
writes the schema back to standard output in Cap'n Proto schema language, similar to what
`capnpc -v` does.
