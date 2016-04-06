#! /usr/bin/env bash

set -euo pipefail

export PATH=$PWD/bin:$PWD:$PATH

capnp compile -Isrc --no-standard-import --src-prefix=src -oc++:src \
    src/capnp/c++.capnp src/capnp/schema.capnp \
    src/capnp/compiler/lexer.capnp src/capnp/compiler/grammar.capnp \
    src/capnp/rpc.capnp src/capnp/rpc-twoparty.capnp src/capnp/persistent.capnp \
    src/capnp/compat/json.capnp
