#! /usr/bin/env bash

set -euo pipefail

doit() {
  echo "@@@@ $@"
  "$@"
}

function test_samples() {
  echo "@@@@ ./addressbook (in various configurations)"
  ./addressbook write | ./addressbook read
  ./addressbook dwrite | ./addressbook dread
  rm -f /tmp/capnp-calculator-example-$$
  ./calculator-server unix:/tmp/capnp-calculator-example-$$ &
  local SERVER_PID=$!
  sleep 1
  ./calculator-client unix:/tmp/capnp-calculator-example-$$
  # `kill %./calculator-server` doesn't seem to work on recent Cygwins, but we can kill by PID.
  kill -9 $SERVER_PID
  # This `fg` will fail if bash happens to have already noticed the quit and reaped the process
  # before `fg` is invoked, so in that case we just proceed.
  fg %./calculator-server || true
  rm -f /tmp/capnp-calculator-example-$$
}

QUICK=
CPP_FEATURES=
WERROR="-Werror"
EXTRA_LIBS=

PARALLEL=$(nproc 2>/dev/null || echo 1)

# Have automake dump test failure to stdout. Important for CI.
export VERBOSE=true

while [ $# -gt 0 ]; do
  case "$1" in
    -j* )
      PARALLEL=${1#-j}
      ;;
    test )
      ;;  # nothing
    quick )
      QUICK=quick
      ;;
    cpp-features )
      if [ "$#" -lt 2 ] || [ -n "$CPP_FEATURES" ]; then
        echo "usage: $0 cpp-features CPP_DEFINES" >&2
        echo "e.g. $0 cpp-features '-DSOME_VAR=5 -DSOME_OTHER_VAR=6'" >&2
        if [ -n "$CPP_FEATURES" ]; then
          echo "cpp-features provided multiple times" >&2
        fi
        exit 1
      fi
      CPP_FEATURES="$2"
      shift
      ;;
    nowerror )
      WERROR="-Wno-error"
      ;;
    extra-libs )
      if [ "$#" -lt 2 ] || [ -n "$EXTRA_LIBS" ]; then
        echo "usage: $0 extra-libs EXTRA_LIBS" >&2
        echo "e.g. $0 extra-libs '-lrt'" >&2
        if [ -n "$EXTRA_LIBS" ]; then
          echo "extra-libs provided multiple times" >&2
        fi
        exit 1
      fi
      EXTRA_LIBS="$2"
      shift
      ;;
    caffeinate )
      # Re-run preventing sleep.
      shift
      exec caffeinate -ims $0 $@
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
      cd $DIR
      exec ./super-test.sh "$@"
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
      ssh $HOST '(chmod -fR +w tmp-test-capnp || true) && rm -rf tmp-test-capnp && mkdir tmp-test-capnp && git init tmp-test-capnp'
      git push ssh://$HOST/~/tmp-test-capnp "$BRANCH:test"
      ssh $HOST "cd tmp-test-capnp && git checkout test"
      exec ssh $HOST "cd tmp-test-capnp && ./super-test.sh $@ && cd .. && rm -rf tmp-test-capnp"
      ;;
    compiler )
      if [ "$#" -lt 2 ]; then
        echo "usage: $0 compiler CXX_NAME" >&2
        exit 1
      fi
      export CXX="$2"
      shift
      ;;
    clang* )
      # Need to set CC as well for configure to handle -fcoroutines-ts.
      export CC=clang${1#clang}
      export CXX=clang++${1#clang}
      if [ "$1" != "clang-5.0" ]; then
        export LIB_FUZZING_ENGINE=-fsanitize=fuzzer
      fi
      ;;
    gcc* )
      export CXX=g++${1#gcc}
      ;;
    g++* )
      export CXX=$1
      ;;
    mingw )
      if [ "$#" -ne 2 ]; then
        echo "usage: $0 mingw CROSS_HOST" >&2
        exit 1
      fi
      CROSS_HOST=$2

      cd c++
      test -e configure || doit autoreconf -i
      test ! -e Makefile || (echo "ERROR: Directory unclean!" >&2 && false)

      export WINEPATH='Z:\usr\'"$CROSS_HOST"'\lib;Z:\usr\lib\gcc\'"$CROSS_HOST"'\6.3-win32;Z:'"$PWD"'\.libs'

      doit ./configure --host="$CROSS_HOST" --disable-shared CXXFLAGS="-static-libgcc -static-libstdc++ $CPP_FEATURES" LIBS="$EXTRA_LIBS"

      doit make -j$PARALLEL check
      doit make distclean
      rm -f *-mingw.exe
      exit 0
      ;;
    android )
      # To install Android SDK:
      # - Download command-line tools: https://developer.android.com/studio/index.html#command-tools
      # - Run $SDK_HOME/tools/bin/sdkmanager platform-tools 'platforms;android-25' 'system-images;android-25;google_apis;armeabi-v7a' emulator 'build-tools;25.0.2' ndk-bundle
      # - Run $SDK_HOME/tools/bin/avdmanager create avd -n capnp -k 'system-images;android-25;google_apis;armeabi-v7a' -b google_apis/armeabi-v7a
      if [ "$#" -ne 4 ]; then
        echo "usage: $0 android SDK_HOME CROSS_HOST COMPILER_PREFIX" >&2
        echo
        echo "SDK_HOME: Location where android-sdk is installed." >&2
        echo "CROSS_HOST: E.g. arm-linux-androideabi" >&2
        echo "COMPILER_PREFIX: E.g. armv7a-linux-androideabi24" >&2
        exit 1
      fi
      SDK_HOME=$2
      CROSS_HOST=$3
      COMPILER_PREFIX=$4

      cd c++
      test -e configure || doit autoreconf -i
      test ! -e Makefile || (echo "ERROR: Directory unclean!" >&2 && false)
      doit ./configure --disable-shared
      doit make -j$PARALLEL capnp capnpc-c++

      cp capnp capnp-host
      cp capnpc-c++ capnpc-c++-host

      export PATH="$SDK_HOME/ndk-bundle/toolchains/llvm/prebuilt/linux-x86_64/bin:$PATH"
      doit make distclean
      doit ./configure --host="$CROSS_HOST" CC="$COMPILER_PREFIX-clang" CXX="$COMPILER_PREFIX-clang++" --with-external-capnp --disable-shared CXXFLAGS="-fPIE $CPP_FEATURES" LDFLAGS='-pie' LIBS="-static-libstdc++ -static-libgcc -ldl $EXTRA_LIBS" CAPNP=./capnp-host CAPNPC_CXX=./capnpc-c++-host

      doit make -j$PARALLEL
      doit make -j$PARALLEL capnp-test

      echo "Starting emulator..."
      trap 'kill $(jobs -p)' EXIT
      # TODO(someday): Speed up with KVM? Then maybe we won't have to skip fuzz tests?
      $SDK_HOME/emulator/emulator -avd capnp -no-window &
      $SDK_HOME/platform-tools/adb 'wait-for-device'
      echo "Waiting for localhost to be resolvable..."
      doit $SDK_HOME/platform-tools/adb shell 'while ! ping -c 1 localhost > /dev/null 2>&1; do sleep 1; done'
      # TODO(cleanup): With 'adb shell' I find I cannot put files anywhere, so I'm using 'su' a
      #   lot here. There is probably a better way.
      doit $SDK_HOME/platform-tools/adb shell 'su 0 tee /data/capnp-test > /dev/null' < capnp-test
      doit $SDK_HOME/platform-tools/adb shell 'su 0 chmod a+rx /data/capnp-test'
      doit $SDK_HOME/platform-tools/adb shell 'cd /data && CAPNP_SKIP_FUZZ_TEST=1 su 0 /data/capnp-test && echo ANDROID_""TESTS_PASSED' | tee android-test.log
      grep -q ANDROID_TESTS_PASSED android-test.log

      doit make distclean
      rm -f capnp-host capnpc-c++-host
      exit 0
      ;;
    cmake )
      cd c++
      rm -rf cmake-build
      mkdir cmake-build
      cd cmake-build
      doit cmake -G "Unix Makefiles" ..
      doit make -j$PARALLEL check
      exit 0
      ;;
    cmake-package )
      # Test that a particular configuration of Cap'n Proto can be discovered and configured against
      # by a CMake project using the find_package() command. This is currently implemented by
      # building the samples against the desired configuration.
      #
      # Takes one argument, the build configuration, which must be one of:
      #
      #   autotools-shared
      #   autotools-static
      #   cmake-shared
      #   cmake-static

      if [ "$#" -ne 2 ]; then
        echo "usage: $0 cmake-package CONFIGURATION" >&2
        echo "  where CONFIGURATION is one of {autotools,cmake}-{static,shared}" >&2
        exit 1
      fi

      CONFIGURATION=$2
      WORKSPACE=$(pwd)/cmake-package/$CONFIGURATION
      SOURCE_DIR=$(pwd)/c++

      rm -rf $WORKSPACE
      mkdir -p $WORKSPACE/{build,build-samples,inst}

      # Configure
      cd $WORKSPACE/build
      case "$CONFIGURATION" in
        autotools-shared )
          autoreconf -i $SOURCE_DIR
          doit $SOURCE_DIR/configure --prefix="$WORKSPACE/inst" --disable-static
          ;;
        autotools-static )
          autoreconf -i $SOURCE_DIR
          doit $SOURCE_DIR/configure --prefix="$WORKSPACE/inst" --disable-shared
          ;;
        cmake-shared )
          doit cmake $SOURCE_DIR -G "Unix Makefiles" -DCMAKE_INSTALL_PREFIX="$WORKSPACE/inst" \
              -DBUILD_TESTING=OFF -DBUILD_SHARED_LIBS=ON
          # The CMake build does not currently set the rpath of the capnp compiler tools.
          export LD_LIBRARY_PATH="$WORKSPACE/inst/lib"
          ;;
        cmake-static )
          doit cmake $SOURCE_DIR -G "Unix Makefiles" -DCMAKE_INSTALL_PREFIX="$WORKSPACE/inst" \
              -DBUILD_TESTING=OFF -DBUILD_SHARED_LIBS=OFF
          ;;
        * )
          echo "Unrecognized cmake-package CONFIGURATION argument, must be {autotools,cmake}-{static,shared}" >&2
          exit 1
          ;;
      esac

      # Build and install
      doit make -j$PARALLEL install

      # Configure, build, and execute the samples.
      cd $WORKSPACE/build-samples
      doit cmake $SOURCE_DIR/samples -G "Unix Makefiles" -DCMAKE_PREFIX_PATH="$WORKSPACE/inst" \
          -DCAPNPC_FLAGS=--no-standard-import -DCAPNPC_IMPORT_DIRS="$WORKSPACE/inst/include"
      doit make -j$PARALLEL

      test_samples

      echo "========================================================================="
      echo "Cap'n Proto ($CONFIGURATION) installs a working CMake config package."
      echo "========================================================================="

      exit 0
      ;;
    exotic )
      echo "========================================================================="
      echo "MinGW 64-bit"
      echo "========================================================================="
      "$0" mingw x86_64-w64-mingw32
      echo "========================================================================="
      echo "MinGW 32-bit"
      echo "========================================================================="
      "$0" mingw i686-w64-mingw32
      echo "========================================================================="
      echo "Android"
      echo "========================================================================="
      "$0" android /home/kenton/android-sdk-linux /home/kenton/android-24 arm-linux-androideabi
      echo "========================================================================="
      echo "CMake"
      echo "========================================================================="
      "$0" cmake
      echo "========================================================================="
      echo "CMake config packages"
      echo "========================================================================="
      "$0" cmake-package autotools-shared
      "$0" cmake-package autotools-static
      "$0" cmake-package cmake-shared
      "$0" cmake-package cmake-static
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
      echo "  gcc-4.7       Runs tests using gcc-4.7."
      echo "  gcc-4.8       Runs tests using gcc-4.8."
      echo "  gcc-4.9       Runs tests using gcc-4.9."
      echo "  remote HOST   Runs tests on HOST via SSH."
      echo "  mingw         Cross-compiles to MinGW and runs tests using WINE."
      echo "  android       Cross-compiles to Android and runs tests using emulator."
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
# real problem. Disable unused parameters because it's stupidly noisy and never a real problem.
# Enable expensive release-gating tests.
export CXXFLAGS="-O2 -DDEBUG -Wall -Wextra ${WERROR} -Wno-strict-aliasing -Wno-sign-compare -Wno-unused-parameter -DCAPNP_EXPENSIVE_TESTS=1 ${CPP_FEATURES}"
export LIBS="$EXTRA_LIBS"

