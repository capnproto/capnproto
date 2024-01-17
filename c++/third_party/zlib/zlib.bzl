"Root build file for Zlib."

cc_library(
    name = "zlib",
    srcs = glob(["*.c"]),
    hdrs = glob(["*.h"]),
    includes = ["."],
    # Workaround for zlib warnings and mac compilation. Some issues were resolved in v1.3, but there are still implicit function declarations.
    copts = [
        "-w",
        "-Dverbose=-1",
    ] + select({
        "@platforms//os:macos": [ "-Wno-implicit-function-declaration" ],
        "@platforms//os:linux": [ "-Wno-implicit-function-declaration" ],
        "//conditions:default": [],
    }),
    visibility = ["//visibility:public"],
)
