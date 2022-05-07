CAPNPROTO_COPTS = [
    "-Wall",
    "-Wextra",
    "-Wno-deprecated-declarations",
    "-Wno-maybe-uninitialized",
    "-Wno-strict-aliasing",
    "-Wno-sign-compare",
    "-Wno-unused-parameter",
    # in order to handle some non-standard quote includes
    "-iquote c++/src/kj",
    "-iquote c++/src/capnp/compiler",
]

# ==============  kj ===================
cc_library(
    name = "kj",
    srcs = [
        "c++/src/kj/arena.c++",
        "c++/src/kj/array.c++",
        "c++/src/kj/common.c++",
        "c++/src/kj/debug.c++",
        "c++/src/kj/encoding.c++",
        "c++/src/kj/exception.c++",
        "c++/src/kj/filesystem.c++",
        "c++/src/kj/filesystem-disk-unix.c++",
        "c++/src/kj/filesystem-disk-win32.c++",
        "c++/src/kj/hash.c++",
        "c++/src/kj/io.c++",
        "c++/src/kj/list.c++",
        "c++/src/kj/main.c++",
        "c++/src/kj/memory.c++",
        "c++/src/kj/mutex.c++",
        "c++/src/kj/parse/char.c++",
        "c++/src/kj/refcount.c++",
        "c++/src/kj/source-location.c++",
        "c++/src/kj/string.c++",
        "c++/src/kj/string-tree.c++",
        "c++/src/kj/table.c++",
        "c++/src/kj/test-helpers.c++",
        "c++/src/kj/thread.c++",
        "c++/src/kj/time.c++",
        "c++/src/kj/units.c++",
    ] + [
        "c++/src/kj/miniposix.h",
        "c++/src/kj/test.h",
    ],
    hdrs = [
        "c++/src/kj/arena.h",
        "c++/src/kj/array.h",
        "c++/src/kj/common.h",
        "c++/src/kj/debug.h",
        "c++/src/kj/encoding.h",
        "c++/src/kj/exception.h",
        "c++/src/kj/filesystem.h",
        "c++/src/kj/function.h",
        "c++/src/kj/hash.h",
        "c++/src/kj/io.h",
        "c++/src/kj/list.h",
        "c++/src/kj/main.h",
        "c++/src/kj/map.h",
        "c++/src/kj/memory.h",
        "c++/src/kj/miniposix.h",
        "c++/src/kj/mutex.h",
        "c++/src/kj/one-of.h",
        "c++/src/kj/parse/char.h",
        "c++/src/kj/parse/common.h",
        "c++/src/kj/refcount.h",
        "c++/src/kj/source-location.h",
        "c++/src/kj/std/iostream.h",
        "c++/src/kj/string.h",
        "c++/src/kj/string-tree.h",
        "c++/src/kj/table.h",
        "c++/src/kj/thread.h",
        "c++/src/kj/threadlocal.h",
        "c++/src/kj/time.h",
        "c++/src/kj/tuple.h",
        "c++/src/kj/units.h",
        "c++/src/kj/vector.h",
        "c++/src/kj/windows-sanity.h",
    ],
    copts = CAPNPROTO_COPTS,
    defines = [
        "KJ_USE_FUTEX=0",
    ],
    # Need this because I need all my deps put relative "real" path of c++/src into -I and -isystem
    includes = ["c++/src"],
    linkopts = ["-pthread"],
    strip_include_prefix = "c++/src",
    visibility = ["//visibility:public"],
)

cc_library(
    name = "kj-test",
    srcs = [
        "c++/src/kj/test.c++",
    ],
    hdrs = [
        "c++/src/kj/compat/gtest.h",
        "c++/src/kj/test.h",
    ],
    copts = CAPNPROTO_COPTS,
    linkopts = ["-pthread"],
    strip_include_prefix = "c++/src",
    visibility = ["//visibility:public"],
    deps = [":kj"],
)

