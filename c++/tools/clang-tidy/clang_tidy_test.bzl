"""clang_tidy_test: run a single clang-tidy check over a source file and verify its diagnostics.

A test is either positive or negative:

* Positive (`expect_match = True`): every line of `src` that carries a `// CHECK-MATCH` marker
  must produce at least one diagnostic from `check`, and no other line may produce one. At least
  one marker is required.
* Negative (`expect_match = False`): `check` must not produce any diagnostic in `src`. Markers are
  not allowed.

In both modes the test fails if `src` does not compile or if `check` is not known to clang-tidy
(e.g. because of a typo in the name), so negative tests can't pass vacuously.

The tests run the clang-tidy binary named by the `//tools/clang-tidy:clang_tidy` flag and are
incompatible (and thus skipped by `bazel test //...`) when the flag is not set:

    bazel test --//tools/clang-tidy:clang_tidy=clang-tidy-23 //tools/clang-tidy/...
"""

load("@bazel_skylib//rules:common_settings.bzl", "BuildSettingInfo")
load("@rules_cc//cc/common:cc_info.bzl", "CcInfo")

def _shell_quote(s):
    return "'" + s.replace("'", "'\\''") + "'"

def _runfiles_path(ctx, path):
    # Compilation contexts carry exec-root-relative include directories; the test runs from the
    # runfiles root of the main repository, which mirrors `File.short_path`.
    for prefix in [ctx.bin_dir.path + "/", ctx.genfiles_dir.path + "/"]:
        if path.startswith(prefix):
            path = path[len(prefix):]
            break
    if path.startswith("external/"):
        path = "../" + path[len("external/"):]
    return path or "."

def _clang_tidy_test_impl(ctx):
    clang_tidy = ctx.attr._clang_tidy[BuildSettingInfo].value
    if not clang_tidy:
        fail("clang_tidy_test requires --//tools/clang-tidy:clang_tidy=<clang-tidy binary>")

    compilation_contexts = [dep[CcInfo].compilation_context for dep in ctx.attr.deps]

    # Like the real build (see .bazelrc), define KJ_HEADER_WARNINGS so that KJ headers are not
    # treated as system headers; otherwise clang-tidy drops diagnostics located inside KJ macros.
    compiler_args = ["-xc++", "-std=c++23", "-DKJ_HEADER_WARNINGS", "-DCAPNP_HEADER_WARNINGS"]
    for cc in compilation_contexts:
        compiler_args += ["-D" + d for d in cc.defines.to_list()]
        for flag, dirs in [
            ("-iquote", cc.quote_includes),
            ("-I", cc.includes),
            ("-isystem", cc.system_includes),
        ]:
            for d in dirs.to_list():
                compiler_args += [flag, _runfiles_path(ctx, d)]
    compiler_args += ctx.attr.copts

    args = [
        "bash",
        ctx.file._runner.short_path,
        clang_tidy,
        ctx.file.config.short_path,
        ctx.attr.check,
        "match" if ctx.attr.expect_match else "no-match",
        ctx.file.src.short_path,
    ] + compiler_args

    script = ctx.actions.declare_file(ctx.label.name)
    ctx.actions.write(
        output = script,
        content = "#!/usr/bin/env bash\nexec " + " ".join([_shell_quote(a) for a in args]) + "\n",
        is_executable = True,
    )

    runfiles = ctx.runfiles(
        files = [ctx.file.src, ctx.file.config, ctx.file._runner],
        transitive_files = depset(transitive = [cc.headers for cc in compilation_contexts]),
    )

    return [DefaultInfo(executable = script, runfiles = runfiles)]

_clang_tidy_test = rule(
    implementation = _clang_tidy_test_impl,
    test = True,
    attrs = {
        "src": attr.label(
            doc = "C++ source file to run clang-tidy on.",
            allow_single_file = True,
            mandatory = True,
        ),
        "check": attr.string(
            doc = "Name of the clang-tidy check under test, e.g. `custom-arrayptr-slice-zero`.",
            mandatory = True,
        ),
        "expect_match": attr.bool(
            doc = "True for a positive test (diagnostics on `// CHECK-MATCH` lines and nowhere " +
                  "else), False for a negative test (no diagnostics at all).",
            mandatory = True,
        ),
        "deps": attr.label_list(
            doc = "cc_library targets providing headers, include paths and defines for `src`.",
            providers = [CcInfo],
        ),
        "copts": attr.string_list(
            doc = "Additional compiler flags passed to clang-tidy after `--`.",
        ),
        "config": attr.label(
            doc = "clang-tidy configuration file defining the check.",
            allow_single_file = True,
            default = Label("//:.clang-tidy"),
        ),
        "_clang_tidy": attr.label(
            default = Label("//tools/clang-tidy:clang_tidy"),
        ),
        "_runner": attr.label(
            default = Label("//tools/clang-tidy:clang_tidy_test_runner.sh"),
            allow_single_file = True,
        ),
    },
)

def clang_tidy_test(name, src, check, expect_match, size = "small", target_compatible_with = [], **kwargs):
    """Defines a positive or negative test for a single clang-tidy check.

    Args:
      name: test name.
      src: C++ source file to run clang-tidy on.
      check: clang-tidy check under test, e.g. `custom-arrayptr-slice-zero`.
      expect_match: True for a positive test, False for a negative one. See the module docs.
      size: test size.
      target_compatible_with: additional compatibility constraints.
      **kwargs: forwarded to the underlying rule (`deps`, `copts`, `config`, `tags`, ...).
    """
    _clang_tidy_test(
        name = name,
        src = src,
        check = check,
        expect_match = expect_match,
        size = size,
        target_compatible_with = select({
            Label("//tools/clang-tidy:clang_tidy_disabled"): ["@platforms//:incompatible"],
            "//conditions:default": [],
        }) + target_compatible_with,
        **kwargs
    )
