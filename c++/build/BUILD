load("@bazel_skylib//rules:common_settings.bzl", "bool_flag")

# Flags to configure KJ library build.
bool_flag(
    name = "openssl",
    build_setting_default = False,
)

bool_flag(
    name = "zlib",
    build_setting_default = False,
)

bool_flag(
    name = "brotli",
    build_setting_default = False,
)

bool_flag(
    name = "libdl",
    build_setting_default = False,
)

bool_flag(
    name = "deprecate_kj_if_maybe",
    build_setting_default = True,
)

bool_flag(
    name = "deprecate_empty_maybe_from_nullptr",
    build_setting_default = True,
)

# Settings to use in select() expressions
config_setting(
    name = "use_openssl",
    flag_values = {"openssl": "True"},
    visibility = ["//visibility:public"],
)

config_setting(
    name = "use_zlib",
    flag_values = {"zlib": "True"},
)

config_setting(
    name = "use_brotli",
    flag_values = {"brotli": "True"},
)

config_setting(
    name = "use_libdl",
    flag_values = {"libdl": "True"},
)

config_setting(
    name = "use_deprecate_kj_if_maybe",
    flag_values = {"deprecate_kj_if_maybe": "True"},
)

config_setting(
    name = "use_deprecate_empty_maybe_from_nullptr",
    flag_values = {"deprecate_empty_maybe_from_nullptr": "True"},
)

cc_library(
    name = "kj-defines",
    defines = select({
        ":use_openssl": ["KJ_HAS_OPENSSL"],
        "//conditions:default": [],
    }) + select({
        ":use_zlib": ["KJ_HAS_ZLIB"],
        "//conditions:default": [],
    }) + select({
        ":use_brotli": ["KJ_HAS_BROTLI"],
        "//conditions:default": [],
    }) + select({
        ":use_libdl": ["KJ_HAS_LIBDL"],
        "//conditions:default": [],
    }) + select({
        ":use_deprecate_kj_if_maybe": ["KJ_DEPRECATE_KJ_IF_MAYBE=1"],
        "//conditions:default": ["KJ_DEPRECATE_KJ_IF_MAYBE=0"],
    }) + select({
        ":use_deprecate_empty_maybe_from_nullptr": ["KJ_DEPRECATE_EMPTY_MAYBE_FROM_NULLPTR=1"],
        "//conditions:default": ["KJ_DEPRECATE_EMPTY_MAYBE_FROM_NULLPTR=0"],
    }),
    visibility = ["//visibility:public"],
)