cc_library(
    name = "kj-async",
    srcs = [
        "c++/src/kj/async.c++",
        "c++/src/kj/async-io.c++",
        "c++/src/kj/async-io-internal.h",
        "c++/src/kj/async-io-unix.c++",
        "c++/src/kj/async-io-win32.c++",
        "c++/src/kj/async-unix.c++",
        "c++/src/kj/async-win32.c++",
        "c++/src/kj/timer.c++",
    ],
    hdrs = [
        "c++/src/kj/async.h",
        "c++/src/kj/async-inl.h",
        "c++/src/kj/async-io.h",
        "c++/src/kj/async-prelude.h",
        "c++/src/kj/async-queue.h",
        "c++/src/kj/async-unix.h",
        "c++/src/kj/async-win32.h",
        "c++/src/kj/timer.h",
    ],
    copts = CAPNPROTO_COPTS,
    linkopts = ["-pthread"],
    strip_include_prefix = "c++/src",
    visibility = ["//visibility:public"],
    deps = [":kj"],
)

cc_library(
    name = "kj-http",
    srcs = [
        "c++/src/kj/compat/http.c++",
        "c++/src/kj/compat/url.c++",
    ] + ["c++/src/kj/exception.h"],
    hdrs = [
        "c++/src/kj/compat/http.h",
        "c++/src/kj/compat/url.h",
    ],
    copts = CAPNPROTO_COPTS,
    linkopts = ["-pthread"],
    strip_include_prefix = "c++/src",
    visibility = ["//visibility:public"],
    deps = [":kj-async"],
)

cc_library(
    name = "kj-tls",
    srcs = [
        "c++/src/kj/compat/readiness-io.c++",
        "c++/src/kj/compat/tls.c++",
    ],
    hdrs = [
        "c++/src/kj/compat/readiness-io.h",
        "c++/src/kj/compat/tls.h",
    ],
    copts = CAPNPROTO_COPTS,
    defines = [
        "KJ_HAS_OPENSSL",
    ],
    linkopts = ["-pthread"],
    strip_include_prefix = "c++/src",
    visibility = ["//visibility:public"],
    deps = [
        ":kj-async",
        "@openssl",
    ],
)

cc_library(
    name = "kj-gzip",
    srcs = [
        "c++/src/kj/compat/gzip.c++",
    ],
    hdrs = [
        "c++/src/kj/compat/gzip.h",
    ],
    copts = CAPNPROTO_COPTS,
    defines = ["KJ_HAS_ZLIB=1"],
    linkopts = ["-pthread"],
    strip_include_prefix = "c++/src",
    visibility = ["//visibility:public"],
    deps = [
        ":kj",
        ":kj-async",
        "@zlib",
    ],
)

cc_test(
    name = "kj-tests",
    srcs = [
        "c++/src/kj/array-test.c++",
        "c++/src/kj/common-test.c++",
        "c++/src/kj/debug-test.c++",
        "c++/src/kj/exception-test.c++",
        "c++/src/kj/io-test.c++",
        "c++/src/kj/list-test.c++",
        "c++/src/kj/map-test.c++",
        "c++/src/kj/memory-test.c++",
        "c++/src/kj/mutex-test.c++",
        "c++/src/kj/std/iostream-test.c++",
        "c++/src/kj/string-test.c++",
        "c++/src/kj/table-test.c++",
        "c++/src/kj/test-test.c++",
        "c++/src/kj/threadlocal-test.c++",
        "c++/src/kj/time-test.c++",
    ],
    copts = CAPNPROTO_COPTS,
    linkopts = ["-pthread"],
    deps = [":kj-test"],
)

cc_library(
    name = "kj-heavy-tests-hdrs",
    hdrs = [
        "c++/src/kj/async-xthread-test.c++",
        "c++/src/kj/compat/http-test.c++",
        "c++/src/kj/filesystem-disk-test.c++",
        "c++/src/kj/filesystem-disk-unix.c++",
    ],
)

cc_test(
    name = "kj-heavy-tests",
    srcs = [
        "c++/src/kj/arena-test.c++",
        "c++/src/kj/async-coroutine-test.c++",
        "c++/src/kj/async-io-test.c++",
        "c++/src/kj/async-queue-test.c++",
        "c++/src/kj/async-test.c++",
        "c++/src/kj/async-unix-test.c++",
        "c++/src/kj/async-win32-test.c++",
        "c++/src/kj/async-win32-xthread-test.c++",
        "c++/src/kj/compat/gzip-test.c++",
        "c++/src/kj/compat/tls-test.c++",
        "c++/src/kj/compat/url-test.c++",
        "c++/src/kj/encoding-test.c++",
        "c++/src/kj/filesystem-test.c++",
        "c++/src/kj/function-test.c++",
        "c++/src/kj/one-of-test.c++",
        "c++/src/kj/parse/char-test.c++",
        "c++/src/kj/parse/common-test.c++",
        "c++/src/kj/refcount-test.c++",
        "c++/src/kj/string-tree-test.c++",
        "c++/src/kj/tuple-test.c++",
        "c++/src/kj/units-test.c++",
    ],
    copts = CAPNPROTO_COPTS,
    linkopts = [
        "-ldl",
        "-pthread",
    ],
    deps = [
        ":kj",
        ":kj-async",
        ":kj-gzip",
        ":kj-heavy-tests-hdrs",
        ":kj-http",
        ":kj-test",
        ":kj-tls",
    ],
)

