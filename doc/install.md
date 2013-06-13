---
layout: page
---

# Installation

<p style="font-size: 125%; font-weight: bold;">Note: Cap'n Proto is not ready yet</p>

<div style="float: right"><a class="groups_link" style="color: #fff"
href="https://groups.google.com/group/capnproto-announce">Sign Up for Updates</a></div>

As of this writing, Cap'n Proto is in the very early stages of development. It is still missing
many essential features:

* **Security:** There are almost certainly a few exploitable security bugs in the Cap'n Proto
  code. You should not use Cap'n Proto on untrusted data until a proper security review has been
  completed.
* **Stability:** The Cap'n Proto format is still changing. Any data written today probably won't
  be understood by future versions. Additionally, the programming interface is still evolving, so
  code written today probably won't work with future versions.
* **Performance:** While Cap'n Proto is inherently fast by design, the implementation has not yet
  undergone serious profiling and optimization.  Currenlty it only beats Protobufs in realistic-ish
  end-to-end benchmarks by, like, 2x-5x.  We can do better.
* **RPC:** The RPC protocol has not yet been specified, much less implemented.
* **Support for languages other than C++:** Hasn't been started yet.
* Many other little things.

Therefore, these instructions are for those that would like to hack on Cap'n Proto.  If that's you,
you should join the [discussion group](https://groups.google.com/group/capnproto)!

Or, if you just want to know when it's ready, add yourself to the
[announce list](https://groups.google.com/group/capnproto-announce).

## Installing the Cap'n Proto Compiler

`capnpc`, which takes `.capnp` files and generates source code for them (e.g. in C++), is itself
written in Haskell.

First, install [Cabal](http://www.haskell.org/cabal/), e.g. on Ubuntu:

    sudo apt-get install cabal-install

Now you can check out, build, and install `capnpc` like so:

    git clone https://github.com/kentonv/capnproto.git
    cd capnproto/compiler
    cabal install capnproto-compiler.cabal

Be sure that the Cabal bin directory (typically `$HOME/.cabal/bin`) is in your `PATH` before you
attempt to build the C++ runtime.

## Installing the C++ Runtime

### GCC 4.7 or Clang 3.2 Needed

If you are using GCC, you MUST use at least version 4.7 as Cap'n Proto uses recently-implemented
C++11 features.  If GCC 4.7 is installed but your system's default GCC is older, you will probably
need to set the environment variable `CXX=g++-4.7` before following the instructions below.

If you are using Clang, you must use at least version 3.2.  To use Clang, set the environment
variable `CXX=clang++` before following any instructions below, otherwise `g++` is used by default.

### Building with Ekam

Ekam is a build system I wrote a while back that automatically figures out how to build your C++
code without instructions.  It also supports continuous builds, where it watches the filesystem for
changes (via inotify) and immediately rebuilds as necessary.  Instant feedback is key to
productivity, so I really like using Ekam.

Unfortunately it's very much unfinished.  It works (for me), but it is quirky and rough around the
edges.  It only works on Linux, and is best used together with Eclipse.  If you find it
unacceptable, scroll down to the [Automake instructions](#building_with_automake).

The Cap'n Proto repo includes a script which will attempt to set up Ekam for you.

    git clone https://github.com/kentonv/capnproto.git
    cd capnproto/c++
    ./setup-ekam.sh

If all goes well, this downloads the Ekam code into a directory called `.ekam` and adds some
symlinks under src.  It also imports the [Google Test](https://googletest.googlecode.com) and
[Protobuf](http://protobuf.googlecode.com) source code, so you can compile tests and benchmarks.

Once Ekam is installed, you can do:

    make -f Makefile.ekam once

This will build everything it can and run tests.

Note that Ekam will fail to build some things and output a bunch of error messages.  You should
be able to ignore any errors that originate outside of the `capnp` and `kj` directories -- these
are just parts of other packages like Google Test that Ekam doesn't fully know how to build, but
aren't needed by Cap'n Proto anyway.

### Running the Benchmarks

Before getting into benchmarks, let me be frank:  performance varies wildly by use case, and no
benchmark is going to properly reflect the big picture.  If performance is critical to your use
case, you should write a benchmark specific to your case, and test multiple serialization
technologies.  Don't assume anything.  If you find Cap'n Proto performs sub-optimally, though,
[tell us about it](https://groups.google.com/group/capnproto).

That said, Cap'n Proto does have a small suite of silly benchmarks used to validate changes.

The Ekam build will put the benchmark binaries in `tmp/capnp/benchmark`.

    tmp/capnp/benchmark

This runs the default test case, CatRank.  CatRank simulates a search engine scoring algorithm
which promotes pages that discuss cats (and demotes ones discussing dogs).  A list of up to 1000
random search results with URLs, scores, and snippets is sent to the server, which searches the
snippets for instances of "cat" and "dog", adjusts their scores accordingly, then returns the new
result list sorted by score.

This test case is very string-heavy.  Cap'n Proto performs well due to its zero-copy strings, but
packing the message doesn't help much.

    tmp/capnp/benchmark eval

In this test case, the client generates a random, deeply-nested arithmetic expression for the
server to evaluate.  This case is a pathologically bad case for Cap'n Proto as it involves lots of
pointers with relatively little actual data.  When packing is enabled it actually loses to
Protobufs by a little bit on CPU time (as of this writing, at least; it'll probably get better with
optimization).

    tmp/capnp/benchmark carsales

This test case involves sending to the server a description of a bunch of cars, and asks the server
to decide how much the lot is worth.  This case is very number-heavy, and because of this
Cap'n Proto's "packed" mode really shines.

### Developing with Ekam

If you intend to do some development, you should build `continuous` or `continuous-opt` instead
of `once`.  These modes will build everything, then watch the source tree for changes and rebuild
as necessary.  `continuous` does a debug build while `continuous-opt` optimizes; the former is best
while developing but don't run the benchmarks in debug mode!

If you use Eclipse, you should use the Ekam Eclipse plugin to get build results fed back into your
editor while building in continuous mode.  Build the plugin like so:

1. Open the `.ekam/eclipse` directory as an Eclipse project.
2. File -> Export -> Plug-in Development -> Deployable Plug-ins and Fragments.
3. Choose the Ekam Dashboard project and export to your Eclipse directory, or export to another
   directory and copy the files into your Eclipse directory.
4. Restart Eclipse.
5. Make sure you have some sort of project in your work space containing your Ekam source tree. It
   should be rooted at the directory containing "src", "tmp", etc. The plugin will mark errors
   within this project.
6. Window -> Show View -> Other -> Ekam -> Ekam Dashboard

The dashboard view lets you browse the whole tree of build actions and also populates your editor
with error markers.

### Building with Automake

If setting up Ekam is too much work for you, you can also build with Automake.

1. Make sure the [Google Test](https://googletest.googlecode.com/) headers are in your include
   path and `libgtest.a` and `libgtest_main.a` are in your library path.  Google Test has
   apparently decided that `make install` is evil so you have to do this somewhat manually.  :(

       wget https://googletest.googlecode.com/files/gtest-1.6.0.zip
       unzip gtest-1.6.0.zip
       cd gtest-1.6.0
       ./configure
       make -j4
       mkdir -p ~/gtest-install/include
       mkdir -p ~/gtest-install/lib
       cp -r include/gtest ~/gtest-install/include/gtest
       cp ./lib/.libs/*.a ~/gtest-install/lib

2. Clone and build the Cap'n Proto code.

       git clone https://github.com/kentonv/capnproto.git
       cd capnproto/c++
       autoreconf -i
       CXXFLAGS="-O2 -DNDEBUG -I$HOME/gtest-install/include" \
           LDFLAGS="-L$HOME/gtest-install/lib" \
           ./configure
       make -j4 check
       sudo make install

This will install `libcapnp.a` in `/usr/local/lib` and headers in `/usr/local/include/capnp` and
`/usr/local/include/kj`.
