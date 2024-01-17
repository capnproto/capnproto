"Provides WORKSPACE.bazel initialization macros for Cap'n'Proto and downstream Bazel projects."

load("@bazel_skylib//:workspace.bzl", "bazel_skylib_workspace")


def capnp_workspace(omit_skylib = False):
    """Call initialization macros for a Cap'n'Proto project via a Bazel workspace.

    Args:
        omit_skylib: Don't initialize skylib.
    """

    if not omit_skylib:
        bazel_skylib_workspace()