filegroup(
    name = "capnp-schemas",
    srcs = [
        "c++/src/capnp/c++.capnp",
        "c++/src/capnp/schema.capnp",
        "c++/src/capnp/stream.capnp",
    ],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "capnp",
    srcs = [
        "c++/src/capnp/any.c++",
        "c++/src/capnp/arena.c++",
        "c++/src/capnp/blob.c++",
        "c++/src/capnp/c++.capnp.c++",
        "c++/src/capnp/dynamic.c++",
        "c++/src/capnp/layout.c++",
        "c++/src/capnp/list.c++",
        "c++/src/capnp/message.c++",
        "c++/src/capnp/schema.c++",
        "c++/src/capnp/schema.capnp.c++",
        "c++/src/capnp/schema-loader.c++",
        "c++/src/capnp/serialize.c++",
        "c++/src/capnp/serialize-packed.c++",
        "c++/src/capnp/stream.capnp.c++",
        "c++/src/capnp/stringify.c++",
    ],
    hdrs = [
        "c++/src/capnp/any.h",
        "c++/src/capnp/arena.h",
        "c++/src/capnp/blob.h",
        "c++/src/capnp/c++.capnp.h",
        "c++/src/capnp/capability.h",
        "c++/src/capnp/common.h",
        "c++/src/capnp/compat/std-iterator.h",
        "c++/src/capnp/dynamic.h",
        "c++/src/capnp/endian.h",
        "c++/src/capnp/generated-header-support.h",
        "c++/src/capnp/layout.h",
        "c++/src/capnp/list.h",
        "c++/src/capnp/membrane.h",
        "c++/src/capnp/message.h",
        "c++/src/capnp/orphan.h",
        "c++/src/capnp/pointer-helpers.h",
        "c++/src/capnp/pretty-print.h",
        "c++/src/capnp/raw-schema.h",
        "c++/src/capnp/schema.capnp.h",
        "c++/src/capnp/schema.h",
        "c++/src/capnp/schema-lite.h",
        "c++/src/capnp/schema-loader.h",
        "c++/src/capnp/schema-parser.h",
        "c++/src/capnp/serialize.h",
        "c++/src/capnp/serialize-async.h",
        "c++/src/capnp/serialize-packed.h",
        "c++/src/capnp/serialize-text.h",
        "c++/src/capnp/stream.capnp.h",
    ],
    copts = CAPNPROTO_COPTS,
    data = [":capnp-schemas"],
    linkopts = ["-pthread"],
    strip_include_prefix = "c++/src",
    visibility = ["//visibility:public"],
    deps = [
        "//:kj",
        "//:kj-async",
    ],
)

filegroup(
    name = "capnp-rpc-schemas",
    srcs = [
        "c++/src/capnp/persistent.capnp",
        "c++/src/capnp/rpc.capnp",
        "c++/src/capnp/rpc-twoparty.capnp",
    ],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "capnp-rpc",
    srcs = [
        "c++/src/capnp/capability.c++",
        "c++/src/capnp/dynamic-capability.c++",
        "c++/src/capnp/ez-rpc.c++",
        "c++/src/capnp/membrane.c++",
        "c++/src/capnp/persistent.capnp.c++",
        "c++/src/capnp/rpc.c++",
        "c++/src/capnp/rpc.capnp.c++",
        "c++/src/capnp/rpc-twoparty.c++",
        "c++/src/capnp/rpc-twoparty.capnp.c++",
        "c++/src/capnp/serialize-async.c++",
    ],
    hdrs = [
        "c++/src/capnp/ez-rpc.h",
        "c++/src/capnp/persistent.capnp.h",
        "c++/src/capnp/rpc.capnp.h",
        "c++/src/capnp/rpc.h",
        "c++/src/capnp/rpc-prelude.h",
        "c++/src/capnp/rpc-twoparty.capnp.h",
        "c++/src/capnp/rpc-twoparty.h",
    ],
    copts = CAPNPROTO_COPTS,
    data = [":capnp-rpc-schemas"],
    linkopts = ["-pthread"],
    strip_include_prefix = "c++/src",
    visibility = ["//visibility:public"],
    deps = [
        ":capnp",
        "//:kj",
        "//:kj-async",
    ],
)

