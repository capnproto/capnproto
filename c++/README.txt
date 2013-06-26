Cap'n Proto - Insanely Fast Data Serialization Format
Copyright 2013 Kenton Varda
http://kentonv.github.com/capnproto/

Cap'n Proto is an insanely fast data interchange format and capability-based
RPC system.  Think JSON, except binary.  Or think of Google's Protocol Buffers
(http://protobuf.googlecode.com), except faster.  In fact, in benchmarks,
Cap'n Proto is INFINITY TIMES faster than Protocol Buffers.

This package contains the C++ runtime implementation of Cap'n Proto.  To build
and use it, you will first need to install the Cap'n Proto compiler, capnpc,
which comes in a separate package.

Full installation and usage instructions and other documentation are maintained
on the Cap'n Proto web site:
  http://kentonv.github.io/capnproto/install.html

WARNING:  You must be using either GCC 4.7+ or Clang 3.2+.  On OSX, Xcode 4's
command-line tools are NOT sufficient -- see the web site for step-by-step
instructions to get Clang 3.2 working on OSX.

To build and install (from a release package), simply do:
  ./configure
  make -j4 check
  sudo make install

The -j4 allows the build to use up to four processor cores instead of one.
You can increase this number if you have more cores.  Specifying "check"
says to run tests in addition to building.  This can be omitted to make the
build slightly faster, but running tests and reporting failures back to the
developers helps us out!

