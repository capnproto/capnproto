#! /usr/bin/env bash

set -eu
shopt -s extglob

if grep 'localhost:4000' *.md _posts/*.md; then
  echo "ERROR: Your content has links to localhost:4000!" >&2
  exit 1
fi

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

echo "Regenerating site..."

rm -rf _site _site.tar.gz

jekyll build --safe --config $CONFIG

echo -n "Push now? (y/N)"
read -n 1 YESNO
echo

if [ "x$YESNO" == "xy" ]; then
  echo "Pushing..."
  tar cz --xform='s,_site/,,' _site/* | gce-ss ssh fe --command "cd /var/www/capnproto.org$PREFIX && tar xz"
else
  echo "Push CANCELED"
fi
