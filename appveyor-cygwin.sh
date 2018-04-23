#! /usr/bin/env bash

cmake -Hc++ -B./build-cygwin -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=debug -DCMAKE_INSTALL_PREFIX=./capnproto-c++-cygwin
cmake --build ./build-cygwin --config debug --target install -- -j2
