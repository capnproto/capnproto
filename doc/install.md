---
layout: page
title: Installation
---

# Installation: Tools and C++ Runtime

The Cap'n Proto tools, including the compiler (which takes `.capnp` files and generates source code
for them), are written in C++.  Therefore, you must install the C++ package even if your actual
development language is something else.

This package is licensed under the [MIT License](http://opensource.org/licenses/MIT).

## Caveats

<p style="font-size: 125%; font-weight: bold;">Cap'n Proto is in BETA</p>

<div style="float: right"><a class="groups_link" style="color: #fff"
href="https://groups.google.com/group/capnproto-announce">Sign Up for Updates</a></div>

As of this writing, Cap'n Proto is in beta.  The serialization layer is close to feature-complete
and we don't anticipate any further changes to the wire format.  That said, if you want to use it,
you should keep in mind some caveats:

* **Security:** Cap'n Proto has not yet had a security review.  Although Kenton has a background
  in security and is not aware of any vulnerabilities in the current code, it's likely that there
  are a few, and [some have been found](https://github.com/sandstorm-io/capnproto/tree/master/security-advisories)
  in the past.  For now, do not accept Cap'n Proto messages from parties you do not trust.
* **API Stability:** The Cap'n Proto programming interface may still change in ways that break
  existing code.  Such changes are likely to be minor and should not affect the wire format.
* **Performance:** While Cap'n Proto is inherently fast by design, the implementation has not yet
  undergone serious profiling and optimization.  Currently it only beats Protobufs in realistic-ish
  end-to-end benchmarks by around 2x-5x.  We can do better.
* **RPC:** The RPC implementation particularly experimental, though it is used heavily by
  [Sandstorm.io](https://sandstorm.io).

If you'd like to hack on Cap'n Proto, you should join the
[discussion group](https://groups.google.com/group/capnproto)!

If you'd just like to receive updates as things progress, add yourself to the
[announce list](https://groups.google.com/group/capnproto-announce).

## Prerequisites

### Supported Compilers

Cap'n Proto makes extensive use of C++11 language features. As a result, it requires a relatively
new version of a well-supported compiler. The minimum versions are:

* GCC 4.8
* Clang 3.3
* Visual C++ 2015

If your system's default compiler is older that the above, you will need to install a newer
compiler and set the `CXX` environment variable before trying to build Cap'n Proto. For example,
after installing GCC 4.8, you could set `CXX=g++-4.8` to use this compiler.

### Supported Operating Systems

In theory, Cap'n Proto should work on any POSIX platform supporting one of the above compilers,
as well as on Windows. We test every Cap'n Proto release on the following platforms:

* Android
* Linux
* Mac OS X
* Windows - Cygwin
* Windows - MinGW-w64
* Windows - Visual C++

**Windows users:** Cap'n Proto requires Visual Studio 2015 Update 3 or newer. All runtime features
of Cap'n Proto -- including serialization and RPC -- are now supported. (It is still not possible to
compile the code generator tool, capnp.exe, using Visual Studio; however, a precompiled copy built
with MinGW is provided in the release zip for your convenience.)

**Mac OS X users:** You must use at least Xcode 5 with the Xcode command-line
tools (Xcode menu > Preferences > Downloads).  Alternatively, the command-line tools
package from [Apple](https://developer.apple.com/downloads/) or compiler builds from
[Macports](http://www.macports.org/), [Fink](http://www.finkproject.org/), or
[Homebrew](http://brew.sh/) are reported to work.

## Installation: Unix

**From Release Tarball**

You may download and install the release version of Cap'n Proto like so:

<pre><code>curl -O <a href="https://capnproto.org/capnproto-c++-0.6.0.tar.gz">https://capnproto.org/capnproto-c++-0.6.0.tar.gz</a>
tar zxf capnproto-c++-0.6.0.tar.gz
cd capnproto-c++-0.6.0
./configure
make -j6 check
sudo make install</code></pre>

This will install `capnp`, the Cap'n Proto command-line tool.  It will also install `libcapnp`,
`libcapnpc`, and `libkj` in `/usr/local/lib` and headers in `/usr/local/include/capnp` and
`/usr/local/include/kj`.

**From Package Managers**

Some package managers include Cap'n Proto packages.

Note: These packages are not maintained by us and are sometimes not up to date with the latest Cap'n Proto release.

* Debian / Ubuntu: `apt-get install capnproto`
* Homebrew (OSX): `brew install capnp`

**From Git**

If you download directly from Git, you will need to have the GNU autotools --
[autoconf](http://www.gnu.org/software/autoconf/),
[automake](http://www.gnu.org/software/automake/), and
[libtool](http://www.gnu.org/software/libtool/) -- installed.

    git clone https://github.com/sandstorm-io/capnproto.git
    cd capnproto/c++
    autoreconf -i
    ./configure
    make -j6 check
    sudo make install

## Installation: Windows

**From Release Zip**

1. Download Cap'n Proto Win32 build:

   <pre><a href="https://capnproto.org/capnproto-c++-win32-0.6.0.zip">https://capnproto.org/capnproto-c++-win32-0.6.0.zip</a></pre>

2. Find `capnp.exe`, `capnpc-c++.exe`, and `capnpc-capnp.exe` under `capnproto-tools-win32-0.6.0` in
   the zip and copy them somewhere.

If you don't care about C++ support, you can stop here. The compiler exe can be used with plugins
provided by projects implementing Cap'n Proto in other languages.

If you want to use Cap'n Proto in C++ with Visual Studio, do the following:

1. Make sure that you are using Visual Studio 2015 or newer, with all updates installed. Cap'n
   Proto uses C++11 language features that did not work in previous versions of Visual Studio,
   and the updates include many bug fixes that Cap'n Proto requires.

2. Install [CMake](http://www.cmake.org/) version 3.1 or later.

3. Use CMake to generate Visual Studio project files under `capnproto-c++-0.6.0` in the zip file.
   You can use the CMake UI for this or run this shell command:

       cmake -G "Visual Studio 14 2015"

    (For VS2017, you can use "Visual Studio 15 2017" as the generator name.)

3. Open the "Cap'n Proto" solution in Visual Studio.

4. Adjust the project build options (e.g., choice of C++ runtime library, enable/disable exceptions
   and RTTI) to match the options of the project in which you plan to use Cap'n Proto.

5. Build the solution (`ALL_BUILD`).

6. Build the `INSTALL` project to copy the compiled libraries, tools, and header files into
   `CMAKE_INSTALL_PREFIX`.

   Alternatively, find the compiled `.lib` files in the build directory under
   `src/{capnp,kj}/{Debug,Release}` and place them somewhere where your project can link against them.
   Also add the `src` directory to your search path for `#include`s, or copy all the headers to your
   project's include directory.

Cap'n Proto can also be built with MinGW or Cygwin, using the Unix/autotools build instructions.

**From Git**

The C++ sources are located under `c++` directory in the git repository. The build instructions are
otherwise the same as for the release zip.

