---
layout: page
title: The capnp Tool
---

# The `capnp` Tool

Cap'n Proto comes with a command-line tool called `capnp` intended to aid development and
debugging.  This tool can be used to:

* Compile Cap'n Proto schemas to produce source code in multiple languages.
* Generate unique type IDs.
* Convert Cap'n Proto messages between binary, text, and JSON formats.
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

`capnp decode` and `capnp encode` are deprecated. Use `capnp convert` to convert messages
between any of the supported formats:

    capnp convert [options] <from>:<to> [schema-file] [type]

The command reads messages from standard input in the `<from>` format and writes them to standard
output in the `<to>` format. Both formats must be specified; supported formats are:

* `binary`: Standard Cap'n Proto serialization format.
* `packed`: Packed binary format, which compresses zero-valued bytes.
* `flat`: A single-segment binary message with no segment table.
* `flat-packed`: A single-segment message in packed format.
* `canonical`: Canonicalized, single-segment binary format with no segment table.
* `text`: A struct literal in the format used by the [schema language](language.html).
* `json`: JSON representation of the message.

When either format is `text` or `json`, provide the schema file and the root struct type. For
conversions between binary formats, the schema file and type are not needed.

For example, convert a binary message to schema-language text:

    capnp convert binary:text myschema.capnp MyType < message.bin > message.txt

Convert text to packed binary:

    capnp convert text:packed myschema.capnp MyType < message.txt > message.packed

Convert a binary message to JSON:

    capnp convert binary:json myschema.capnp MyType < message.bin > message.json

Convert between binary formats without specifying a schema:

    capnp convert binary:packed < message.bin > message.packed

The `--short` option writes text or JSON output on one line without pretty-printing:

    capnp convert --short binary:json myschema.capnp MyType < message.bin

The `--quiet` option suppresses warnings when the input does not appear to match the specified
format:

    capnp convert --quiet packed:binary < message.packed > message.bin

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
