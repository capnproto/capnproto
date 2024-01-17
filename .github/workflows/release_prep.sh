#!/usr/bin/env bash

set -o errexit -o nounset -o pipefail

# Set by GH actions, see
# https://docs.github.com/en/actions/learn-github-actions/environment-variables#default-environment-variables
TAG=${GITHUB_REF_NAME}
# The prefix is chosen to match what GitHub generates for source archives
PREFIX="capnp-cpp-${TAG:1}"
ARCHIVE="capnp-cpp-$TAG.tar.gz"
git archive --format=tar --prefix=${PREFIX}/ ${TAG} | gzip > $ARCHIVE
SHA=$(shasum -a 256 $ARCHIVE | awk '{print $1}')

cat << EOF
1. Enable with \`common --enable_bzlmod\` in \`.bazelrc\`.
2. Add to your \`MODULE.bazel\` file:

\`\`\`starlark
bazel_dep(name = "capnp-cpp", version = "${TAG:1}")
\`\`\`

## Using WORKSPACE

Paste this snippet into your `WORKSPACE.bazel` file:

\`\`\`starlark
load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
http_archive(
    name = "capnp-cpp",
    sha256 = "${SHA}",
    strip_prefix = "${PREFIX}",
    url = "https://github.com/capnproto/capnp-cpp/releases/download/${TAG}/${ARCHIVE}",
)
EOF

awk 'f;/--SNIP--/{f=1}' integration_tests/workspace/WORKSPACE.bazel
echo "\`\`\`" 
