#! /bin/bash

set -euo pipefail

doit() {
  echo "@@@@ $@"
  "$@"
}

BRANCH=$(git rev-parse --abbrev-ref HEAD)

case "$BRANCH" in
  master )
    if [ "x${1:-}" != xcandidate ]; then
      echo "Parameter must be 'candidate' when starting from master branch." >&2
      exit 1
    fi

    echo "New major release."

    VERSION=$(grep AC_INIT c++/configure.ac | sed -e 's/^.*],\[\([^]]*\)-dev].*$/\1/g')

    echo "Version: $VERSION.0"

    echo "========================================================================="
    echo "Creating release branch..."
    echo "========================================================================="
    doit git checkout -b release-$VERSION.0
    VERSION_REGEX=${VERSION/./[.]}-dev
    doit sed -i -e "s/$VERSION_REGEX/$VERSION.0-rc1/g" c++/configure.ac compiler/capnproto-compiler.cabal
    doit git commit -a -m "Set release branch version to $VERSION.0-rc1."

    echo "========================================================================="
    echo "Building compiler package..."
    echo "========================================================================="
    cd compiler
    doit cabal configure
    doit cabal sdist
    doit cp dist/capnproto-compiler-$VERSION.0.tar.gz ../capnproto-compiler-$VERSION.0-rc1.tar.gz
    doit cabal clean
    cd ..

    echo "========================================================================="
    echo "Building C++ runtime package..."
    echo "========================================================================="
    cd c++
    doit ./setup-autotools.sh | tr = -
    doit autoreconf -i
    doit ./configure
    doit make dist
    doit mv capnproto-c++-$VERSION.0-rc1.tar.gz ..
    doit make maintainer-clean
    cd ..

    echo "========================================================================="
    echo "Updating version in master branch..."
    echo "========================================================================="
    doit git checkout master
    declare -a VERSION_ARR=(${VERSION/./ })
    NEXT_VERSION=${VERSION_ARR[0]}.$((VERSION_ARR[1] + 1))
    doit sed -i -e "s/$VERSION_REGEX/$NEXT_VERSION-dev/g" c++/configure.ac compiler/capnproto-compiler.cabal
    doit git commit -a -m "Set mainline version to $NEXT_VERSION-dev."

    echo "========================================================================="
    echo "Done"
    echo "========================================================================="
    echo "Ready to release:"
    echo "  capnproto-compiler-$VERSION.0-rc1.tar.gz"
    echo "  capnproto-c++-$VERSION.0-rc1.tar.gz"
    echo "Don't forget to push changes:"
    echo "  git push origin master release-$VERSION.0"
    ;;

  release-* )
    if [ "x${1:-}" == xcandidate ]; then
      echo "New release candidate."
      echo "NOT IMPLEMENTED" >&2
      exit 1
    elif [ "x${1:-}" == xfinal ]; then
      echo "Final release."
      echo "NOT IMPLEMENTED" >&2
      exit 1
    else
      echo "Parameter must be either 'candidate' or 'final'." >&2
      exit 1
    fi
    ;;

  * )
    echo "Not a master or release branch." >&2
    exit 1
esac