if [ "${CXX:-}" != "g++-5" ]; then
  # This warning flag is missing on g++-5 but available on all other GCC/Clang versions we target
  # in CI.
  export CXXFLAGS="$CXXFLAGS -Wimplicit-fallthrough"
fi

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
#
# NOTE: You might be tempted to use `grep -q` here instead of sending output to /dev/null. However,
#   we cannot, because `grep -q` exits immediately upon seeing a match. If it exits too soon, the
#   first stage of the pipeline gets killed, and the whole expression is considered to have failed
#   since we are running bash with the `pipefail` option enabled.
# FUN STORY: We used to use grep -q. One day, we found that Clang 9 when running under GitHub
#   Actions was detected as *not* Clang. But if we ran it twice, it would succeed on the second
#   try. It turns out that under previous versions of Clang, the `__clang__` define was pretty
#   close to the end of the list, so it always managed to write the whole list before `grep -q`
#   exited. But under Clang 9, there's a bunch more defines after this one, giving more time for
#   `grep -q` to exit and break everything. But if the compiler had executed once recently then
#   the second run would go faster due to caching (I guess) and manage to get all the data out
#   to the buffer in time.
if (${CXX:-g++} -dM -E -x c++ /dev/null 2>&1 | grep '__clang__' > /dev/null); then
  IS_CLANG=yes
  DISABLE_OPTIMIZATION_IF_GCC=
