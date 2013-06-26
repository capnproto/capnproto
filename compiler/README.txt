Cap'n Proto - Insanely Fast Data Serialization Format
Copyright 2013 Kenton Varda
http://kentonv.github.com/capnproto/

Cap'n Proto is an insanely fast data interchange format and capability-based
RPC system.  Think JSON, except binary.  Or think of Google's Protocol Buffers
(http://protobuf.googlecode.com), except faster.  In fact, in benchmarks,
Cap'n Proto is INFINITY TIMES faster than Protocol Buffers.

This package is the executable tool which parses Cap'n Proto schema definitions
and generates corresponding source code in various target languages.  To be
useful, you will also need to obtain a runtime library for your target
language.  These are distributed separately.

Full installation and usage instructions and other documentation are maintained
on the Cap'n Proto web site:
  http://kentonv.github.io/capnproto/install.html
  
To build and install, simply do:
  cabal install capnproto-compiler.cabal
