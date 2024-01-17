"Provides dependency definition macros for Cap'n'Proto, and for use in downstream Bazel projects."

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

load(
    "//defs:dependencies.bzl",
    "BROTLI",
    "ZLIB",
    "SKYLIB",
    "BORINGSSL",
)
load(
    "@bazel_tools//tools/build_defs/repo:utils.bzl",
    "maybe",
)


def capnp_repositories(
    enable_bzlmod = False,
    omit_brotli = False,
    omit_skylib = False,
    omit_zlib = False,
    omit_boringssl = False):
    """Add Cap'n'Proto-required dependencies for a Bazel project workspace

    If repositories are already available in the Bazel build, they will not be re-added.
    Pass `omit_*` to avoid a particular dependency.

    Args:
        enable_bzlmod: Only add dependencies not available via Bzlmod.
        omit_brotli: Don't add a Brotli dependency.
        omit_skylib: Don't add a Skylib dependency.
        omit_zlib: Don't add a Zlib dependency.
        omit_boringssl: Don't add a Boringssl dependency.
    """

    # Brotli is not available yet on the Bazel Registry.
    if not omit_brotli:
        maybe(
            http_archive,
            name = "brotli",
            sha256 = BROTLI.digest,
            strip_prefix = "brotli-%s" % BROTLI.version,
            type = "tgz",
            urls = ["https://github.com/google/brotli/archive/refs/tags/v%s.tar.gz" % BROTLI.version],
        )

    if not enable_bzlmod and not omit_skylib:
        maybe(
            http_archive,
            name = "bazel_skylib",
            sha256 = SKYLIB.digest,
            urls = [
                "https://mirror.bazel.build/github.com/bazelbuild/bazel-skylib/releases/download/%s/bazel-skylib-%s.tar.gz" % (SKYLIB.version, SKYLIB.version),
                "https://github.com/bazelbuild/bazel-skylib/releases/download/%s/bazel-skylib-%s.tar.gz" % (SKYLIB.version, SKYLIB.version),
            ],
        )

    if not enable_bzlmod and not omit_zlib:
        maybe(
            http_archive,
            name = "zlib",
            sha256 = ZLIB.digest,
            strip_prefix = "zlib-%s" % ZLIB.version,
            build_file = "//third_party/zlib:zlib.bzl",
            urls = ["https://zlib.net/zlib-%s.tar.xz" % ZLIB.version],
        )

    if not enable_bzlmod and not omit_boringssl:
        maybe(
            http_archive,
            name = "ssl",
            sha256 = BORINGSSL.digest,
            strip_prefix = BORINGSSL.prefix,
            type = "tgz",
            # from master-with-bazel branch
            urls = ["https://github.com/google/boringssl/tarball/%s" % BORINGSSL.version],
        )
