#! /usr/bin/env bash

set -euo pipefail

if [ "$1" != "package" ]; then
  if (grep -r KJ_DBG c++/src | egrep -v '/debug(-test)?[.]' | grep -v 'See KJ_DBG\.$'); then
    echo '*** Error:  There are instances of KJ_DBG in the code.' >&2
    exit 1
  fi

  if (egrep -r 'TODO\((now|soon)\)' *); then
    echo '*** Error:  There are release-blocking TODOs in the code.' >&2
    exit 1
  fi
fi

doit() {
  echo "@@@@ $@"
  "$@"
}

get_version() {
  local VERSION=$(grep '^AC_INIT' c++/configure.ac | sed -e 's/^[^]]*],\[\([^]]*\)].*$/\1/g')
  if [[ ! "$VERSION" =~ $1 ]]; then
    echo "Couldn't parse version: $VERSION" >&2
    exit 1
  fi
  echo "$VERSION"
}

get_release_version() {
  get_version '^[0-9]+[.][0-9]+[.][0-9]+(-rc[0-9]+|[.][0-9]+)?$'
}

update_version() {
  local OLD=$1
  local NEW=$2
  local BRANCH_DESC=$3

  local OLD_REGEX=${OLD//./[.]}
  doit sed -i -e "s/$OLD_REGEX/$NEW/g" c++/configure.ac
  doit sed -i -e "s/set(VERSION.*)/set(VERSION $NEW)/g" c++/CMakeLists.txt

  local NEW_NOTAG=${NEW%%-*}
  declare -a NEW_ARR=(${NEW_NOTAG//./ })
  doit sed -i -re "
      s/^#define CAPNP_VERSION_MAJOR [0-9]+\$/#define CAPNP_VERSION_MAJOR ${NEW_ARR[0]}/g;
      s/^#define CAPNP_VERSION_MINOR [0-9]+\$/#define CAPNP_VERSION_MINOR ${NEW_ARR[1]}/g;
      s/^#define CAPNP_VERSION_MICRO [0-9]+\$/#define CAPNP_VERSION_MICRO ${NEW_ARR[2]:-0}/g" \
      c++/src/capnp/common.h

  local NEW_COMBINED=$(( ${NEW_ARR[0]} * 1000000 + ${NEW_ARR[1]} * 1000 + ${NEW_ARR[2]:-0 }))
  doit sed -i -re "s/^#if CAPNP_VERSION != [0-9]*\$/#if CAPNP_VERSION != $NEW_COMBINED/g" \
      c++/src/*/*.capnp.h c++/src/*/*/*.capnp.h

  doit git commit -a -m "Set $BRANCH_DESC version to $NEW."
}

build_packages() {
  local VERSION=$1
  local VERSION_BASE=${VERSION%%-*}

  echo "========================================================================="
  echo "Building C++ package..."
  echo "========================================================================="

  # make dist tarball and move into ..
  cd c++
  doit autoreconf -i
  doit ./configure
  doit make -j6 distcheck
  doit mv capnproto-c++-$VERSION.tar.gz ..
  doit make distclean

  # build windows executables
  doit ./configure --host=i686-w64-mingw32 --with-external-capnp \
      --disable-shared CXXFLAGS='-static-libgcc -static-libstdc++'
  doit make -j6 capnp.exe capnpc-c++.exe capnpc-capnp.exe
  doit i686-w64-mingw32-strip capnp.exe capnpc-c++.exe capnpc-capnp.exe
  doit mkdir capnproto-tools-win32-$VERSION
  doit mv capnp.exe capnpc-c++.exe capnpc-capnp.exe capnproto-tools-win32-$VERSION
  doit make maintainer-clean

  # repack dist tarball and win32 tools into win32 zip, with DOS line endings
  doit tar zxf ../capnproto-c++-$VERSION.tar.gz
  find capnproto-c++-$VERSION -name '*.c++' -o -name '*.h' -o -name '*.capnp' -o -name '*.md' -o -name '*.txt' | grep -v testdata | doit xargs unix2dos
  doit zip -r ../capnproto-c++-win32-$VERSION.zip capnproto-c++-$VERSION capnproto-tools-win32-$VERSION

  rm -rf capnproto-c++-$VERSION capnproto-tools-win32-$VERSION
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
  local FINAL=$3
  echo "========================================================================="
  echo "Done"
  echo "========================================================================="
  echo "Ready to release:"
  echo "  capnproto-c++-$VERSION.tar.gz"
  echo "  capnproto-c++-win32-$VERSION.zip"
  echo "Don't forget to push changes:"
  echo "  git push origin $PUSH"

  read -s -n 1 -p "Shall I push to git and upload to capnproto.org now? (y/N)" YESNO

  echo
  case "$YESNO" in
    y | Y )
      doit git push origin $PUSH
      doit gce-ss copy-files capnproto-c++-$VERSION.tar.gz capnproto-c++-win32-$VERSION.zip \
          fe:/var/www/capnproto.org

      if [ "$FINAL" = yes ]; then
        echo "========================================================================="
        echo "Publishing docs"
        echo "========================================================================="
        cd doc
        doit ./push-site.sh
        cd ..
        echo "========================================================================="
        echo "Really done"
        echo "========================================================================="
      fi

      echo "Release is available at:"
      echo "  http://capnproto.org/capnproto-c++-$VERSION.tar.gz"
      ;;
    * )
      echo "OK, do it yourself then."
      ;;
  esac
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

    done_banner $RELEASE_VERSION-rc1 "master release-$RELEASE_VERSION" no
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

    done_banner $RC_VERSION release-$BRANCH_VERSION no
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

    doit sed -i -re "s/capnproto-c[+][+]-[0-9]+[.][0-9]+[.][0-9]+([.][0-9]+)?\>/capnproto-c++-$NEW_VERSION/g" doc/install.md
    doit sed -i -re "s/capnproto-c[+][+]-win32-[0-9]+[.][0-9]+[.][0-9]+([.][0-9]+)?\>/capnproto-c++-win32-$NEW_VERSION/g" doc/install.md
    doit sed -i -re "s/capnproto-tools-win32-[0-9]+[.][0-9]+[.][0-9]+([.][0-9]+)?\>/capnproto-tools-win32-$NEW_VERSION/g" doc/install.md
    update_version $OLD_VERSION $NEW_VERSION "release branch"

    doit git tag v$NEW_VERSION

    build_packages $NEW_VERSION

    done_banner $NEW_VERSION "v$NEW_VERSION release-$NEW_VERSION" yes
    ;;

  # ======================================================================================
  security:release-* )
    echo "Security release."
    OLD_VERSION=$(get_release_version)

    if [[ $OLD_VERSION == *-rc* ]]; then
      echo "Security releases don't have candidates." >&2
      exit 1
    fi

    declare -a VERSION_ARR=(${OLD_VERSION//./ } 0)
    NEW_VERSION=${VERSION_ARR[0]}.${VERSION_ARR[1]}.${VERSION_ARR[2]}.$((VERSION_ARR[3] + 1))

    echo "Version: $NEW_VERSION"

    echo "========================================================================="
    echo "Updating version number to $NEW_VERSION..."
    echo "========================================================================="

    doit sed -i -re "s/capnproto-c[+][+]-[0-9]+[.][0-9]+[.][0-9]+([.][0-9]+)?\>/capnproto-c++-$NEW_VERSION/g" doc/install.md
    doit sed -i -re "s/capnproto-c[+][+]-win32-[0-9]+[.][0-9]+[.][0-9]+([.][0-9]+)?\>/capnproto-c++-win32-$NEW_VERSION/g" doc/install.md
    doit sed -i -re "s/capnproto-tools-win32-[0-9]+[.][0-9]+[.][0-9]+([.][0-9]+)?\>/capnproto-tools-win32-$NEW_VERSION/g" doc/install.md
    update_version $OLD_VERSION $NEW_VERSION "release branch"

    cherry_pick "$@"

    doit git tag v$NEW_VERSION

    build_packages $NEW_VERSION

    done_banner $NEW_VERSION "v$NEW_VERSION release-$NEW_VERSION" yes
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

    if [[ $OLD_VERSION == *-rc* ]]; then
      BRANCH_VERSION=${OLD_VERSION%%-rc*}
      done_banner $OLD_VERSION release-$BRANCH_VERSION no
    else
      doit git tag v$OLD_VERSION
      done_banner $OLD_VERSION "v$OLD_VERSION release-$OLD_VERSION" no
    fi
    ;;

  # ======================================================================================
  package:* )
    echo "Just building a package."
    build_packages $(get_version '.*')
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
