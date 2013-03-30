---
layout: page
---

# Installation

## Cap'n Proto IS NOT READY

As of this writing, Cap'n Proto is in the very early stages of development. It is still missing
many essential features:

* **Security:** There are almost certainly a few exploitable security bugs in the Cap'n Proto
  code. You should not use Cap'n Proto on untrusted data until a proper security review has been
  completed.
* **Stability:** The Cap'n Proto format is still changing. Any data written today probably won't
  be understood by future versions. Additionally, the programming interface is still evolving, so
  code written today probably won't work with future versions.
* **Performance:** While already beating the pants off other systems, Cap'n Proto has not yet
  undergone serious profiling and optimization.
* **RPC:** The RPC protocol has not yet been specified, much less implemented.
* **Support for languages other than C++:** Hasn't been started yet.

Therefore, you should only be installing Cap'n Proto at this time if you just want to play around
with it or help develop it.  If so, great!  Please report your findings to the
[discussion group](https://groups.google.com/group/capnproto).

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

### GCC 4.7 Needed

If you are using GCC, you MUST use at least version 4.7 as Cap'n Proto uses recently-implemented
C++11 features.  If you are using some other compiler...  good luck.

### Building with Ekam

Ekam is a build system I wrote a while back that automatically figures out how to build your C++
code without instructions.  It also supports continuous builds, where it watches the filesystem for
changes (via inotify) and immediately rebuilds as necessary.  Instant feedback is key to
productivity, so I really like using Ekam.

Unfortunately it's very much unfinished.  It works (for me), but it is very quirky.  It only works
on Linux, and is best used together with Eclipse.

The Cap'n Proto repo includes a script which will attempt to set up Ekam for you.

    git clone https://github.com/kentonv/capnproto.git
    cd capnproto/c++
    ./setup-ekam.sh

If all goes well, this downloads the Ekam code into `.ekam` and adds some symlinks under src.
It also imports the [Google Test](https://googletest.googlecode.com) and
[Protobuf](http://protobuf.googlecode.com) source code, so you can compile tests and benchmarks.

Once Ekam is installed, you can do:

    make -f Makefile.ekam continuous

If you use Eclipse, you should use the Ekam Eclipse plugin to get build results fed back into your
editor.  Build the plugin like so:

1. Open the `.ekam/eclipse` directory as an Eclipse project.
2. File -> Export -> Plug-in Development -> Deployable Plug-ins and Fragments.
3. Choose the Ekam Dashboard project and export to your Eclipse directory, or export to another
   directory and copy the files into your Eclipse directory.
4. Restart Eclipse.
5. Make sure you have some sort of project in your work space containing your Ekam source tree. It
   should be rooted at the directory containing "src", "tmp", etc. The plugin will mark errors
   within this project.
6. Window -> Show View -> Other -> Ekam -> Ekam Dashboard

The dashboard view lets you browse the whole tree and also populates your editor with error
markers.

### Building With Automake

If setting up Ekam is too much work for you, you can also build with Automake.

1. Make sure the [Google Test](https://googletest.googlecode.com/) headers are in your include
   path and `libgtest.a` and `libgtest_main.a` are in your library path.  Google Test has
   apparently decided that `make install` is evil so you have to do this somewhat manually.  :(

       wget https://googletest.googlecode.com/files/gtest-1.6.0.zip
       unzip gtest-1.6.0.zip
       cd gtest-1.6.0
       ./configure
       make
       cp -r include/gtest $PREFIX/include/gtest
       cp ./lib/.libs/*.a $PREFIX/lib

2. Clone and build the Cap'n Proto code.

       git clone https://github.com/kentonv/capnproto.git
       cd capnproto/c++
       autoreconf -i
       ./configure
       make check
