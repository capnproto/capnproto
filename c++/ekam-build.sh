#! /bin/bash
#
# This file builds Cap'n Proto using Ekam.

set -euo pipefail

NPROC=$(nproc)

if [ ! -e deps/ekam ]; then
  mkdir -p deps
  git clone https://github.com/capnproto/ekam.git deps/ekam
fi

if [ ! -e deps/ekam/deps/capnproto ]; then
  mkdir -p deps/ekam/deps
  ln -s ../../../.. deps/ekam/deps/capnproto
fi

if [ ! -e deps/ekam/ekam ]; then
  (cd deps/ekam && make -j$NPROC)
fi

OPT_CXXFLAGS=
EXTRA_LIBS=
EKAM_FLAGS=

while [ $# -gt 0 ]; do
  case $1 in
    dbg | debug )
      OPT_CXXFLAGS="-g -DCAPNP_DEBUG_TYPES "
      ;;
    opt | release )
      OPT_CXXFLAGS="-DNDEBUG -O2 -g"
      ;;
    prof | profile )
      OPT_CXXFLAGS="-DNDEBUG -O2 -g"
      EXTRA_LIBS="$EXTRA_LIBS -lprofiler"
      ;;
    tcmalloc )
      EXTRA_LIBS="$EXTRA_LIBS -ltcmalloc"
      ;;
    continuous )
      EKAM_FLAGS="-c -n :41315"
      ;;
    * )
      echo "Unknown option: $1" >&2
      exit 1
      ;;
  esac
  shift
done

CLANG_CXXFLAGS="-std=c++20 -stdlib=libc++ -pthread -Wall -Wextra -Werror -Wno-strict-aliasing -Wno-sign-compare -Wno-unused-parameter -Wimplicit-fallthrough -Wno-error=unused-command-line-argument -Wno-missing-field-initializers -DKJ_HEADER_WARNINGS -DCAPNP_HEADER_WARNINGS -DKJ_HAS_OPENSSL -DKJ_HAS_LIBDL -DKJ_HAS_ZLIB -DKJ_BENCHMARK_MALLOC"

export CXX=${CXX:-clang++}
export CC=${CC:-clang}
export LIBS="-lz -ldl -lcrypto -lssl -stdlib=libc++ $EXTRA_LIBS -pthread"
export CXXFLAGS=${CXXFLAGS:-$OPT_CXXFLAGS $CLANG_CXXFLAGS}

# TODO(someday): Get the protobuf benchmarks working. For now these settings will prevent build
#   errors in the benchmarks directory. Note that it's tricky to link against an installed copy
#   of libprotobuf because we have to use compatible C++ standard libraries. We either need to
#   build libprotobuf from source using libc++, or we need to switch back to libstdc++ when
#   enabling libprotobuf. Arguably building from source would be more fair so we can match compiler
#   flags for performance comparison purposes, but we'll have to see if ekam is still able to build
#   libprotobuf these days...
CXXFLAGS="$CXXFLAGS -DCAPNP_NO_PROTOBUF_BENCHMARK"
export PROTOC=/bin/true

exec deps/ekam/bin/ekam $EKAM_FLAGS -j$NPROC
