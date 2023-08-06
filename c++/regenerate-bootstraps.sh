#! /usr/bin/env bash

#TODO (later): Try to convert this to a bazel run command, although bazel's sandboxing might make
# this difficult. For now, running `bazel-bin/src/capnp/capnp_tool compile -Isrc
# --no-standard-import --src-prefix=src -obazel-bin/src/capnp/capnpc-c++:src <set of files>`
# achieves the same.

set -euo pipefail

export PATH=$PWD/bin:$PWD:$PATH

capnp compile -Isrc --no-standard-import --src-prefix=src -oc++:src \
    src/capnp/c++.capnp src/capnp/schema.capnp src/capnp/stream.capnp \
    src/capnp/compiler/lexer.capnp src/capnp/compiler/grammar.capnp \
    src/capnp/rpc.capnp src/capnp/rpc-twoparty.capnp src/capnp/persistent.capnp \
    src/capnp/compat/json.capnp
