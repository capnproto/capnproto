---
layout: page
title: The capnp Tool
---

# The `capnp` Tool

Cap'n Proto comes with a command-line tool called `capnp` intended to aid development and
debugging.  This tool can be used to:

* Compile Cap'n Proto schemas to produce source code in multiple languages.
* Generate unique type IDs.
* Decode Cap'n Proto messages to human-readable text.
* Encode text representations of Cap'n Proto messages to binary.
* Evaluate and extract constants defined in Cap'n Proto schemas.

This page summarizes the functionality.  A complete reference on the command's usage can be
found by typing:

    capnp help

## Compiling Schemas

    capnp compile -oc++ myschema.capnp

This generates files `myschema.capnp.h` and `myschema.capnp.c++` which contain C++ source code
corresponding to the types defined in `myschema.capnp`.  Options exist to control output location
and import paths.

The above example generates C++ code, but the tool is able to generate output in any language
for which a plugin is available.  Compiler plugins are just regular programs named
`capnpc-language`.  For example, the above command runs `capnpc-c++`.  [More on how to write
compiler plugins](otherlang.html#how-to-write-compiler-plugins).

Note that some Cap'n Proto implementations (especially for interpreted languages) do not require
generating source code.

## Converting Messages

    capnp convert json:binary addressbook.capnp AddressBook < addressbook.json > addressbook.bin

`capnp convert` reads a Cap'n Proto message from standard input encoded in one format and writes
the message to standard output in another format. The example above is converting from json to
binary format. The Cap'n Proto schema file and type must be specified when converting either to
or from a text encoding. These arguments are ignored and can be omitted for binary-to-binary
conversions as in the example below.

    capnp convert binary:packed < addressbook.bin > addressbook.packed

This is mainly useful for debugging purposes, to build test data or to apply tweaks to data while
text encoded. You should not rely on `capnp convert` for encoding data written and maintained in
text format long-term -- instead, use `capnp eval`, which is much more powerful.

## Evaluating Constants

    capnp eval myschema.capnp myConstant

This prints the value of `myConstant`, a [const](language.html#constants) declaration, after
applying variable substitution.  It can also output the value in binary format (`--binary` or
`--packed`).

At first glance, this may seem no more interesting than `capnp encode`:  the syntax used to define
constants in schema files is the same as the format accepted by `capnp encode`, right?  There is,
however, a big difference:  constants in schema files may be defined in terms of other constants,
which may even be imported from other files.

As a result, `capnp eval` is a great basis for implementing config files.  For example, a large
company might maintain a production server that serves dozens of clients and needs configuration
information about each one.  Rather than maintaining the config as one enormous file, it can be
written as several separate files with a master file that imports the rest.

Such a configuration should be compiled to binary format using `capnp eval` before deployment,
in order to verify that there are no errors and to make deployment easier and faster.  While you
could technically ship the text configs to production and have the servers parse them directly
(e.g. with `capnp::SchemaParser`), encoding before deployment is more efficient and robust.
