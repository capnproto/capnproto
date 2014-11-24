@echo off

rem You'll need to build capnp.exe and capnpc-c++.exe using MinGW.

capnp compile -oc++ -I../src --src-prefix=../src ../src/capnp/test.capnp ../src/capnp/test-import.capnp ../src/capnp/test-import2.capnp
