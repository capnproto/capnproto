MSVC project files

As of this writing, this MSVC project is very basic: all it does is compile a
unit test covering all lite-mode functionality. In the long run, we intend for
MSVC libraries to be compiled using the cmake files. This project is a stopgap
because Kenton doesn't understand cmake and wanted to get some work done.

The solution file refers to gtest, which must be downloaded to "c++/gtest".
The "setup-autotools.sh" script accomplishes this, although it requires bash
to run.

The solution also refers to generated code for test schemas which should be
under msvc/capnp (i.e. a directory called "capnp" within *this* directory).
To generate these files, do the following:

1. Build capnp.exe and capnpc-c++.exe with MinGW. (Or, download the
   precompiled binaries if they are available.)
2. Copy those files to this directory, or somewhere in PATH.
3. Run gen-test-code.bat.
