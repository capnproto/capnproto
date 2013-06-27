#! /bin/bash

set -euo pipefail

doit() {
  echo "@@@@ $@"
  "$@"
}

get_version() {
  local VERSION=$(grep AC_INIT c++/configure.ac | sed -e 's/^[^]]*],\[\([^]]*\)].*$/\1/g')
  if [[ ! "$VERSION" =~ $1 ]]; then
    echo "Couldn't parse version: $VERSION" >&2
    exit 1
  fi
  echo "$VERSION"
}

get_release_version() {
  get_version '^[0-9]+[.][0-9]+[.][0-9]+(-rc[0-9]+)?$'
}

update_version() {
  local OLD=$1
  local NEW=$2
  local BRANCH_DESC=$3

  local OLD_REGEX=${OLD//./[.]}
  doit sed -i -e "s/$OLD_REGEX/$NEW/g" c++/configure.ac compiler/capnproto-compiler.cabal
  doit git commit -a -m "Set $BRANCH_DESC version to $NEW."
}

build_packages() {
  local VERSION=$1
  local VERSION_BASE=${VERSION%%-*}

  echo "========================================================================="
  echo "Building compiler package..."
  echo "========================================================================="
  cd compiler
  doit cabal configure
  doit cabal sdist
  doit cp dist/capnproto-compiler-$VERSION_BASE.tar.gz ../capnproto-compiler-$VERSION.tar.gz
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
  doit mv capnproto-c++-$VERSION.tar.gz ..
  doit make maintainer-clean
  cd ..
}

cherry_pick() {
  shift
  if [ $# -gt 0 ]; then
    echo "========================================================================="
    echo "Cherry-picking fixes"
    echo "========================================================================="
    doit git cherry-pick "$@"
  fi
}

done_banner() {
  local VERSION=$1
  local PUSH=$2
  echo "========================================================================="
  echo "Done"
  echo "========================================================================="
  echo "Ready to release:"
  echo "  capnproto-compiler-$VERSION.tar.gz"
  echo "  capnproto-c++-$VERSION.tar.gz"
  echo "Don't forget to push changes:"
  echo "  git push origin $PUSH"
}

BRANCH=$(git rev-parse --abbrev-ref HEAD)

case "${1-}:$BRANCH" in
  # ======================================================================================
  candidate:master )
    echo "New major release."

    if [ $# -gt 1 ]; then
      echo "Cannot cherry-pick when starting from master.  Do it yourself." >&2
      exit 1
    fi

    HEAD_VERSION=$(get_version '^[0-9]+[.][0-9]+-dev$')
    RELEASE_VERSION=${HEAD_VERSION%%-dev}.0

    echo "Version: $RELEASE_VERSION"

    echo "========================================================================="
    echo "Creating release branch..."
    echo "========================================================================="
    doit git checkout -b release-$RELEASE_VERSION

    update_version $HEAD_VERSION $RELEASE_VERSION-rc1 "release branch"

    build_packages $RELEASE_VERSION-rc1

    echo "========================================================================="
    echo "Updating version in master branch..."
    echo "========================================================================="

    doit git checkout master
    declare -a VERSION_ARR=(${RELEASE_VERSION//./ })
    NEXT_VERSION=${VERSION_ARR[0]}.$((VERSION_ARR[1] + 1))

    update_version $HEAD_VERSION $NEXT_VERSION-dev "mainlaine"

    done_banner $RELEASE_VERSION-rc1 "master release-$RELEASE_VERSION"
    ;;

  # ======================================================================================
  candidate:release-* )
    echo "New release candidate."
    OLD_VERSION=$(get_release_version)

    if [[ $OLD_VERSION == *-rc* ]]; then
      # New release candidate for existing release.

      RC=${OLD_VERSION##*-rc}
      BRANCH_VERSION=${OLD_VERSION%%-rc*}
      RC_VERSION=$BRANCH_VERSION-rc$(( RC + 1 ))

      echo "Version: $RC_VERSION"
    else
      # New micro release.

      declare -a VERSION_ARR=(${OLD_VERSION//./ })
      BRANCH_VERSION=${VERSION_ARR[0]}.${VERSION_ARR[1]}.$((VERSION_ARR[2] + 1))

      RC_VERSION=$BRANCH_VERSION-rc1
      echo "Version: $RC_VERSION"

      echo "========================================================================="
      echo "Creating new release branch..."
      echo "========================================================================="

      doit git checkout -b release-$BRANCH_VERSION
    fi

    echo "========================================================================="
    echo "Updating version number to $RC_VERSION..."
    echo "========================================================================="

    update_version $OLD_VERSION $RC_VERSION "release branch"

    cherry_pick "$@"

    build_packages $RC_VERSION

    done_banner $RC_VERSION release-$BRANCH_VERSION
    ;;

  # ======================================================================================
  final:release-* )
    echo "Final release."
    OLD_VERSION=$(get_release_version)

    if [[ $OLD_VERSION != *-rc* ]]; then
      echo "Current version is already a final release.  You need to create a new candidate first." >&2
      exit 1
    fi

    if [ $# -gt 1 ]; then
      echo "Cannot cherry-pick into final release.  Make another candidate." >&2
      exit 1
    fi

    RC=${OLD_VERSION##*-rc}
    NEW_VERSION=${OLD_VERSION%%-rc*}

    echo "Version: $NEW_VERSION"

    echo "========================================================================="
    echo "Updating version number to $NEW_VERSION..."
    echo "========================================================================="

    update_version $OLD_VERSION $NEW_VERSION "release branch"

    build_packages $NEW_VERSION

    done_banner $NEW_VERSION release-$NEW_VERSION
    ;;

  # ======================================================================================
  retry:release-* )
    echo "Retrying release."
    OLD_VERSION=$(get_release_version)
    echo "Version: $OLD_VERSION"

    if [[ $OLD_VERSION == *-rc* ]]; then
      # We can add more cherry-picks when retrying a candidate.
      cherry_pick "$@"
    else
      if [ $# -gt 1 ]; then
        echo "Cannot cherry-pick into final release.  Make another candidate." >&2
        exit 1
      fi
    fi

    OLD_VERSION=$(get_release_version)
    build_packages $OLD_VERSION
    done_banner $OLD_VERSION release-$OLD_VERSION
    ;;

  # ======================================================================================
  *:master )
    echo "Invalid command for mainline branch.  Only command is 'candidate'." >&2
    exit 1
    ;;

  *:release-* )
    echo "Invalid command for release branch.  Commands are 'candidate', 'final', and 'retry'." >&2
    exit 1
    ;;

  * )
    echo "Not a master or release branch." >&2
    exit 1
    ;;
esac
