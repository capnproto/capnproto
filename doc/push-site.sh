#! /usr/bin/env bash

set -eu
shopt -s extglob

if [ "x$(git status --porcelain)" != "x" ]; then
  echo -n "git repo has uncommited changes.  Continue anyway? (y/N) " >&2
  read -n 1 YESNO
  echo >&2
  if [ "x$YESNO" != xy ]; then
    exit 1
  fi
fi

case $(git rev-parse --abbrev-ref HEAD) in
  master )
    echo "On master branch.  Will generate to /next."
    CONFIG=_config_next.yml
    PREFIX=/next
    LABEL="preview site"
    ;;

  release-* )
    echo "On release branch.  Will generate to /."
    CONFIG=_config.yml
    PREFIX=
    LABEL="site"
    ;;

  * )
    echo "Unrecognized branch." >&2
    exit 1
    ;;
esac

echo "Checking out doc branch in ./.gh-pages..."

if [ ! -e .gh-pages ]; then
  git clone -b gh-pages git@github.com:kentonv/capnproto.git .gh-pages
  cd .gh-pages
else
  cd .gh-pages
  git pull
fi

if [ "x$(git status --porcelain)" != "x" ]; then
  echo "error:  .gh-pages is not clean." >&2
  exit 1
fi

cd ..

echo "Regenerating site..."

rm -rf _site .gh-pages$PREFIX/!(next)

jekyll build --safe --config $CONFIG
mkdir -p .gh-pages$PREFIX
cp -r _site/* .gh-pages$PREFIX

REV="$(git rev-parse HEAD)"

cd .gh-pages
git add *
git commit -m "$LABEL generated @$REV"

if [ "x$(git status --porcelain)" != "x" ]; then
  echo "error:  .gh-pages is not clean after commit." >&2
  exit 1
fi

echo -n "Push now? (y/N)"
read -n 1 YESNO
echo

if [ "x$YESNO" == "xy" ]; then
  git push
  cd ..
else
  echo "Did not push.  You may want to delete .gh-pages."
fi