else
  IS_CLANG=no
  DISABLE_OPTIMIZATION_IF_GCC=-O0
fi

if [ $IS_CLANG = yes ]; then
  # Don't fail out on this ridiculous "argument unused during compilation" warning.
  export CXXFLAGS="$CXXFLAGS -Wno-error=unused-command-line-argument"

  # Enable coroutines if supported.
  if [ "${CXX#*-}" -ge 14 ] 2>/dev/null; then
    # Somewhere between version 10 and 14, Clang started supporting coroutines as a C++20 feature,
    # and started issuing deprecating warnings for -fcoroutines-ts. (I'm not sure which version it
    # was exactly since our CI jumped from 10 to 14, so I'm somewhat arbitrarily choosing 14 as the
    # cutoff.)
    export CXXFLAGS="$CXXFLAGS -std=c++20 -stdlib=libc++"
    export LDFLAGS="-stdlib=libc++"

    # TODO(someday): On Ubuntu 22.04, clang-14 with -stdlib=libc++ fails to link with libfuzzer,
    #   which looks like it might itself be linked against libstdc++? Need to investigate.
    unset LIB_FUZZING_ENGINE
  elif [ "${CXX#*-}" -ge 10 ] 2>/dev/null; then
    # At the moment, only our clang-10 CI run seems to like -fcoroutines-ts. Earlier versions seem
    # to have a library misconfiguration causing ./configure to result in the following error:
    #   conftest.cpp:12:12: fatal error: 'initializer_list' file not found
    #   #include <initializer_list>
    # Let's use any clang version >= 10 so that if we move to a newer version, we'll get additional
    # coverage by default.
    export CXXFLAGS="$CXXFLAGS -std=gnu++17 -stdlib=libc++ -fcoroutines-ts"
    export LDFLAGS="-fcoroutines-ts -stdlib=libc++"
  fi
