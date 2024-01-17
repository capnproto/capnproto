"""Bazel rule to compile .capnp files into c++."""

load(
    "//internal:cc_capnp_library.bzl",
    _capnp_provider = "capnp_provider",
    _cc_capnp_library = "cc_capnp_library",
)

## Exports.
capnp_provider = _capnp_provider
cc_capnp_library = _cc_capnp_library
