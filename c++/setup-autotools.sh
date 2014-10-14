#! /usr/bin/env bash

set -euo pipefail

if [ ! -e gtest ]; then
  echo "================================================================================"
  echo "Fetching Google Test code..."
  echo "================================================================================"
  svn checkout http://googletest.googlecode.com/svn/tags/release-1.7.0 gtest
fi

echo "================================================================================"
echo "Done"
echo "================================================================================"
echo
echo "Ready to run autoreconf.  For example:"
echo "  autoreconf -i && ./configure && make -j6 check && sudo make install"
