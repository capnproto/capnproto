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
echo "Ready to run cmake (no support for installing yet).  For example:"
echo "  mkdir build && cd build && cmake .. -G 'Unix Makefiles' && make -j6 check"
