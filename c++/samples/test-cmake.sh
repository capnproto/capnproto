#! /usr/bin/env bash
#
# Quick script that compiles and runs the samples using CMake, then cleans up.
# Used for release testing.

mkdir build
cd build
cmake ..
make
make test
cd ..
rm -fr build
