#! /usr/bin/env bash

set -euo pipefail

if ! uname | grep -iq Linux; then
  echo "Sorry, Ekam only works on Linux right now." >&2
  exit 1
fi

echo -n "Looking for compiler... "
if [ "x${CXX:-}" == "x" ]; then
  if ! (g++ --version | grep -q ' 4[.][789][.]'); then
    if which g++-4.7 > /dev/null; then
      CXX=g++-4.7
    elif which g++-4.8 > /dev/null; then
      CXX=g++-4.8
    else
      echo "none"
      echo "Please install G++ 4.7 or better.  Or, set the environment variable CXX " >&2
      echo "to a compiler that you think will work." >&2
      exit 1
    fi
  else
    CXX=g++
  fi
fi

echo "$CXX"
export CXX

if [ ! -e .ekam ]; then
  echo "================================================================================"
  echo "Fetching Ekam and Protobuf code..."
  echo "================================================================================"
  hg clone https://code.google.com/p/kentons-code/ .ekam

  # You don't want these.
  rm -rf .ekam/src/modc .ekam/src/evlan
fi

if [ ! -e .ekam/src/protobuf ]; then
  echo "================================================================================"
  echo "Fetching Protobuf code..."
  echo "================================================================================"
  svn checkout http://protobuf.googlecode.com/svn/tags/2.5.0/ .ekam/src/protobuf
fi

if [ ! -e .ekam/src/protobuf/src/config.h ]; then
  echo "================================================================================"
  echo "Configuring Protobuf..."
  echo "================================================================================"
  pushd .ekam/src/protobuf > /dev/null
  ./autogen.sh
  ./configure
  cp config.h src
  make maintainer-clean
  popd
fi

if ! which ekam > /dev/null; then
  if [ ! -e .ekam/bin/ekam ]; then
    echo "================================================================================"
    echo "Bootstrapping Ekam..."
    echo "================================================================================"
    pushd .ekam > /dev/null
    ./bootstrap.sh
    popd
  fi
else
  echo "================================================================================"
  echo "Using already-installed ekam binary: $(which ekam)"
  echo "================================================================================"
fi

if [ ! -e src/base ]; then
  ln -s ../.ekam/src/base src/base
fi
if [ ! -e src/os ]; then
  ln -s ../.ekam/src/os src/os
fi
if [ ! -e src/ekam ]; then
  ln -s ../.ekam/src/ekam src/ekam
fi
if [ ! -e src/protobuf ]; then
  ln -s ../.ekam/src/protobuf src/protobuf
fi

echo "================================================================================"
echo "All done..."
echo "================================================================================"
echo "Try:"
echo "  make -f Makefile.ekam once"
echo "  make -f Makefile.ekam continuous"
echo "  make -f Makefile.ekam continuous-opt"
