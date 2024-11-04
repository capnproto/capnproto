load("@bazel_skylib//rules:common_settings.bzl", "bool_flag")

def capnp_configure():
    """Generates set of flag, settings for capnp configuration.
    """

    # Define some methods for generated capnp code in source file instead of header, reducing
    # header parsing overhead but reducing inlining opportunities. Recommended for debug builds.
    bool_flag(
        name = "capnp_no_inline_accessors",
        build_setting_default = False,
    )

    # Generate rust capnp libraries
    bool_flag(
        name = "gen_rust",
        build_setting_default = False,
    )

    # Settings to use in select() expressions
    native.config_setting(
        name = "capnp_no_inline_accessors_true",
        flag_values = {"capnp_no_inline_accessors": "True"},
        visibility = ["//visibility:public"],
    )
    native.config_setting(
        name = "gen_rust_true",
        flag_values = {"gen_rust": "True"},
        visibility = ["//visibility:public"],
    )
