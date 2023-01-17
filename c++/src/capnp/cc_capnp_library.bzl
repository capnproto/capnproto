"""Bazel rule to compile .capnp files into c++."""

capnp_provider = provider("Capnproto Provider", fields = {
    "includes": "includes for this target (transitive)",
    "inputs": "src + data for the target",
    "src_prefix": "src_prefix of the target",
})

def _workspace_path(label, path):
    if label.workspace_root == "":
        return path
    return label.workspace_root + "/" + path

def _capnp_gen_impl(ctx):
    label = ctx.label
    src_prefix = _workspace_path(label, ctx.attr.src_prefix) if ctx.attr.src_prefix != "" else ""
    includes = []

    inputs = ctx.files.srcs + ctx.files.data
    for dep_target in ctx.attr.deps:
        includes += dep_target[capnp_provider].includes
        inputs += dep_target[capnp_provider].inputs

    if src_prefix != "":
        includes.append(src_prefix)

    system_include = ctx.files._capnp_system[0].dirname.removesuffix("/capnp")

    gen_dir = ctx.var["GENDIR"]
    out_dir = gen_dir
    if src_prefix != "":
        out_dir = out_dir + "/" + src_prefix

    cc_out = "-o%s:%s" % (ctx.executable._capnpc_cxx.path, out_dir)
    args = ctx.actions.args()
    args.add_all(["compile", "--verbose", cc_out])
    args.add_all(["-I" + inc for inc in includes])
    args.add_all(["-I", system_include])

    if src_prefix == "":
        # guess src_prefix for generated files
        for src in ctx.files.srcs:
            if src.path.startswith(gen_dir):
                src_prefix = gen_dir
                break

    if src_prefix != "":
        args.add_all(["--src-prefix", src_prefix])

    args.add_all([s for s in ctx.files.srcs])

    ctx.actions.run(
        inputs = inputs + ctx.files._capnpc_cxx + ctx.files._capnpc_capnp + ctx.files._capnp_system,
        outputs = ctx.outputs.outs,
        executable = ctx.executable._capnpc,
        arguments = [args],
        mnemonic = "GenCapnp",
    )

    return [
        capnp_provider(
            includes = includes,
            inputs = inputs,
            src_prefix = src_prefix,
        ),
    ]

_capnp_gen = rule(
    attrs = {
        "srcs": attr.label_list(allow_files = True),
        "deps": attr.label_list(providers = [capnp_provider]),
        "data": attr.label_list(allow_files = True),
        "outs": attr.output_list(),
        "src_prefix": attr.string(),
        "_capnpc": attr.label(executable = True, allow_single_file = True, cfg = "exec", default = "@capnp-cpp//src/capnp:capnp_tool"),
        "_capnpc_cxx": attr.label(executable = True, allow_single_file = True, cfg = "exec", default = "@capnp-cpp//src/capnp:capnpc-c++"),
        "_capnpc_capnp": attr.label(executable = True, allow_single_file = True, cfg = "exec", default = "@capnp-cpp//src/capnp:capnpc-capnp"),
        "_capnp_system": attr.label(default = "@capnp-cpp//src/capnp:capnp_system_library"),
    },
    output_to_genfiles = True,
    implementation = _capnp_gen_impl,
)

def cc_capnp_library(
        name,
        srcs = [],
        data = [],
        deps = [],
        src_prefix = "",
        visibility = None,
        target_compatible_with = None,
        **kwargs):
    """Bazel rule to create a C++ capnproto library from capnp source files

    Args:
        name: library name
        srcs: list of files to compile
        data: additional files to provide to the compiler - data files and includes that need not to
            be compiled
        deps: other cc_capnp_library rules to depend on
        src_prefix: src_prefix for capnp compiler to the source root
        visibility: rule visibility
        target_compatible_with: target compatibility
        **kwargs: rest of the arguments to cc_library rule
    """

    hdrs = [s + ".h" for s in srcs]
    srcs_cpp = [s + ".c++" for s in srcs]

    _capnp_gen(
        name = name + "_gen",
        srcs = srcs,
        deps = [s + "_gen" for s in deps],
        data = data,
        outs = hdrs + srcs_cpp,
        src_prefix = src_prefix,
        visibility = visibility,
        target_compatible_with = target_compatible_with,
    )
    native.cc_library(
        name = name,
        srcs = srcs_cpp,
        hdrs = hdrs,
        deps = deps + ["@capnp-cpp//src/capnp:capnp_runtime"],
        visibility = visibility,
        target_compatible_with = target_compatible_with,
        **kwargs
    )