else
  # GCC emits uninitialized warnings all over and they seem bogus. We use valgrind to test for
  # uninitialized memory usage later on. GCC 4 also emits strange bogus warnings with
  # -Wstrict-overflow, so we disable it.
  CXXFLAGS="$CXXFLAGS -Wno-maybe-uninitialized -Wno-strict-overflow"

  # TODO(someday): Enable coroutines in g++ if supported.
fi

cd c++
doit autoreconf -i
doit ./configure --prefix="$STAGING" || (cat config.log && exit 1)
doit make -j$PARALLEL check

if [ $IS_CLANG = no ]; then
  # Verify that generated code compiles with pedantic warnings.  Make sure to treat capnp headers
  # as system headers so warnings in them are ignored.
  doit ${CXX:-g++} -isystem src -std=c++14 -fno-permissive -pedantic -Wall -Wextra -Werror \
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
doit ${CXX:-g++} -std=c++14 addressbook.c++ addressbook.capnp.c++ -o addressbook \
    $CXXFLAGS $(pkg-config --cflags --libs capnp)

doit capnp compile -oc++ calculator.capnp -I"$STAGING"/include --no-standard-import
doit ${CXX:-g++} -std=c++14 calculator-client.c++ calculator.capnp.c++ -o calculator-client \
    $CXXFLAGS $(pkg-config --cflags --libs capnp-rpc)