filegroup(
    name = "capnp-json-schemas",
    srcs = [
        "c++/src/capnp/compat/json.h",
    ],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "capnp-json",
    srcs = [
        "c++/src/capnp/compat/json.c++",
        "c++/src/capnp/compat/json.capnp.c++",
    ],
    hdrs = [
        "c++/src/capnp/compat/json.capnp.h",
        "c++/src/capnp/compat/json.h",
    ],
    copts = CAPNPROTO_COPTS,
    data = [":capnp-json-schemas"],
    linkopts = ["-pthread"],
    strip_include_prefix = "c++/src",
    visibility = ["//visibility:public"],
    deps = [
        ":capnp",
        "//:kj",
        "//:kj-async",
    ],
)

cc_library(
    name = "capnp-websocket",
    srcs = [
        "c++/src/capnp/compat/websocket-rpc.c++",
    ],
    hdrs = [
        "c++/src/capnp/compat/websocket-rpc.h",
    ],
    copts = CAPNPROTO_COPTS,
    linkopts = ["-pthread"],
    strip_include_prefix = "c++/src",
    visibility = ["//visibility:public"],
    deps = [
        ":capnp",
        ":capnp-rpc",
        "//:kj",
        "//:kj-async",
        "//:kj-http",
    ],
)

cc_library(
    name = "capnpc",
    srcs = [
        "c++/src/capnp/compiler/compiler.c++",
        "c++/src/capnp/compiler/compiler.h",
        "c++/src/capnp/compiler/error-reporter.c++",
        "c++/src/capnp/compiler/error-reporter.h",
        "c++/src/capnp/compiler/generics.c++",
        "c++/src/capnp/compiler/generics.h",
        "c++/src/capnp/compiler/grammar.capnp.c++",
        "c++/src/capnp/compiler/lexer.c++",
        "c++/src/capnp/compiler/lexer.capnp.c++",
        "c++/src/capnp/compiler/lexer.capnp.h",
        "c++/src/capnp/compiler/node-translator.c++",
        "c++/src/capnp/compiler/node-translator.h",
        "c++/src/capnp/compiler/parser.c++",
        "c++/src/capnp/compiler/resolver.h",
        "c++/src/capnp/compiler/type-id.c++",
        "c++/src/capnp/compiler/type-id.h",
        "c++/src/capnp/schema-parser.c++",
        "c++/src/capnp/serialize-text.c++",
    ],
    hdrs = [
        "c++/src/capnp/compiler/compiler.h",
        "c++/src/capnp/compiler/grammar.capnp.h",
        "c++/src/capnp/compiler/lexer.capnp.h",
        "c++/src/capnp/compiler/lexer.h",
        "c++/src/capnp/compiler/parser.h",
    ],
    copts = CAPNPROTO_COPTS + [
        # Need this for external link to this BUILD file
        "-iquote external/capnproto/c++/src/capnp/compiler",
    ],
    # include_prefix = "capnp",
    linkopts = ["-pthread"],
    strip_include_prefix = "c++/src",
    visibility = ["//visibility:public"],
    deps = [
        ":capnp",
        "//:kj",
    ],
)

cc_binary(
    name = "capnp-bin",
    srcs = [
        "c++/src/capnp/compiler/capnp.c++",
        "c++/src/capnp/compiler/lexer.h",
        "c++/src/capnp/compiler/module-loader.c++",
        "c++/src/capnp/compiler/module-loader.h",
    ],
    copts = CAPNPROTO_COPTS,
    linkopts = ["-pthread"],
    visibility = ["//visibility:public"],
    deps = [
        ":capnp",
        ":capnp-json",
        ":capnpc",
        "//:kj",
    ],
)

cc_binary(
    name = "capnpc-cpp",
    srcs = [
        "c++/src/capnp/compiler/capnpc-c++.c++",
    ],
    copts = CAPNPROTO_COPTS,
    linkopts = ["-pthread"],
    visibility = ["//visibility:public"],
    deps = [
        ":capnp",
        "//:kj",
    ],
)
