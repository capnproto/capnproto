load("@bazel_skylib//rules:common_settings.bzl", "bool_flag")

def capnp_configure():
    """Generates set of flag, settings for capnp configuration.
    """

    # Generate rust capnp libraries
    bool_flag(
        name = "gen_rust",
        build_setting_default = False,
    )

    # Settings to use in select() expressions
    native.config_setting(
        name = "gen_rust_true",
        flag_values = {"gen_rust": "True"},
        visibility = ["//visibility:public"],
    )
