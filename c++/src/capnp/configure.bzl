load("@bazel_skylib//rules:common_settings.bzl", "bool_flag")

def capnp_configure():
    """Generates set of flag, settings for capnp configuration.
    """

    # Define some methods for generated capnp code in source file instead of header, reducing
    # header parsing overhead but reducing inlining opportunities. Recommended for debug builds.
    bool_flag(
        name = "capnpc_generate_stubbed_includes",
        build_setting_default = False,
    )

    # Generate rust capnp libraries
    bool_flag(
        name = "gen_rust",
        build_setting_default = False,
    )

    # Settings to use in select() expressions
    native.config_setting(
        name = "capnpc_generate_stubbed_includes_true",
        flag_values = {"capnpc_generate_stubbed_includes": "True"},
        visibility = ["//visibility:public"],
    )
    native.config_setting(
        name = "gen_rust_true",
        flag_values = {"gen_rust": "True"},
        visibility = ["//visibility:public"],
    )
