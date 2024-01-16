"Provides macro exports for Cap'n'Proto use from Bazel. This API surface is public."

load(
    "@capnp-cpp//internal:cc_capnp_library.bzl",
    _cc_capnp_library = "cc_capnp_library"
)

## Exports.
cc_capnp_library = _cc_capnp_library
