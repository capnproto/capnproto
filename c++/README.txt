Cap'n Proto - Insanely Fast Data Serialization Format
Copyright 2013-2015 Sandstorm Development Group, Inc.
https://capnproto.org

Cap'n Proto is an insanely fast data interchange format and capability-based
RPC system.  Think JSON, except binary.  Or think of Google's Protocol Buffers
(http://protobuf.googlecode.com), except faster.  In fact, in benchmarks,
Cap'n Proto is INFINITY TIMES faster than Protocol Buffers.

Full installation and usage instructions and other documentation are maintained
on the Cap'n Proto web site:
  http://kentonv.github.io/capnproto/install.html

WARNING: Cap'n Proto requires a modern compiler. See the above link for
detailed requirements.

To build and install (from a release package), simply do:
  ./configure
  make -j4 check
  sudo make install

The -j4 allows the build to use up to four processor cores instead of one.
You can increase this number if you have more cores.  Specifying "check"
says to run tests in addition to building.  This can be omitted to make the
build slightly faster, but running tests and reporting failures back to the
developers helps us out!

