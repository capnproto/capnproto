"""An openssl build file based on a snippet found in the github issue:
https://github.com/bazelbuild/rules_foreign_cc/issues/337

Note that the $(PERL) "make variable" (https://docs.bazel.build/versions/main/be/make-variables.html)
is populated by the perl toolchain provided by rules_perl.
"""

load("@rules_foreign_cc//foreign_cc:defs.bzl", "configure_make", "configure_make_variant")

# Read https://wiki.openssl.org/index.php/Compilation_and_Installation

filegroup(
    name = "all_srcs",
    srcs = glob(
        include = ["**"],
        exclude = ["*.bazel"],
    ),
)

CONFIGURE_OPTIONS = [
    "no-comp",
    "no-idea",
    "no-weak-ssl-ciphers",
    "no-shared",
]

LIB_NAME = "openssl"

MAKE_TARGETS = [
    "build_libs",
    "install_dev",
]

config_setting(
    name = "msvc_compiler",
    flag_values = {
        "@bazel_tools//tools/cpp:compiler": "msvc-cl",
    },
    visibility = ["//visibility:public"],
)

alias(
    name = "openssl",
    actual = select({
        ":msvc_compiler": "openssl_msvc",
        "//conditions:default": "openssl_default",
    }),
    visibility = ["//visibility:public"],
)

configure_make_variant(
    name = "openssl_msvc",
    build_data = [
        "@nasm//:nasm",
    ],
    configure_command = "Configure",
    configure_in_place = True,
    configure_options = CONFIGURE_OPTIONS + [
        "VC-WIN64A",
        # Unset Microsoft Assembler (MASM) flags set by built-in MSVC toolchain,
        # as NASM is unsed to build OpenSSL rather than MASM
        "ASFLAGS=\" \"",
    ],
    configure_prefix = "$$PERL",
    env = {
        # The Zi flag must be set otherwise OpenSSL fails to build due to missing .pdb files
        "CFLAGS": "-Zi",
        "PATH": "$$(dirname $(execpath @nasm//:nasm)):$$PATH",
        "PERL": "$$EXT_BUILD_ROOT$$/$(PERL)",
    },
    lib_name = LIB_NAME,
    lib_source = ":all_srcs",
    out_static_libs = [
        "libssl.lib",
        "libcrypto.lib",
    ],
    targets = MAKE_TARGETS,
    toolchain = "@rules_foreign_cc//toolchains:preinstalled_nmake_toolchain",
    toolchains = ["@rules_perl//:current_toolchain"],
)

# c
configure_make(
    name = "openssl_default",
    configure_command = "config",
    configure_in_place = True,
    configure_options = CONFIGURE_OPTIONS,
    env = select({
        "@platforms//os:macos": {
            "AR": "",
            "PERL": "$$EXT_BUILD_ROOT$$/$(PERL)",
        },
        "//conditions:default": {
            "PERL": "$$EXT_BUILD_ROOT$$/$(PERL)",
        },
    }),
    lib_name = LIB_NAME,
    lib_source = ":all_srcs",
    out_lib_dir = "lib",  # some later version it would be "lib64",
    # Note that for Linux builds, libssl must come before libcrypto on the linker command-line.
    # As such, libssl must be listed before libcrypto
    out_static_libs = [
        "libssl.a",
        "libcrypto.a",
    ],
    targets = MAKE_TARGETS,
    toolchains = ["@rules_perl//:current_toolchain"],
)

filegroup(
    name = "gen_dir",
    srcs = [":openssl"],
    output_group = "gen_dir",
    visibility = ["//visibility:public"],
)
