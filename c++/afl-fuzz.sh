#! /bin/bash

set -euo pipefail

echo "Choose test case:"
echo "1) TestAllTypes parsing"
echo "2) TestLists parsing"
echo "3) Canonicalization"

read -p "choice: " -n 1 TESTCASE
echo

case "$TESTCASE" in
  1 )
    TESTDATA=binary
    FLAGS=
    TESTNAME=default
    ;;
  2 )
    TESTDATA=lists.binary
    FLAGS=--lists
    TESTNAME=lists
    ;;
  3 )
    TESTDATA=binary
    FLAGS=--canonicalize
    TESTNAME=canonicalize
    ;;
  * )
    echo "Invalid choice: $TESTCASE" >&2
    exit 1
esac

echo "Choose compiler:"
echo "1) GCC"
echo "2) Clang"

read -p "choice: " -n 1 TESTCASE
echo

case "$TESTCASE" in
  1 )
    export CXX=afl-g++
    ;;
  2 )
    export CXX=afl-clang++
    ;;
  * )
    echo "Invalid choice: $TESTCASE" >&2
    exit 1
esac

if [ -e Makefile ]; then
  if ! grep -q '^CXX *= *'"$CXX" Makefile; then
    # Wrong compiler used.
    make distclean
    $(dirname $0)/configure --disable-shared
  fi
else
  $(dirname $0)/configure --disable-shared
fi

make -j$(nproc)
make -j$(nproc) capnp-afl-testcase

NOW=$(date +%Y-%m-%d.%H-%M-%S).$TESTNAME.$CXX

mkdir afl.$NOW.inputs afl.$NOW.findings

cp $(dirname $0)/src/capnp/testdata/$TESTDATA afl.$NOW.inputs

afl-fuzz -i afl.$NOW.inputs -o afl.$NOW.findings -- ./capnp-afl-testcase $FLAGS
