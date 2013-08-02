#! /bin/bash
#
# Quick script that compiles and runs the samples, then cleans up.
# Used for release testing.

set -exuo pipefail

capnpc -oc++ addressbook.capnp
c++ -std=c++11 -Wall addressbook.c++ addressbook.capnp.c++ -lcapnp -pthread -o addressbook
./addressbook write | ./addressbook read
./addressbook dwrite | ./addressbook dread
rm addressbook addressbook.capnp.c++ addressbook.capnp.h

