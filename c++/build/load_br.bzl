load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

# Defined in a bzl file to allow dependents to pull in brotli via capnproto. Using latest brotli
# commit due to macOS compile issues with v1.0.9, switch to a release version later
def load_brotli():
    http_archive(
        name = "brotli",
        sha256 = "e33f397d86aaa7f3e786bdf01a7b5cff4101cfb20041c04b313b149d34332f64",
        strip_prefix = "google-brotli-ed1995b",
        type = "tgz",
        urls = ["https://github.com/google/brotli/tarball/ed1995b6bda19244070ab5d331111f16f67c8054"],
    )
