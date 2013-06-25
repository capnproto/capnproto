#! /bin/bash

set -euo pipefail

if [ ! -e gtest ]; then
  echo "================================================================================"
  echo "Fetching Google Test code..."
  echo "================================================================================"
  svn checkout http://googletest.googlecode.com/svn/tags/release-1.6.0 gtest

  echo "================================================================================"
  echo "Patching Google Test for C++11..."
  echo "================================================================================"
  cd gtest
  patch -p0 < ../gtest-1.6.0-c++11.patch
  cd ..
fi

echo "================================================================================"
echo "Done"
echo "================================================================================"
echo
echo "Ready to run autoreconf.  For example:"
echo "  autoreconf -i && ./configure && make -j6 check && sudo make install"
