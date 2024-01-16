---
layout: page
title: Usage from Bazel
---

# Using Cap'n'Proto from Bazel

Cap'n'Proto is available for use directly from downstream Bazel projects, via the two supported mechanisms for Bazel dependencies: `WORKSPACE.bazel` and `MODULE.bazel` (also known as Bzlmod).

## Installation via `WORKSPACE`

In your `WORKSPACE.bazel` file:

```python
load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

http_repository(
    name = "capnp-cpp",
    # download a tarball from the latest releases ...
)

## Setup Cap'n'Proto

load("@capnp-cpp//defs:repositories.bzl", "capnp_repositories")

capnp_repositories()

load("@capnp-cpp//defs:workspace.bzl", "capnp_workspace")

capnp_workspace()
```

## Installation via Bzlmod

In your `MODULE.bazel` file:

```python
bazel_dep(
    name = "capnp-cpp",
    version = "x.x.x",  # find latest version at registry.bazel.build
)
```

## Usage

Define a schema, for instance:

**`sample.capnp`:**
```capnproto
struct Message {
    # ...
}
```

Add a target in your `BUILD.bazel`:

**`BUILD.bazel`:**
```python
load("@capnp-cpp//defs:capnp.bzl", "cc_capnp_library")

cc_capnp_library(
    name = "my-cool-capnp",
    srcs = ["sample.capnp"],
)
```

Then you can build it:

```
bazel build //some/target/path:my-cool-capnp
```

And use it in downstream C++ code:

```python
cc_library(
    name = "my-cc-lib",
    srcs = ["..."],
    deps = [
        ":my-cool-capnp",
    ],
)
```

That's it! Pretty easy.
