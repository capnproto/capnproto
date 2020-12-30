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

### OneOf (variant)

`OneOf<T>` is a template for a type-safe tagged union, similar to (but predating)
[`std::variant`](https://en.cppreference.com/w/cpp/utility/variant). It has the advantage of
enabling easy handing of the different control flow paths with compiler warnings automatically for
unhandled cases using a familiar `switch`/`case` approach.

You can construct a `OneOf<T>` using a value valid for any of the types:

```c++
#include <kj/one-of.h>

auto numberInt = kj::OneOf<int, double, kj::String, bignum>(8);
auto numberFloat = kj::OneOf<int, double, kj::String, bignum>(8.0);
auto numberString = kj::OneOf<int, double, kj::String, bignum>("8");
```

Afterwards, you can handle the various cases as follows:

```c+++
KJ_SWITCH_ONEOF(number) {
  KJ_CASE_ONEOF(i, int) {
    doSomethingWithInt(i);
  }
  KJ_CASE_ONEOF(d, double) {
    doSomethingWithDouble(d);
  }
  KJ_CASE_ONEOF(s, kj::String) {
    doSomethingWithString(s);
  }
  KJ_CASE_ONEOF_DEFAULT {
    doSomethingElse();
  }
}
```

`OneOf<T>` is non-nullable by default; using a nullish value (`nullptr`, etc) to make it nullable
is inadvisable because it may interfere with implementation details. Instead, if you need an empty
state, consider creating an empty struct which you can then use as an explicit empty state:

```c++
struct Empty {};

kj::OneOf<Empty, double, kj::String> foo;
```

### Own (unique_ptr)

`Own<T>` is a template for a smart pointer that ensures that only a single owner holds a given
pointer at a given point in time, similar to
[`std::unique_ptr`](https://en.cppreference.com/w/cpp/memory/unique_ptr). It has the advantage
of not allowing manual `release()` of the owned memory and only allowing automatic memory
management, thus preventing accidental double-frees and facilitating the trivial implementation
of custom memory allocators if and when needed.

For the general case, you can construct an `Own<T>` using `kj::heap<T>(Args ...)`, which allocates
memory on the heap and constructs an instance of `T` by calling the constructor matching the
provided arguments:

```c++
#include <kj/memory.h>

class Thing {
  ...

  Thing(unsigned quantity, double difficulty);

  ...
};

auto ownedThing = kj::heap<Thing>(1, 7.5);
```

You can also use it as part of a class, in which case it's default initialized to `nullptr` until
you initialize it otherwise:

```c++
class Continent {
  ...

  Continent(kj::Own<Thing> creature) : creature(kj::mv(creature)) {};

  ...

  kj::Own<Thing> creature;

  ...
};

Continent antarctica(kj::heap<Thing>(3, 999));
```

Since an `Own<T>` is fundamentally a wrapper around a pointer, you can use it in the same manner as
a raw pointer when attempting to access its internals:

```c++
if (ownedThing) {
  ownedThing->morph();
}
```

Meanwhile, if you'd like to transfer ownership of the underlying pointer, you can simply move your
existing `Own<T>` into another, which will set the original (existing) `Own<T>` to `nullptr`:

```c++
transport(kj::mv(ownedThing))
```

When the `Own<T>` goes out of scope or when an object containing an `Own<T>` is destructed, the
_disposer_ associated with the particular `Own<T>` is called to perform any necessary destruction
and/or deallocation of the underlying object pointed to.
