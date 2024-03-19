#! /usr/bin/env bash

set -euo pipefail

bazel build src/capnp/capnp_tool src/capnp/capnpc-c++

bazel-bin/src/capnp/capnp_tool compile -Isrc --no-standard-import --src-prefix=src -obazel-bin/src/capnp/capnpc-c++:src \
    src/capnp/c++.capnp src/capnp/schema.capnp src/capnp/stream.capnp \
    src/capnp/compiler/lexer.capnp src/capnp/compiler/grammar.capnp \
    src/capnp/rpc.capnp src/capnp/rpc-twoparty.capnp src/capnp/persistent.capnp \
    src/capnp/compat/json.capnp
