#! /bin/bash

set -euo pipefail

doit() {
  echo "@@@@ $@"
  "$@"
}

if [ $# -gt 0 ]; then
  if [ "x$1" == "xtest" ]; then
    : # nothing
  elif [ "x$1" == "xremote" ]; then
    if [ "$#" -lt 2 ]; then
      echo "usage: $0 remote HOST [COMMAND]" >&2
      exit 1
    fi
    HOST=$2
    shift 2
    echo "========================================================================="
    echo "Pushing code to $HOST..."
    echo "========================================================================="
    ssh $HOST 'rm -rf tmp-test-capnp && mkdir tmp-test-capnp && git init tmp-test-capnp'
    git push ssh://$HOST/~/tmp-test-capnp master:test
    ssh $HOST "cd tmp-test-capnp && git checkout test && ./super-test.sh $@ && cd .. && rm -rf tmp-test-capnp"
    exit 0
  elif [ "x$1" == "xclang" ]; then
    export CXX=clang++
  elif [ "x$1" == "xgcc-4.8" ]; then
    export CXX=g++-4.8
  elif [ "x$1" == "xkenton" ]; then
    cat << "__EOF__"
=========================================================================
*************************************************************************
  _     _                        ____  ____ ____
 | |   (_)_ __  _   ___  __     / ___|/ ___/ ___|
 | |   | | '_ \| | | \ \/ /____| |  _| |  | |
 | |___| | | | | |_| |>  <_____| |_| | |__| |___
 |_____|_|_| |_|\__,_/_/\_\     \____|\____\____|

*************************************************************************
=========================================================================
__EOF__
    $0 test
    $0 clean
    cat << "__EOF__"
=========================================================================
*************************************************************************
   ___  ______  __      ____ _
  / _ \/ ___\ \/ /     / ___| | __ _ _ __   __ _
 | | | \___ \\  /_____| |   | |/ _` | '_ \ / _` |
 | |_| |___) /  \_____| |___| | (_| | | | | (_| |
  \___/|____/_/\_\     \____|_|\__,_|_| |_|\__, |
                                           |___/
*************************************************************************
=========================================================================
__EOF__
    $0 remote beat
    cat << "__EOF__"
=========================================================================
*************************************************************************
   ____                     _
  / ___|   _  __ ___      _(_)_ __
 | |  | | | |/ _` \ \ /\ / / | '_ \
 | |__| |_| | (_| |\ V  V /| | | | |
  \____\__, |\__, | \_/\_/ |_|_| |_|
       |___/ |___/
*************************************************************************
=========================================================================
__EOF__
    $0 remote Kenton@flashman
    cat << "__EOF__"
=========================================================================
*************************************************************************
   ____  ____ ____   _  _    ___
  / ___|/ ___/ ___| | || |  ( _ )
 | |  _| |  | |     | || |_ / _ \
 | |_| | |__| |___  |__   _| (_) |
  \____|\____\____|    |_|(_)___/

*************************************************************************
=========================================================================
__EOF__
    $0 gcc-4.8
    $0 clean
    cat << "__EOF__"
=========================================================================
*************************************************************************
  _     _                        ____ _
 | |   (_)_ __  _   ___  __     / ___| | __ _ _ __   __ _
 | |   | | '_ \| | | \ \/ /____| |   | |/ _` | '_ \ / _` |
 | |___| | | | | |_| |>  <_____| |___| | (_| | | | | (_| |
 |_____|_|_| |_|\__,_/_/\_\     \____|_|\__,_|_| |_|\__, |
                                                    |___/
*************************************************************************
=========================================================================
__EOF__
    $0 clang
    $0 clean
    cat << "__EOF__"
=========================================================================
*************************************************************************
  ____  _   _ ___ ____    ___ _____
 / ___|| | | |_ _|  _ \  |_ _|_   _|
 \___ \| |_| || || |_) |  | |  | |
  ___) |  _  || ||  __/   | |  | |
 |____/|_| |_|___|_|     |___| |_|

*************************************************************************
=========================================================================
__EOF__
    exit 0
  elif [ "x$1" == "xclean" ]; then
    rm -rf tmp-staging
    cd c++
    if [ -e Makefile ]; then
      doit make maintainer-clean
    fi
    rm -f capnproto-*.tar.gz samples/addressbook samples/addressbook.capnp.c++ \
          samples/addressbook.capnp.h
    exit 0
  elif [ "x$1" == "xhelp" ]; then
    echo "usage: $0 [COMMAND]"
    echo "commands:"
    echo "  test          Runs tests (the default)."
    echo "  clang         Runs tests using Clang compiler."
    echo "  gcc-4.8       Runs tests using gcc-4.8."
    echo "  remote HOST   Runs tests on HOST via SSH."
    echo "  kenton        Kenton's meta-test (uses hosts on Kenton's network)."
    echo "  clean         Delete temporary files that may be left after failure."
    echo "  help          Prints this help text."
    exit 0
  else
    echo "unknown command: $1" >&2
    echo "try: $0 help" >&2
    exit 1
  fi
fi

rm -rf tmp-staging
mkdir tmp-staging
STAGING=$PWD/tmp-staging

mkdir $STAGING/bin
mkdir $STAGING/lib
export PATH=$STAGING/bin:$PATH
export LD_LIBRARY_PATH=$STAGING/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}

echo "========================================================================="
echo "Building c++"
echo "========================================================================="

if [ "x`uname`" == xDarwin ]; then
  if [ ! -e ~/clang-3.2/bin/clang++ ]; then
    echo "You need to put the clang-3.2 binaries in ~/clang-3.2." >&2
    exit 1
  fi
  export CXX=~/clang-3.2/bin/clang++
  SAMPLE_CXXFLAGS=-stdlib=libc++
else
  SAMPLE_CXXFLAGS=
fi

cd c++
doit ./setup-autotools.sh | tr = -
doit autoreconf -i
doit ./configure --prefix="$STAGING"
doit make -j6 check

echo "========================================================================="
echo "Testing c++ install"
echo "========================================================================="

doit make install

test "x$(which capnp)" == "x$STAGING/bin/capnp"
test "x$(which capnpc-c++)" == "x$STAGING/bin/capnpc-c++"

cd samples
doit capnp compile -oc++ addressbook.capnp -I"$STAGING"/include --no-standard-import
doit ${CXX:-g++} -std=c++11 $SAMPLE_CXXFLAGS -I"$STAGING"/include -L"$STAGING"/lib \
    addressbook.c++ addressbook.capnp.c++ -lcapnp -lkj -pthread -o addressbook
echo "@@@@ ./addressbook (in various configurations)"
./addressbook write | ./addressbook read
./addressbook dwrite | ./addressbook dread
rm addressbook addressbook.capnp.c++ addressbook.capnp.h
cd ..

echo "========================================================================="
echo "Testing --with-external-capnp"
echo "========================================================================="

doit make distclean
doit ./configure --prefix="$STAGING" --disable-shared \
    --with-external-capnp CAPNP=$STAGING/bin/capnp
doit make -j6 check
doit make distclean

# Test 32-bit build now while we have $STAGING available for cross-compiling.
if [ "x`uname -m`" = "xx86_64" ]; then
  echo "========================================================================="
  echo "Testing 32-bit build"
  echo "========================================================================="

  if [[ "`uname`" =~ CYGWIN ]]; then
    # It's just not possible to run cygwin32 binaries from within cygwin64.

    # Build as if we are cross-compiling, using the capnp we installed to $STAGING.
    doit ./configure --prefix="$STAGING" --disable-shared --host=i686-pc-cygwin \
        --with-external-capnp CAPNP=$STAGING/bin/capnp
    doit make -j6 capnp-test.exe

    # Expect a cygwin32 sshd to be listening at localhost port 2222, and use it
    # to run the tests.
    doit scp -P 2222 capnp-test.exe localhost:~/tmp-capnp-test.exe
    doit ssh -p 2222 localhost './tmp-capnp-test.exe && rm tmp-capnp-test.exe'

    doit make distclean

  elif [ "x${CXX:-g++}" != "xg++-4.8" ]; then
    doit ./configure CXX="${CXX:-g++} -m32" --disable-shared
    doit make -j6 check
    doit make distclean
  fi
fi

echo "========================================================================="
echo "Testing c++ uninstall"
echo "========================================================================="

doit ./configure --prefix="$STAGING"
doit make uninstall

echo "========================================================================="
echo "Testing c++ dist"
echo "========================================================================="

doit make distcheck
doit make distclean
rm capnproto-*.tar.gz

echo "========================================================================="
echo "Testing with -fno-rtti and -fno-exceptions"
echo "========================================================================="

doit ./configure --disable-shared CXXFLAGS=-fno-rtti
doit make -j6 check
doit make distclean
doit ./configure --disable-shared CXXFLAGS=-fno-exceptions
doit make -j6 check
doit make distclean
doit ./configure --disable-shared CXXFLAGS="-fno-rtti -fno-exceptions"
doit make -j6 check

doit make maintainer-clean

rm -rf "$STAGING"

