"""Bazel rule to compile .capnp files into c++."""
load(":capnp_gen.bzl", "capnp_gen", _capnp_provider = "capnp_provider")

# re-export for backward compatibility
capnp_provider = _capnp_provider

def cc_capnp_library(
        name,
        srcs = [],
        data = [],
        deps = [],
        src_prefix = "",
        tags = ["off-by-default"],
        visibility = None,
        target_compatible_with = None,
        **kwargs):
    """Bazel rule to create a C++ capnproto library from capnp source files

    Args:
        name: library name
        srcs: list of files to compile
        data: additional files to provide to the compiler - data files and includes that need not to
            be compiled
        deps: other cc_capnp_library rules to depend on
        src_prefix: src_prefix for capnp compiler to the source root
        visibility: rule visibility
        target_compatible_with: target compatibility
        **kwargs: rest of the arguments to cc_library rule
    """

    hdrs = [s + ".h" for s in srcs]
    srcs_cpp = [s + ".c++" for s in srcs]

    capnp_gen(
        name = name + "_gen",
        srcs = srcs,
        deps = [s + "_gen" for s in deps],
        data = data,
        outs = hdrs + srcs_cpp,
        src_prefix = src_prefix,
        visibility = visibility,
        capnpc_plugin = "@capnp-cpp//src/capnp:capnpc-c++",
        target_compatible_with = target_compatible_with,
    )
    native.cc_library(
        name = name,
        srcs = srcs_cpp,
        hdrs = hdrs,
        deps = deps + ["@capnp-cpp//src/capnp:capnp_runtime"],
        # Allows us to avoid building the library archive when using start_end_lib
        tags = tags,
        visibility = visibility,
        target_compatible_with = target_compatible_with,
        **kwargs
    )
