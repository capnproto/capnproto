MSVC project files

As of this writing, this MSVC project is very basic: all it does is compile a
unit test covering all lite-mode functionality. In the long run, we intend for
MSVC libraries to be compiled using the cmake files. This project is a stopgap
because Kenton doesn't understand cmake and wanted to get some work done.

The solution file refers to gtest, which must be downloaded to "c++/gtest".
The "setup-autotools.sh" script accomplishes this, although it requires bash
to run.