doit ${CXX:-g++} -std=c++14 calculator-server.c++ calculator.capnp.c++ -o calculator-server \
    $CXXFLAGS $(pkg-config --cflags --libs capnp-rpc)

test_samples
rm addressbook addressbook.capnp.c++ addressbook.capnp.h
rm calculator-client calculator-server calculator.capnp.c++ calculator.capnp.h

rm -rf cmake-build
mkdir cmake-build
cd cmake-build

doit cmake .. -G "Unix Makefiles" -DCMAKE_PREFIX_PATH="$STAGING" \
    -DCAPNPC_FLAGS=--no-standard-import -DCAPNPC_IMPORT_DIRS="$STAGING/include"
doit make -j$PARALLEL

test_samples
cd ../..
rm -rf samples/cmake-build

if [ "$QUICK" = quick ]; then
  doit make maintainer-clean
  rm -rf "$STAGING"
  exit 0
fi

echo "========================================================================="
echo "Testing --with-external-capnp and --disable-reflection"
echo "========================================================================="

doit make distclean
doit ./configure --prefix="$STAGING" --disable-shared --disable-reflection \
    --with-external-capnp CAPNP=$STAGING/bin/capnp
doit make -j$PARALLEL check
doit make distclean

# Test 32-bit build now while we have $STAGING available for cross-compiling.
#
# Cygwin64 can cross-compile to Cygwin32 but can't actually run the cross-compiled binaries. Let's
# just skip this test on Cygwin since it's so slow and honestly no one cares.
#
# MacOS apparently no longer distributes 32-bit standard libraries. OK fine let's restrict this to
# Linux.
if [ "x`uname -m`" = "xx86_64" ] && [ "x`uname`" = xLinux ]; then
  echo "========================================================================="
  echo "Testing 32-bit build"
  echo "========================================================================="

  doit ./configure CXX="${CXX:-g++} -m32" CXXFLAGS="$CXXFLAGS ${ADDL_M32_FLAGS:-}" --disable-shared
  doit make -j$PARALLEL check
  doit make distclean
fi

echo "========================================================================="
echo "Testing c++ uninstall"
echo "========================================================================="

doit ./configure --prefix="$STAGING"
doit make uninstall

echo "========================================================================="
echo "Testing c++ dist"
echo "========================================================================="

doit make -j$PARALLEL distcheck
doit make distclean
rm capnproto-*.tar.gz

if [ "x`uname`" = xLinux ]; then
  echo "========================================================================="
  echo "Testing generic Unix (no Linux-specific features)"
  echo "========================================================================="

  doit ./configure --disable-shared CXXFLAGS="$CXXFLAGS -DKJ_USE_FUTEX=0 -DKJ_USE_EPOLL=0"
  doit make -j$PARALLEL check
  doit make distclean
fi

echo "========================================================================="
echo "Testing with -fno-rtti and -fno-exceptions"
echo "========================================================================="

# GCC miscompiles capnpc-c++ when -fno-exceptions and -O2 are specified together. The
# miscompilation happens in one specific inlined call site of Array::dispose(), but this method
# is inlined in hundreds of other places without issue, so I have no idea how to narrow down the
# bug. Clang works fine. So, for now, we disable optimizations on GCC for -fno-exceptions tests.

doit ./configure --disable-shared CXXFLAGS="$CXXFLAGS -fno-rtti -fno-exceptions $DISABLE_OPTIMIZATION_IF_GCC"
doit make -j$PARALLEL check

if [ "x`uname`" = xLinux ]; then
  doit make distclean

  echo "========================================================================="
  echo "Testing with valgrind"
  echo "========================================================================="

  doit ./configure --disable-shared CXXFLAGS="-g $CPP_FEATURES"
  doit make -j$PARALLEL
  doit make -j$PARALLEL capnp-test
  # Running the fuzz tests under Valgrind is a great thing to do -- but it takes
  # some 40 minutes. So, it needs to be done as a separate step of the release
  # process, perhaps along with the AFL tests.
  CAPNP_SKIP_FUZZ_TEST=1 doit valgrind --leak-check=full --track-fds=yes --error-exitcode=1 --child-silent-after-fork=yes --sim-hints=lax-ioctls --suppressions=valgrind.supp ./capnp-test
fi

doit make maintainer-clean

rm -rf "$STAGING"
