load("@bazel_skylib//rules:common_settings.bzl", "bool_flag", "int_flag")

def kj_configure():
    """Generates set of flag, settings for kj configuration.

    Creates kj-defines cc_library with all necessary preprocessor defines.
    """

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
        name = "save_acquired_lock_info",
        build_setting_default = False,
    )

    bool_flag(
        name = "track_lock_blocking",
        build_setting_default = False,
    )

    bool_flag(
        name = "coroutines",
        build_setting_default = False,
    )

    # Settings to use in select() expressions
    native.config_setting(
        name = "use_openssl",
        flag_values = {"openssl": "True"},
        visibility = ["//visibility:public"],
    )

    native.config_setting(
        name = "use_zlib",
        flag_values = {"zlib": "True"},
    )

    native.config_setting(
        name = "use_brotli",
        flag_values = {"brotli": "True"},
    )

    native.config_setting(
        name = "use_libdl",
        flag_values = {"libdl": "True"},
    )

    native.config_setting(
        name = "use_coroutines",
        flag_values = {"coroutines": "True"},
    )

    native.config_setting(
        name = "use_save_acquired_lock_info",
        flag_values = {"save_acquired_lock_info": "True"},
    )

    native.config_setting(
        name = "use_track_lock_blocking",
        flag_values = {"track_lock_blocking": "True"},
    )

    native.cc_library(
        name = "kj-defines",
        defines = select({
            "//src/kj:use_openssl": ["KJ_HAS_OPENSSL"],
            "//conditions:default": [],
        }) + select({
            "//src/kj:use_zlib": ["KJ_HAS_ZLIB"],
            "//conditions:default": [],
        }) + select({
            "//src/kj:use_brotli": ["KJ_HAS_BROTLI"],
            "//conditions:default": [],
        }) + select({
            "//src/kj:use_libdl": ["KJ_HAS_LIBDL"],
            "//conditions:default": [],
        }) + select({
            "//src/kj:use_save_acquired_lock_info": ["KJ_SAVE_ACQUIRED_LOCK_INFO=1"],
            "//conditions:default": ["KJ_SAVE_ACQUIRED_LOCK_INFO=0"],
        }) + select({
            "//src/kj:use_track_lock_blocking": ["KJ_TRACK_LOCK_BLOCKING=1"],
            "//conditions:default": ["KJ_TRACK_LOCK_BLOCKING=0"],
        }),
    )
