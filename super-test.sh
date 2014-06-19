#! /usr/bin/env bash

set -euo pipefail

doit() {
  echo "@@@@ $@"
  "$@"
}

QUICK=

while [ $# -gt 0 ]; do
  case "$1" in
    test )
      ;;  # nothing
    quick )
      QUICK=quick
      ;;
    caffeinate )
      # Re-run preventing sleep.
      shift
      exec caffeinate $0 $@
      ;;
    tmpdir )
      # Clone to a temp directory.
      if [ "$#" -lt 2 ]; then
        echo "usage: $0 tmpdir NAME [COMMAND]" >&2
        exit 1
      fi
      DIR=/tmp/$2
      shift 2
      if [ -e $DIR ]; then
        if [ "${DIR/*..*}" = "" ]; then
          echo "NO DO NOT PUT .. IN THERE IT'S GOING TO GO IN /tmp AND I'M GONNA DELETE IT" >&2
          exit 1
        fi
        if [ ! -e "$DIR/super-test.sh" ]; then
          echo "$DIR exists and it doesn't look like one of mine." >&2
          exit 1
        fi
        # make distcheck leaves non-writable files when it fails, so we need to chmod to be safe.
        chmod -R +w $DIR
        rm -rf $DIR
      fi
      git clone . $DIR
      if [ -e c++/gtest ]; then
        cp -r c++/gtest $DIR/c++/gtest
      fi
      cd $DIR
      exec ./super-test.sh $@
      ;;
    remote )
      if [ "$#" -lt 2 ]; then
        echo "usage: $0 remote HOST [COMMAND]" >&2
        exit 1
      fi
      HOST=$2
      shift 2
      echo "========================================================================="
      echo "Pushing code to $HOST..."
      echo "========================================================================="
      BRANCH=$(git rev-parse --abbrev-ref HEAD)
      ssh $HOST 'rm -rf tmp-test-capnp && mkdir tmp-test-capnp && git init tmp-test-capnp'
      git push ssh://$HOST/~/tmp-test-capnp "$BRANCH:test"
      ssh $HOST "cd tmp-test-capnp && git checkout test"
      scp -qr c++/gtest $HOST:~/tmp-test-capnp/c++/gtest
      exec ssh $HOST "cd tmp-test-capnp && ./super-test.sh $@ && cd .. && rm -rf tmp-test-capnp"
      ;;
    clang )
      export CXX=clang++
      ;;
    gcc-4.8 )
      export CXX=g++-4.8
      ;;
    gcc-4.7 )
      export CXX=g++-4.7
      ;;
    kenton )
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
      $0 test $QUICK
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
      $0 remote beat caffeinate $QUICK
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
      $0 remote Kenton@flashman $QUICK
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
      $0 gcc-4.8 $QUICK
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
      $0 clang $QUICK
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
      ;;
    clean )
      rm -rf tmp-staging
      cd c++
      if [ -e Makefile ]; then
        doit make maintainer-clean
      fi
      rm -f capnproto-*.tar.gz samples/addressbook samples/addressbook.capnp.c++ \
            samples/addressbook.capnp.h
      exit 0
      ;;
    help )
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
      ;;
    * )
      echo "unknown command: $1" >&2
      echo "try: $0 help" >&2
      exit 1
      ;;
  esac
  shift
done

# Build optimized builds because they catch more problems, but also enable debugging macros.
# Enable lots of warnings and make sure the build breaks if they fire.  Disable strict-aliasing
# because GCC warns about code that I know is OK.  Disable sign-compare because I've fixed more
# sign-compare warnings than probably all other warnings combined and I've never seen it flag a
# real problem.
export CXXFLAGS="-O2 -DDEBUG -Wall -Werror -Wno-strict-aliasing -Wno-sign-compare"

STAGING=$PWD/tmp-staging

rm -rf "$STAGING"
mkdir "$STAGING"
mkdir "$STAGING/bin"
mkdir "$STAGING/lib"
export PATH=$STAGING/bin:$PATH
export LD_LIBRARY_PATH=$STAGING/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
export PKG_CONFIG_PATH=$STAGING/lib/pkgconfig

if [ "$QUICK" = quick ]; then
  echo "************************** QUICK TEST ***********************************"
fi

echo "========================================================================="
echo "Building c++"
echo "========================================================================="

