---
layout: page
title: KJ: A Performance and Safety Library for C++
---

# KJ: A Performance and Safety Library for C++

KJ provides a variety of types, templates, and macros to ease the writing of correct,
high performance C++ code. Similarities are highlighted when and where KJ features and
STL features resemble each other, so if you know of an STL feature and are looking for
the equivalent KJ feature, searching for the STL feature by name should work here.

## Primitives

### Maybe (optional)

`Maybe<T>` is a template for an optionally contained value, similar to (but predating)
[`std::optional<T>`](https://en.cppreference.com/w/cpp/utility/optional). It has the advantage of
ensuring that the empty case is handled, thus preventing null pointer dereferences and/or access to
invalid objects at runtime.

You can construct a `Maybe<T>` using either a value of type `T` or using `nullptr`:

```c++
#include <kj/common.h>

auto number = kj::Maybe<double>(1.0);
auto empty = kj::Maybe<double>(nullptr);
```

You can also use it easily as part of a class, in which case it is default initialized to be empty
until you initialize it otherwise:

```c++
struct Attic {
  ...

  Attic(Ghost ghost) : ghost(ghost) {};

  ...

  kj::Maybe<Ghost> ghost;
};
```

Afterwards, you can simultaneously check for and access the contents of a `Maybe<T>` using the
`KJ_IF_MAYBE()` macro as follows:

```c++
KJ_IF_MAYBE(pointerToValue, maybeValue) {
  doSomething(*pointerToValue);
} else {
  handleEmptyCase();
}
```
