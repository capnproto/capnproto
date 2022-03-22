workspace(name = "capnnp")

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository")

# zlib
http_archive(
    name = "zlib",
    build_file = "//bazel/builds:zlib.BUILD",
    sha256 = "629380c90a77b964d896ed37163f5c3a34f6e6d897311f1df2a7016355c45eff",
    strip_prefix = "zlib-1.2.11",
    urls = ["https://github.com/madler/zlib/archive/v1.2.11.tar.gz"],
)

http_archive(
    name = "openssl",
    build_file = "//bazel/builds:openssl.BUILD",
    sha256 = "6b2d2440ced8c802aaa61475919f0870ec556694c466ebea460e35ea2b14839e",
    strip_prefix = "openssl-OpenSSL_1_1_1n",
    urls = ["https://github.com/openssl/openssl/archive/refs/tags/OpenSSL_1_1_1n.tar.gz"],
)


git_repository(
    name = "hedron_compile_commands",
    branch = "main",
    remote = "https://github.com/hedronvision/bazel-compile-commands-extractor.git"
)


# rules_perl
git_repository(
    name = "rules_perl",
    branch = "main",
    remote = "https://github.com/bazelbuild/rules_perl.git",
)

load("@rules_perl//perl:deps.bzl", "perl_register_toolchains", "perl_rules_dependencies")

perl_rules_dependencies()

perl_register_toolchains()

# rules_foreign_cc
git_repository(
    name = "rules_foreign_cc",
    remote = "https://github.com/bazelbuild/rules_foreign_cc.git",
    tag = "0.7.1",
)

load("@rules_foreign_cc//foreign_cc:repositories.bzl", "rules_foreign_cc_dependencies")

rules_foreign_cc_dependencies()