# Apple now aliases gcc to clang, so probe to find out what compiler we're really using.
if (${CXX:-g++} -dM -E -x c++ /dev/null 2>&1 | grep -q '__clang__'); then
  IS_CLANG=yes
else
  IS_CLANG=no
fi

if [ $IS_CLANG = yes ]; then
  # There's an unused private field in gtest.
  export CXXFLAGS="$CXXFLAGS -Wno-unused-private-field"
else
  # GCC 4.8 emits a weird uninitialized warning in kj/parse/char-test, deep in one of the parser
  # combinators.  It could be a real bug but there is just not enough information to figure out
  # where the problem is coming from, because GCC does not provide any description of the inlining
  # that has occurred.  Since I have not observed any actual problem (tests pass, etc.), I'm
  # muting it for now.
  CXXFLAGS="$CXXFLAGS -Wno-maybe-uninitialized"
fi

cd c++
doit ./setup-autotools.sh | tr = -
doit autoreconf -i
doit ./configure --prefix="$STAGING"
doit make -j6 check

if [ $IS_CLANG = no ]; then
  # Verify that generated code compiles with pedantic warnings.  Make sure to treat capnp headers
  # as system headers so warnings in them are ignored.
  doit ${CXX:-g++} -isystem src -std=c++11 -fno-permissive -pedantic -Wall -Wextra -Werror \
      -c src/capnp/test.capnp.c++ -o /dev/null
fi

echo "========================================================================="
echo "Testing c++ install"
echo "========================================================================="

doit make install

test "x$(which capnp)" = "x$STAGING/bin/capnp"
test "x$(which capnpc-c++)" = "x$STAGING/bin/capnpc-c++"

cd samples

doit capnp compile -oc++ addressbook.capnp -I"$STAGING"/include --no-standard-import
doit ${CXX:-g++} -std=c++11 addressbook.c++ addressbook.capnp.c++ -o addressbook \
    $CXXFLAGS $(pkg-config --cflags --libs capnp)
echo "@@@@ ./addressbook (in various configurations)"
./addressbook write | ./addressbook read
./addressbook dwrite | ./addressbook dread
rm addressbook addressbook.capnp.c++ addressbook.capnp.h

doit capnp compile -oc++ calculator.capnp -I"$STAGING"/include --no-standard-import
doit ${CXX:-g++} -std=c++11 calculator-client.c++ calculator.capnp.c++ -o calculator-client \
    $CXXFLAGS $(pkg-config --cflags --libs capnp-rpc)
doit ${CXX:-g++} -std=c++11 calculator-server.c++ calculator.capnp.c++ -o calculator-server \
    $CXXFLAGS $(pkg-config --cflags --libs capnp-rpc)
rm -f /tmp/capnp-calculator-example-$$
./calculator-server unix:/tmp/capnp-calculator-example-$$ &
sleep 0.1
./calculator-client unix:/tmp/capnp-calculator-example-$$
kill %+
wait %+ || true
rm calculator-client calculator-server calculator.capnp.c++ calculator.capnp.h /tmp/capnp-calculator-example-$$

cd ..

if [ "$QUICK" = quick ]; then
  doit make maintainer-clean
  rm -rf "$STAGING"
  exit 0
fi

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

doit ./configure --disable-shared CXXFLAGS="$CXXFLAGS -fno-rtti"
doit make -j6 check
doit make distclean
doit ./configure --disable-shared CXXFLAGS="$CXXFLAGS -fno-exceptions"
doit make -j6 check
doit make distclean
doit ./configure --disable-shared CXXFLAGS="$CXXFLAGS -fno-rtti -fno-exceptions"
doit make -j6 check

# Valgrind is currently "experimental and mostly broken" on OSX and fails to run the full test
# suite, but I have it installed because it did manage to help me track down a bug or two.  Anyway,
# skip it on OSX for now.
if [ "x`uname`" != xDarwin ] && which valgrind > /dev/null; then
  doit make distclean

  echo "========================================================================="
  echo "Testing with valgrind"
  echo "========================================================================="

  doit ./configure --disable-shared CXXFLAGS="-g"
  doit make -j6
  doit make -j6 capnp-test
  doit valgrind --leak-check=full --track-fds=yes --error-exitcode=1 ./capnp-test
fi

doit make maintainer-clean

rm -rf "$STAGING"
