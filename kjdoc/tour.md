---
title: A tour of KJ
---

This page is a tour through the functionality provided by KJ. It is intended for developers new to KJ who want to learn the ropes.

**This page is not an API reference.** KJ's reference documentation is provided by comments in the headers themselves. Keeping reference docs in the headers makes it easy to find them using your editor's "jump to declaration" hotkey. It also ensures that the documentation is never out-of-sync with the version of KJ you are using.

Core Programming
======================================================================

This section covers core KJ features used throughout nearly all KJ-based code.

Every KJ developer should familiarize themselves at least with this section.

## Core Utility Functions

### Argument-passing: move, copy, forward

`kj::mv` has exactly the same semantics as `std::move`, but takes fewer keystrokes to type. Since this is used extraordinarily often, saving a few keystrokes really makes a legitimate difference. If you aren't familiar with `std::move`, I recommend reading up on [C++11 move semantics](https://stackoverflow.com/questions/3106110/what-is-move-semantics).

`kj::cp` is invoked in a similar way to `kj::mv`, but explicitly invokes the copy constructor of its argument, returning said copy. This is occasionally useful when invoking a function that wants an rvalue reference as a parameter, which normally requires pass-by-move, but you really want to pass it a copy.

`kj::fwd`, is equivalent to `std::forward`. It is used to implement [perfect forwarding](https://en.cppreference.com/w/cpp/utility/forward), that is, forwarding arbitrary arguments from a template function into some other function without understanding their types.

### Deferring code to scope exit

This macro declares some code which must execute when exiting the current scope (whether normally or by exception). It is essentially a shortcut for declaring a class with a destructor containing said code, and instantiating that destructor. Example:

```c++
void processFile() {
  int fd = open("file.txt", O_RDONLY);
  KJ_ASSERT(fd >= 0);

  // Make sure file is closed on return.
  KJ_DEFER(close(fd));

  // ... do something with the file ...
}
```

You can also pass a multi-line block (in curly braces) as the argument to `KJ_DEFER`.

There is also a non-macro version, `kj::defer`, which takes a lambda as its argument, and returns an object that invokes that lambda on destruction. The returned object has move semantics. This is convenient when the scope of the deferral isn't necessarily exactly function scope, such as when capturing context in a callback. Example:

```c++
kj::Function<void(int arg)> processFile() {
  int fd = open("file.txt", O_RDONLY);
  KJ_ASSERT(fd >= 0);

  // Make sure file is closed when the returned function
  // is eventually destroyed.
  auto deferredClose = kj::defer([fd]() { close(fd); });

  return [fd, deferredClose = kj::mv(deferredClose)]
         (int arg) {
    // ... do something with fd and arg ...
  }
}
```

Sometimes, you want a deferred action to occur only when the scope exits normally via `return`, or only when it exits due to an exception. For those purposes, `KJ_ON_SCOPE_SUCCESS` and `KJ_ON_SCOPE_FAILURE` may be used, with the same syntax as `KJ_DEFER`.

### Size and range helpers

`kj::size()` accepts a built-in array or a container as an argument, and returns the number of elements. In the case of a container, the container must implement a `.size()` method. The idea is that you can use this to find out how many iterations a range-based `for` loop on that container would execute. That said, in practice `kj::size` is most commonly used with arrays, as a shortcut for something like `sizeof(array) / sizeof(array[0])`.

```c++
int arr[15];
KJ_ASSERT(kj::size(arr) == 15);
```

`kj::range(i, j)` returns an iterable that contains all integers from `i` to `j` (including `i`, but not including `j`). This is typically used in `for` loops:

```c++
for (auto i: kj::range(5, 10)) {
  KJ_ASSERT(i >= 5 && i < 10);
}
```

In the very-common case of iterating from zero, `kj::zeroTo(i)` should be used instead of `kj::range(0, i)`, in order to avoid ambiguity about what type of integer should be generated.

`kj::indices(container)` is equivalent to `kj::zeroTo(kj::size(container))`. This is extremely convenient when iterating over parallel arrays.

```c++
KJ_ASSERT(foo.size() == bar.size());
for (auto i: kj::indices(foo)) {
  foo[i] = bar[i];
}
```

`kj::repeat(value, n)` returns an iterable that acts like an array of size `n` where every element is `value`. This is not often used, but can be convenient for string formatting as well as generating test data.

### Casting helpers

`kj::implicitCast<T>(value)` is equivalent to `static_cast<T>(value)`, but will generate a compiler error if `value` cannot be implicitly cast to `T`. For example, `static_cast` can be used for both upcasts (derived type to base type) and downcasts (base type to derived type), but `implicitCast` can only be used for the former.

`kj::downcast<T>(value)` is equivalent to `static_cast<T>(value)`, except that when compiled in debug mode with RTTI available, a runtime check (`dynamic_cast`) will be performed to verify that `value` really has type `T`. Use this in cases where you are casting a base type to a derived type, and you are confident that the object is actually an instance of the derived type. The debug-mode check will help you catch bugs.

`kj::dynamicDowncastIfAvailable<T>(value)` is like `dynamic_cast<T*>(value)` with two differences. First, it returns `kj::Maybe<T&>` instead of `T*`. Second, if the program is compiled without RTTI enabled, the function always returns null. This function is intended to be used to implement optimizations, where the code can do something smarter if `value` happens to be of some specific type -- but if RTTI is not available, it is safe to skip the optimization. See [KJ idiomatic use of dynamic_cast](../style-guide.md#dynamic_cast) for more background.

### Min/max, numeric limits, and special floats

`kj::min()` and `kj::max()` return the minimum and maximum of the input arguments, automatically choosing the appropriate return type even if the inputs are of different types.

`kj::minValue` and `kj::maxValue` are special constants that, when cast to an integer type, become the minimum or maximum value of the respective type. For example:

```c++
int16_t i = kj::maxValue;
KJ_ASSERT(i == 32767);
```

`kj::inf()` evaluates to floating-point infinity, while `kj::nan()` evaluates to floating-point NaN. `kj::isNaN()` returns true if its argument is NaN.

### Explicit construction and destruction

`kj::ctor()` and `kj::dtor()` explicitly invoke a constructor or destructor in a way that is readable and convenient. The first argument is a reference to memory where the object should live.

These functions should almost never be used in high-level code. They are intended for use in custom memory management, or occasionally with unions that contain non-trivial types (but consider using `kj::OneOf` instead). You must understand C++ memory aliasing rules to use these correctly.

## Ownership and memory management

KJ style makes heavy use of [RAII](../style-guide.md#raii-resource-acquisition-is-initialization). KJ-based code should never use `new` and `delete` directly. Instead, use the utilities in this section to manage memory in a RAII way.

### Owned pointers, heap allocation, and disposers

`kj::Own<T>` is a pointer to a value of type `T` which is "owned" by the holder. When a `kj::Own` goes out-of-scope, the value it points to will (typically) be destroyed and freed.

`kj::Own` has move semantics. Thus, when used as a function parameter or return type, `kj::Own` indicates that ownership of the object is being transferred.

`kj::heap<T>(args...)` allocates an object of type `T` on the heap, passing `args...` to its constructor, and returns a `kj::Own<T>`. This is the most common way to create owned objects.

However, a `kj::Own` does not necessarily refer to a heap object. A `kj::Own` is actually implemented as a pair of a pointer to the object, and a pointer to a `kj::Disposer` object that knows how to destroy it; `kj::Own`'s destructor invokes the disposer. `kj::Disposer` is an abstract interface with many implementations. `kj::heap` uses an implementation that invokes the object's destructor then frees its underlying space from the heap (like `delete` does), but other implementations exist. Alternative disposers allow an application to control memory allocation more precisely when desired.

Some example uses of disposers include:

* `kj::fakeOwn(ref)` returns a `kj::Own` that points to `ref` but doesn't actually destroy it. This is useful when you know for sure that `ref` will outlive the scope of the `kj::Own`, and therefore heap allocation is unnecessary. This is common in cases where, for example, the `kj::Own` is being passed into an object which itself will be destroyed before `ref` becomes invalid. It also makes sense when `ref` is actually a static value or global that lives forever.
* `kj::refcounted<T>(args...)` allocates a `T` which uses reference counting. It returns a `kj::Own<T>` that represents one reference to the object. Additional references can be created by calling `kj::addRef(*ptr)`. The object is destroyed when no more `kj::Own`s exist pointing at it. Note that `T` must be a subclass of `kj::Refcounted`. If references may be shared across threads, then atomic refcounting must be used; use `kj::atomicRefcounted<T>(args...)` and inherit `kj::AtomicRefcounted`. Reference counting should be using sparingly; see [KJ idioms around reference counting](../style-guide.md#reference-counting) for a discussion of when it should be used and why it is designed the way it is.
* `kj::attachRef(ref, args...)` returns a `kj::Own` pointing to `ref` that actually owns `args...`, so that when the `kj::Own` goes out-of-scope, the other arguments are destroyed. Typically these arguments are themselves `kj::Own`s or other pass-by-move values that themselves own the object referenced by `ref`. `kj::attachVal(value, args...)` is similar, where `value` is a pass-by-move value rather than a reference; a copy of it will be allocated on the heap. Finally, `ownPtr.attach(args...)` returns a new `kj::Own` pointing to the same value that `ownPtr` pointed to, but such that `args...` are owned as well and will be destroyed together. Attachments are always destroyed after the thing they are attached to.
* `kj::SpaceFor<T>` contains enough space for a value of type `T`, but does not construct the value until its `construct(args...)` method is called. That method returns an `kj::Own<T>`, whose disposer destroys the value. `kj::SpaceFor` is thus a safer way to perform manual construction compared to invoking `kj::ctor()` and `kj::dtor()`.

These disposers cover most use cases, but you can also implement your own if desired. `kj::Own` features a constructor overload that lets you pass an arbitrary disposer.

### Arrays

`kj::Array<T>` is similar to `kj::Own<T>`, but points to (and owns) an array of `T`s.

A `kj::Array<T>` can be allocated with `kj::heapArray<T>(size)`, if `T` can be default-constructed. Otherwise, you will need to use a `kj::ArrayBuilder<T>` to build the array. First call `kj::heapArrayBuilder<T>(size)`, then invoke the builder's `add(value)` method to add each element, then finally call its `finish()` method to obtain the completed `kj::Array<T>`. `ArrayBuilder` requires that you know the final size before you start; if you don't, you may want to use `kj::Vector<T>` instead.

Passing a `kj::Array<T>` implies an ownership transfer. If you merely want to pass a pointer to an array, without transferring ownership, use `kj::ArrayPtr<T>`. This type essentially encapsulates a pointer to the beginning of the array, plus its size. Note that a `kj::ArrayPtr` points to _the underlying memory_ backing a `kj::Array`, not to the `kj::Array` itself; thus, moving a `kj::Array` does NOT invalidate any `kj::ArrayPtr`s already pointing at it. You can also construct a `kj::ArrayPtr` pointing to any C-style array (doesn't have to be a `kj::Array`) using `kj::arrayPtr(ptr, size)` or `kj::arrayPtr(beginPtr, endPtr)`.

Both `kj::Array` and `kj::ArrayPtr` contain a number of useful methods, like `slice()`. Be sure to check out the class definitions for more details.

## Strings

A `kj::String` is a segment of text. By convention, this text is expected to be Unicode encoded in UTF-8. But, `kj::String` itself is not Unicode-aware; it is merely an array of `char`s.

NUL characters (`'\0'`) are allowed to appear anywhere in a string and do not terminate the string. However, as a convenience, the buffer backing a `kj::String` always has an additional NUL character appended to the end (but not counted in the size). This allows the text in a `kj::String` to be passed to legacy C APIs that use NUL-terminated strings without an extra copy; use the `.cStr()` method to get a `const char*` for such cases. (Of course, keep in mind that if the string contains NUL characters other than at the end, legacy C APIs will interpret the string as truncated at that point.)

`kj::StringPtr` represents a pointer to a `kj::String`. Similar to `kj::ArrayPtr`, `kj::StringPtr` does not point at the `kj::String` object itself, but at its backing array. Thus, moving a `kj::String` does not invalidate any `kj::StringPtr`s. This is a major difference from `std::string`! Moving an `std::string` invalidates all pointers into its backing buffer (including `std::string_view`s), because `std::string` inlines small strings as an optimization. This optimization may seem clever, but means that `std::string` cannot safely be used as a way to hold and transfer ownership of a text buffer. Doing so can lead to subtle, data-dependent bugs; a program might work fine until someone gives it an unusually small input, at which point it segfaults. `kj::String` foregoes this optimization for simplicity.

Also similar to `kj::ArrayPtr`, a `kj::StringPtr` does not have to point at a `kj::String`. It can be initialized from a string literal or any C-style NUL-terminated `const char*` without making a copy. Also, KJ defines the special literal suffix `_kj` to write a string literal whose type is implicitly `kj::StringPtr`.

```c++
// It's OK to initialize a StringPtr from a classic literal.
// No copy is performed; the StringPtr points directly at
// constant memory.
kj::StringPtr foo = "foo";

// But if you add the _kj suffix, then you don't even need
// to declare the type. `bar` will implicitly have type
// kj::StringPtr. Also, this version can be declared
// `constexpr`.
constexpr auto bar = "bar"_kj;
```

### Stringification

To allocate and construct a `kj::String`, use `kj::str(args...)`. Each argument is stringified and the results are concatenated to form the final string. (You can also allocate an uninitialized string buffer with `kj::heapString(size)`.)

```c++
kj::String makeGreeting(kj::StringPtr name) {
  return kj::str("Hello, ", name, "!");
}
```

KJ knows how to stringify most primitive types as well as many KJ types automatically. Note that integers will be stringified in base 10; if you want hexadecimal, use `kj::hex(i)` as the parameter to `kj::str()`.

You can additionally extend `kj::str()` to work with your own types by declaring a stringification method using `KJ_STRINGIFY`, like so:

```c++
enum MyType { A, B, C };
kj::StringPtr KJ_STRINGIFY(MyType value) {
  switch (value) {
    case A: return "A"_kj;
    case B: return "B"_kj;
    case C: return "C"_kj;
  }
  KJ_UNREACHABLE;
}
```

The `KJ_STRINGIFY` declaration should appear either in the same namespace where the type is defined, or in the global scope. The function can return any random-access iterable sequence of `char`, such as a `kj::String`, `kj::StringPtr`, `kj::ArrayPtr<char>`, etc. As an alternative to `KJ_STRINGIFY`, you can also declare a `toString()` method on your type, with the same return type semantics.

When constructing very large, complex strings -- for example, when writing a code generator -- consider using `kj::StringTree`, which maintains a tree of strings and only concatenates them at the very end. For example, `kj::strTree(foo, kj::strTree(bar, baz)).flatten()` only performs one concatenation, whereas `kj::str(foo, kj::str(bar, baz))` would perform a redundant intermediate concatenation.

## Core Utility Types

### Maybes

`kj::Maybe<T>` is either `nullptr`, or contains a `T`. In KJ-based code, nullable values should always be expressed using `kj::Maybe`. Primitive pointers should never be null. Use `kj::Maybe<T&>` instead of `T*` to express that the pointer/reference can be null.

In order to dereference a `kj::Maybe`, you must use the `KJ_IF_MAYBE` macro, which behaves like an `if` statement.

```c++
kj::Maybe<int> maybeI = 123;
kj::Maybe<int> maybeJ = nullptr;

KJ_IF_MAYBE(i, maybeI) {
  // This block will execute, with `i` being a
  // pointer into `maybeI`'s value. In a better world,
  // `i` would be a reference rather than a pointer,
  // but we couldn't find a way to trick the compiler
  // into that.
  KJ_ASSERT(*i == 123);
} else {
  KJ_FAIL_ASSERT("can't get here");
}

KJ_IF_MAYBE(j, maybeJ) {
  KJ_FAIL_ASSERT("can't get here");
} else {
  // This block will execute.
}
```

Note that `KJ_IF_MAYBE` forces you to think about the null case. This differs from `std::optional`, which can be dereferenced using `*`, resulting in undefined behavior if the value is null.

Similarly, `map()` and `orDefault()` allow transforming and retrieving the stored value in a safe manner without complex control flows.

Performance nuts will be interested to know that `kj::Maybe<T&>` and `kj::Maybe<Own<T>>` are both optimized such that they take no more space than their underlying pointer type, using a literal null pointer to indicate nullness. For other types of `T`, `kj::Maybe<T>` must maintain an extra boolean and so is somewhat larger than `T`.

### Variant types

`kj::OneOf<T, U, V>` is a variant type that can be assigned to exactly one of the input types. To unpack the variant, use `KJ_SWITCH_ONEOF`:

```c++
void handle(kj::OneOf<int, kj::String> value) {
  KJ_SWITCH_ONEOF(value) {
    KJ_CASE_ONEOF(i, int) {
      // Note that `i` is an lvalue reference to the content
      // of the OneOf. This differs from `KJ_IF_MAYBE` where
      // the variable is a pointer.
      handleInt(i);
    }
    KJ_CASE_ONEOF(s, kj::String) {
      handleString(s);
    }
  }
}
```

Often, in real-world usage, the type of each variant in a `kj::OneOf` is not sufficient to understand its meaning; sometimes two different variants end up having the same type used for different purposes. In these cases, it would be useful to assign a name to each variant. A common way to do this is to define a custom `struct` type for each variant, and then declare the `kj::OneOf` using those:

```c++
struct NotStarted {
  kj::String filename;
};
struct Running {
  kj::Own<File> file;
};
struct Done {
  kj::String result;
};

typedef kj::OneOf<NotStarted, Running, Done> State;
```

### Functions

`kj::Function<ReturnType(ParamTypes...)>` represents a callable function with the given signature. A `kj::Function` can be initialized from any callable object, such as a lambda, function pointer, or anything with `operator()`. `kj::Function` is useful when you want to write an API that accepts a lambda callback, without defining the API itself as a template. `kj::Function` supports move semantics.

`kj::ConstFunction` is like `kj::Function`, but is used to indicate that the function should be safe to call from multiple threads. (See [KJ idioms around constness and thread-safety](../style-guide.md#constness).)

A special optimization type, `kj::FunctionParam`, is like `kj::Function` but designed to be used specifically as the type of a callback parameter to some other function where that callback is only called synchronously; i.e., the callback won't be called anymore after the outer function returns. Unlike `kj::Function`, a `kj::FunctionParam` can be constructed entirely on the stack, with no heap allocation.

### Vectors (growable arrays)

Like `std::vector`, `kj::Vector` is an array that supports appending an element in amortized O(1) time. When the underlying backing array is full, an array of twice the size is allocated and all elements moved.

### Hash/tree maps/sets... and tables

`kj::HashMap`, `kj::HashSet`, `kj::TreeMap`, and `kj::TreeSet` do what you'd expect, with modern lambda-oriented interfaces that are less awkward than the corresponding STL types.

All of these types are actually specific instances of the more-general `kj::Table`. A `kj::Table` can have any number of columns (whereas "sets" have exactly 1 and "maps" have exactly 2), and can maintain indexes on multiple columns at once. Each index can be hash-based, tree-based, or a custom index type that you provide.

Unlike STL's, KJ's hashtable-based containers iterate in a well-defined deterministic order based on the order of insertion and removals. Deterministic behavior is important for reproducibility, which is important not just for debugging, but also in distributed systems where multiple systems must independently reproduce the same state. KJ's hashtable containers are also faster than `libstdc++`'s in benchmarks.

KJ's tree-based containers use a b-tree design for better memory locality than the more traditional red-black trees. The implementation is tuned to avoid code bloat by keeping most logic out of templates, though this does make it slightly slower than `libstdc++`'s `map` and `set` in benchmarks.

`kj::hashCode(params...)` computes a hash across all the inputs, appropriate for use in a hash table. It is extensible in a similar fashion to `kj::str()`, by using `KJ_HASHCODE` or defining a `.hashCode()` method on your custom types. `kj::Table`'s hashtable-based index uses `kj::hashCode` to compute hashes.

## Debugging and Observability

KJ believes that there is no such thing as bug-free code. Instead, we must expect that our code will go wrong, and try to extract as much information as possible when it does. To that end, KJ provides powerful assertion macros designed for observability. (Be sure also to read about [KJ's exception philosophy](../style-guide.md#exceptions); this section describes the actual APIs involved.)

### Assertions

Let's start with the basic assert:

```c++
KJ_ASSERT(foo == bar.baz, "the baz is not foo", bar.name, i);
```

When `foo == bar.baz` evaluates false, this line will throw an exception with a description like this:

```
src/file.c++:52: failed: expected foo == bar.baz [123 == 321]; the baz is not foo; bar.name = "banana"; i = 5
stack: libqux.so@0x52134 libqux.so@0x16f582 bin/corge@0x12515 bin/corge@0x5552
```

Notice all the information this contains:

* The file and line number in the source code where the assertion macro was used.
* The condition which failed.
* The stringified values of the operands to the condition, i.e. `foo` and `bar.baz` (shown in `[]` brackets).
* The values of all other parameters passed to the assertion, i.e. `"the baz is not foo"`, `bar.name`, and `i`. For expressions that aren't just string literals, both the expression and the stringified result of evaluating it are shown.
* A numeric stack trace. If possible, the addresses will be given relative to their respective binary, so that ASLR doesn't make traces useless. The trace can be decoded with tools like `addr2line`. If possible, KJ will also shell out to `addr2line` itself to produce a human-readable trace.

Note that the work of producing an error description happens only in the case that it's needed. If the condition evaluates true, then that is all the work that is done.

`KJ_ASSERT` should be used in cases where you are checking conditions that, if they fail, represent a bug in the code where the assert appears. On the other hand, when checking for preconditions -- i.e., bugs in the _caller_ of the code -- use `KJ_REQUIRE` instead:

```c++
T& operator[](size_t i) {
  KJ_REQUIRE(i < size(), "index out-of-bounds");
  // ...
}
```

`KJ_REQUIRE` and `KJ_ASSERT` do exactly the same thing; using one or the other is only a matter of self-documentation.

`KJ_FAIL_ASSERT(...)` should be used instead of `KJ_ASSERT(false, ...)` when you want a branch always to fail.

Assertions operate exactly the same in debug and release builds. To express a debug-only assertion, you can use `KJ_DASSERT`. However, we highly recommend letting asserts run in production, as they are frequently an invaluable tool for tracking down bugs that weren't covered in testing.

### Logging

The `KJ_LOG` macro can be used to log messages meant for the developer or operator without interrupting control flow.

```c++
if (foo.isWrong()) {
  KJ_LOG(ERROR, "the foo is wrong", foo);
}
```

The first parameter is the log level, which can be `INFO`, `WARNING`, `ERROR`, or `FATAL`. By default, `INFO` logs are discarded, while other levels are displayed. For programs whose main function is based on `kj/main.h`, the `-v` flag can be used to enable `INFO` logging. A `FATAL` log should typically be followed by `abort()` or similar.

Parameters other than the first are stringified in the same manner as with `KJ_ASSERT`. These parameters will not be evaluated at all, though, if the specified log level is not enabled.

By default, logs go to standard error. However, you can implement a `kj::ExceptionCallback` (in `kj/exception.h`) to capture logs and customize how they are handled.

### Debug printing

Let's face it: "printf() debugging" is easy and effective. KJ embraces this with the `KJ_DBG()` macro.

```c++
KJ_DBG("hi", foo, bar, baz.qux)
```

`KJ_DBG(...)` is equivalent to `KJ_LOG(DEBUG, ...)` -- logging at the `DEBUG` level, which is always enabled. The dedicated macro exists for brevity when debugging. `KJ_DBG` is intended to be used strictly for temporary debugging code that should never be committed. We recommend setting up commit hooks to reject code that contains invocations of `KJ_DBG`.

### System call error checking

KJ includes special variants of its assertion macros that convert traditional C API error conventions into exceptions.

```c++
int fd;
KJ_SYSCALL(fd = open(filename, O_RDONLY), "couldn't open the document", filename);
```

This macro evaluates the first parameter, which is expected to be a system call. If it returns a negative value, indicating an error, then an exception is thrown. The exception description incorporates a description of the error code communicated by `errno`, as well as the other parameters passed to the macro (stringified in the same manner as other assertion/logging macros do).

Additionally, `KJ_SYSCALL()` will automatically retry calls that fail with `EINTR`. Because of this, it is important that the expression is idempotent.

Sometimes, you need to handle certain error codes without throwing. For those cases, use `KJ_SYSCALL_HANDLE_ERRORS`:

```c++
int fd;
KJ_SYSCALL_HANDLE_ERRORS(fd = open(filename, O_RDONLY)) {
  case ENOENT:
    // File didn't exist, return null.
    return nullptr;
  default:
    // Some other error. The error code (from errno) is in a local variable `error`.
    // `KJ_FAIL_SYSCALL` expects its second parameter to be this integer error code.
    KJ_FAIL_SYSCALL("open()", error, "couldn't open the document", filename);
}
```

On Windows, two similar macros are available based on Windows API calling conventions: `KJ_WIN32` works with API functions that return a `BOOLEAN`, `HANDLE`, or pointer type. `KJ_WINSOCK` works with Winsock APIs that return negative values to indicate errors. Some Win32 APIs follow neither of these conventions, in which case you will have to write your own code to check for an error and use `KJ_FAIL_WIN32` to turn it into an exception.

### Alternate exception types

As described in [KJ's exception philosophy](../style-guide.md#exceptions), KJ supports a small set of exception types. Regular assertions throw `FAILED` exceptions. `KJ_SYSCALL` usually throws `FAILED`, but identifies certain error codes as `DISCONNECTED` or `OVERLOADED`. For example, `ECONNRESET` is clearly a `DISCONNECTED` exception.

If you wish to manually construct and throw a different exception type, you may use `KJ_EXCEPTION`:

```c++
kj::Exception e = KJ_EXCEPTION(DISCONNECTED, "connection lost", addr);
```

### Throwing and catching exceptions

KJ code usually should not use `throw` or `catch` directly, but rather use KJ's wrappers:

```c++
// Throw an exception.
kj::Exception e = ...;
kj::throwFatalException(kj::mv(e));

// Run some code catching exceptions.
kj::Maybe<kj::Exception> maybeException = kj::runCatchingExceptions([&]() {
  doSomething();
});
KJ_IF_MAYBE(e, maybeException) {
  // handle exception
}
```

These wrappers perform some extra bookkeeping:
* `kj::runCatchingExceptions()` will catch any kind of exception, whether it derives from `kj::Exception` or not, and will do its best to convert it into a `kj::Exception`.
* `kj::throwFatalException()` and `kj::throwRecoverableException()` invoke the thread's current `kj::ExceptionCallback` to throw the exception, allowing apps to customize how exceptions are handled. The default `ExceptionCallback` makes sure to throw the exception in such a way that it can be understood and caught by code looking for `std::exception`, such as the C++ library's standard termination handler.
* These helpers also work, to some extent, even when compiled with `-fno-exceptions` -- see below. (Note that "fatal" vs. "recoverable" exceptions are only different in this case; when exceptions are enabled, they are handled the same.)

### Supporting `-fno-exceptions`

KJ strongly recommends using C++ exceptions. However, exceptions are controversial, and many C++ applications are compiled with exceptions disabled. Some KJ-based libraries (especially Cap'n Proto) would like to accommodate such users. To that end, KJ's exception and assertion infrastructure is designed to degrade gracefully when compiled without exception support. In this case, exceptions are split into two types:

* Fatal exceptions, when compiled with `-fno-exceptions`, will terminate the program when thrown.
* Recoverable exceptions, when compiled with `-fno-exceptions`, will be recorded on the side. Control flow then continues normally, possibly using a dummy value or skipping code which cannot execute. Later, the application can check if an exception has been raised and handle it.

`KJ_ASSERT`s (and `KJ_REQUIRE`s) are fatal by default. To make them recoverable, add a "recovery block" after the assert:

```c++
kj::StringPtr getItem(int i) {
  KJ_REQUIRE(i >= 0 && i < items.size()) {
    // This is the recovery block. Recover by simply returning an empty string.
    return "";
  }
  return items[i];
}
```

When the code above is compiled with exceptions enabled, an out-of-bounds index will result in an exception being thrown. But when compiled with `-fno-exceptions`, the function will store the exception off to the side (in KJ), and then return an empty string.

A recovery block can indicate that control flow should continue normally even in case of error by using a `break` statement.

```c++
void incrementBy(int i) {
  KJ_REQUIRE(i >= 0, "negative increments not allowed") {
    // Pretend the caller passed `0` and continue.
    i = 0;
    break;
  }

  value += i;
}
```

**WARNING:** The recovery block is executed even when exceptions are enabled. The exception is thrown upon exit from the block (even if a `return` or `break` statement is present). Therefore, be careful about side effects in the recovery block. Also, note that both GCC and Clang have a longstanding bug where a returned value's destructor is not called if the return is interrupted by an exception being thrown. Therefore, you must not return a value with a non-trivial destructor from a recovery block.

There are two ways to handle recoverable exceptions:

* Use `kj::runCatchingExceptions()`. When compiled with `-fno-exceptions`, this function will arrange for any recoverable exception to be stored off to the side. Upon completion of the given lambda, `kj::runCatchingExceptions()` will return the exception.
* Write a custom `kj::ExceptionCallback`, which can handle exceptions in any way you choose.

Note that while most features of KJ work with `-fno-exceptions`, some of them have not been carefully written for this case, and may trigger fatal exceptions too easily. People relying on this mode will have to tread carefully.

### Exceptions in Destructors

Bugs can occur anywhere -- including in destructors. KJ encourages applications to detect bugs using assertions, which throw exceptions. As a result, exceptions can be thrown in destructors. There is no way around this. You cannot simply declare that destructors shall not have bugs.

Because of this, KJ recommends that all destructors be declared with `noexcept(false)`, in order to negate C++11's unfortunate decision that destructors should be `noexcept` by default.

However, this does not solve C++'s Most Unfortunate Decision, namely that throwing an exception from a destructor that was called during an unwind from another exception always terminates the program. It is very common for exceptions to cause "secondary" exceptions during unwind. For example, the destructor of a buffered stream might check whether the buffer has been flushed, and raise an exception if it has not, reasoning that this is a serious bug that could lead to data loss. But if the program is already unwinding due to some other exception, then it is likely that the failure to flush the buffer is because of that other exception. The "secondary" exception might as well be ignored. Terminating the program is the worst possible response.

To work around the MUD, KJ offers two tools:

First, during unwind from one exception, KJ will handle all "recoverable" exceptions as if compiled with `-fno-exceptions`, described in the previous section. So, whenever writing assertions in destructors, it is a good idea to give them a recovery block like `{break;}` or `{return;}`.

```c++
BufferedStream::~BufferedStream() noexcept(false) {
  KJ_REQUIRE(buffer.size() == 0, "buffer was not flushed; possible data loss") {
    // Don't throw if we're unwinding!
    break;
  }
}
```

Second, `kj::UnwindDetector` can be used to squelch exceptions during unwind. This is especially helpful in cases where your destructor needs to call complex external code that wasn't written with destructors in mind. Use it like so:

```c++
class Transaction {
public:
  // ...

private:
  kj::UnwindDetector unwindDetector;
  // ...
};

Transaction::~Transaction() noexcept(false) {
  unwindDetector.catchExceptionsIfUnwinding([&]() {
    if (!committed) {
      rollback();
    }
  });
}
```

Core Systems
======================================================================

This section describes KJ APIs that control process execution and low-level interactions with the operating system. Most users of KJ will need to be familiar with most of this section.

## Threads and Synchronization

`kj::Thread` creates a thread in which the lambda passed to `kj::Thread`'s constructor will be executed. `kj::Thread`'s destructor waits for the thread to exit before continuing, and rethrows any exception that had been thrown from the thread's main function -- unless the thread's `.detach()` method has been called, in which case `kj::Thread`'s destructor does nothing.

`kj::MutexGuarded<T>` holds an instance of `T` that is protected by a mutex. In order to access the protected value, you must first create a lock. `.lockExclusive()` returns `kj::Locked<T>` which can be used to access the underlying value. `.lockShared()` returns `kj::Locked<const T>`, [using constness to enforce thread-safe read-only access](../style-guide.md#constness) so that multiple threads can take the lock concurrently. In this way, KJ mutexes make it difficult to forget to take a lock before accessing the protected object.

`kj::Locked<T>` has a method `.wait(cond)` which temporarily releases the lock and waits, taking the lock back as soon as `cond(value)` evaluates true. This provides a much cleaner and more readable interface than traditional conditional variables.

`kj::Lazy<T>` is an instance of `T` that is constructed on first access in a thread-safe way.

Macros `KJ_TRACK_LOCK_BLOCKING` and `KJ_SAVE_ACQUIRED_LOCK_INFO` can be used to enable support utilities to implement deadlock detection & analysis.
* `KJ_TRACK_LOCK_BLOCKING`: When the current thread is doing a blocking synchronous KJ operation, that operation is available via `kj::blockedReason()` (intention is for this to be invoked from the signal handler running on the thread that's doing the synchronous operation).
* `KJ_SAVE_ACQUIRED_LOCK_INFO`: When enabled, lock acquisition will save state about the location of the acquired lock. When combined with `KJ_TRACK_LOCK_BLOCKING` this can be particularly helpful because any watchdog can just forward the signal to the thread that's holding the lock.
## Asynchronous Event Loop

### Promises

KJ makes asynchronous programming manageable using an API modeled on E-style Promises. E-style Promises were also the inspiration for JavaScript Promises, so modern JavaScript programmers should find KJ Promises familiar, although there are some important differences.

A `kj::Promise<T>` represents an asynchronous background task that, upon completion, either "resolves" to a value of type `T`, or "rejects" with an exception.

In the simplest case, a `kj::Promise<T>` can be directly constructed from an instance of `T`:

```c++
int i = 123;
kj::Promise<int> promise = i;
```

In this case, the promise is immediately resolved to the given value.

A promise can also immediately reject with an exception:

```c++
kj::Exception e = KJ_EXCEPTION(FAILED, "problem");
kj::Promise<int> promise = kj::mv(e);
```

Of course, `Promise`s are much more interesting when they don't complete immediately.

When a function returns a `Promise`, it means that the function performs some asynchronous operation that will complete in the future. These functions are always non-blocking -- they immediately return a `Promise`. The task completes asynchronously on the event loop. The eventual results of the promise can be obtained using `.then()` to register a callback, or, in certain situations, `.wait()` to synchronously wait. These are described in more detail below.

### Basic event loop setup

In order to execute `Promise`-based code, the thread must be running an event loop. Typically, at the top level of the thread, you would do something like:

```c++
kj::AsyncIoContext io = kj::setupAsyncIo();

kj::AsyncIoProvider& ioProvider = *io.provider;
kj::LowLevelAsyncIoProvider& lowLevelProvider = *io.lowLevelProvider;
kj::WaitScope& waitScope = io.waitScope;
```

`kj::setupAsyncIo()` constructs and returns a bunch of objects:

* A `kj::AsyncIoProvider`, which provides access to a variety of I/O APIs, like timers, pipes, and networking.
* A `kj::LowLevelAsyncIoProvider`, which allows you to wrap existing low-level operating system handles (Unix file descriptors, or Windows `HANDLE`s) in KJ asynchronous interfaces.
* A `kj::WaitScope`, which allows you to perform synchronous waits (see next section).
* OS-specific interfaces for even lower-level access -- see the API definition for more details.

In order to implement all this, KJ will set up the appropriate OS-specific constructs to handle I/O events on the host platform. For example, on Linux, KJ will use `epoll`, whereas on Windows, it will set up an I/O Completion Port.

Sometimes, you may need KJ promises to cooperate with some existing event loop, rather than set up its own. For example, you might be using libuv, or Boost.Asio. Usually, a thread can only have one event loop, because it can only wait on one OS event queue (e.g. `epoll`) at a time. To accommodate this, it is possible (though not easy) to adapt KJ to run on top of some other event loop, by creating a custom implementation of `kj::EventPort`. The details of how to do this are beyond the scope of this document.

Sometimes, you may find that you don't really need to perform operating system I/O at all. For example, a unit test might only need to call some asynchronous functions using mock I/O interfaces, or a thread in a multi-threaded program may only need to exchange events with other threads and not the OS. In these cases, you can create a simple event loop instead:

```c++
kj::EventLoop eventLoop;
kj::WaitScope waitScope(eventLoop);
```

### Synchronous waits

In the top level of your program (or thread), the program is allowed to synchronously wait on a promise using the `kj::WaitScope` (see above).

```
kj::Timer& timer = io.provider->getTimer();
kj::Promise<void> promise = timer.afterDelay(5 * kj::SECONDS);
promise.wait(waitScope);  // returns after 5 seconds' delay
```

`promise.wait()` will run the thread's event loop until the promise completes. It will then return the `Promise`'s result (or throw the `Promise`'s exception). `.wait()` consumes the `Promise`, as if the `Promise` has been moved away.

Synchronous waits cannot be nested -- i.e. a `.then()` callback (see below) that is called by the event loop itself cannot execute another level of synchronous waits. Hence, synchronous waits generally can only be used at the top level of the thread. The API requires passing a `kj::WaitScope` to `.wait()` as a way to demonstrate statically that the caller is allowed to perform synchronous waits. Any function which wishes to perform synchronous waits must take a `kj::WaitScope&` as a parameter to indicate that it does this.

Synchronous waits often make sense to use in "client" programs that only have one task to complete before they exit. On the other end of the spectrum, server programs that handle many clients generally must do everything asynchronously. At the top level of a server program, you will typically instruct the event loop to run forever, like so:

```c++
// Run event loop forever, do everything asynchronously.
kj::NEVER_DONE.wait(waitScope);
```

Libraries should always be asynchronous, so that either kind of program can use them.

### Asynchronous callbacks

Similar to JavaScript promises, you may register a callback to call upon completion of a KJ promise using `.then()`:

```c++
kj::Promise<kj::String> textPromise = stream.readAllText();
kj::Promise<int> lineCountPromise = textPromise
    .then([](kj::String text) {
  int lineCount = 0;
  for (char c: text) {
    if (c == '\n') {
      ++lineCount;
    }
  }
  return lineCount;
});
```

`promise.then()` takes, as its argument, a lambda which transforms the result of the `Promise`. It returns a new `Promise` for the transformed result. We call this lambda a "continuation".

Calling `.then()`, like `.wait()`, consumes the original promise, as if it were "moved away". Ownership of the original promise is transferred into the new, derived promise. If you want to register multiple continuations on the same promise, you must fork it first (see below).

If the continuation itself returns another `Promise`, then the `Promise`s become chained. That is, the final type is reduced from `Promise<Promise<T>>` to just `Promise<T>`.

```c++
kj::Promise<kj::Own<kj::AsyncIoStream>> connectPromise =
    networkAddress.connect();
kj::Promise<kj::String> textPromise = connectPromise
    .then([](kj::Own<kj::AsyncIoStream> stream) {
  return stream->readAllText().attach(kj::mv(stream));
});
```

If a promise rejects (throws an exception), then the exception propagates through `.then()` to the new derived promise, without calling the continuation. If you'd like to actually handle the exception, you may pass a second lambda as the second argument to `.then()`.

```c++
kj::Promise<kj::String> promise = networkAddress.connect()
    .then([](kj::Own<kj::AsyncIoStream> stream) {
  return stream->readAllText().attach(kj::mv(stream));
}, [](kj::Exception&& exception) {
  return kj::str("connection error: ", exception);
});
```

You can also use `.catch_(errorHandler)`, which is a shortcut for `.then(identityFunction, errorHandler)`.

### `kj::evalNow()`, `kj::evalLater()`, and `kj::evalLast()`

These three functions take a lambda as the parameter, and return the result of evaluating the lambda. They differ in when, exactly, the execution happens.

```c++
kj::Promise<int> promise = kj::evalLater([]() {
  int i = doSomething();
  return i;
});
```

As with `.then()` continuations, the lambda passed to these functions may itself return a `Promise`.

`kj::evalNow()` executes the lambda immediately -- before `evalNow()` even returns. The purpose of `evalNow()` is to catch any exceptions thrown and turn them into a rejected promise. This is often a good idea when you don't want the caller to have to handle both synchronous and asynchronous exceptions -- wrapping your whole function in `kj::evalNow()` ensures that all exceptions are delivered asynchronously.

`kj::evalLater()` executes the lambda on a future turn of the event loop. This is equivalent to `kj::Promise<void>().then()`.

`kj::evalLast()` arranges for the lambda to be called only after all other work queued to the event loop has completed (but before querying the OS for new I/O events). This can often be useful e.g. for batching. For example, if a program tends to make many small write()s to a socket in rapid succession, you might want to add a layer that collects the writes into a batch, then sends the whole batch in a single write from an `evalLast()`. This way, none of the bytes are significantly delayed, but they can still be coalesced.

If multiple `evalLast()`s exist at the same time, they will execute in last-in-first-out order. If the first one out schedules more work on the event loop, that work will be completed before the next `evalLast()` executes, and so on.

### Attachments

Often, a task represented by a `Promise` will require that some object remains alive until the `Promise` completes. In particular, under KJ conventions, unless documented otherwise, any class method which returns a `Promise` inherently expects that the caller will ensure that the object it was called on will remain alive until the `Promise` completes (or is canceled). Put another way, member function implementations may assume their `this` pointer is valid as long as their returned `Promise` is alive.

You may use `promise.attach(kj::mv(object))` to give a `Promise` direct ownership of an object that must be kept alive until the promise completes. `.attach()`, like `.then()`, consumes the promise and returns a new one of the same type.

```c++
kj::Promise<kj::Own<kj::AsyncIoStream>> connectPromise =
    networkAddress.connect();
kj::Promise<kj::String> textPromise = connectPromise
    .then([](kj::Own<kj::AsyncIoStream> stream) {
  // We must attach the stream so that it remains alive until `readAllText()`
  // is done. The stream will then be discarded.
  return stream->readAllText().attach(kj::mv(stream));
});
```

Using `.attach()` is semantically equivalent to using `.then()`, passing an identity function as the continuation, while having that function capture ownership of the attached object, i.e.:

```c++
// This...
promise.attach(kj::mv(attachment));
// ...is equivalent to this...
promise.then([a = kj::mv(attachment)](auto x) { return kj::mv(x); });
```

Note that you can use `.attach()` together with `kj::defer()` to construct a "finally" block -- code which will execute after the promise completes (or is canceled).

```c++
promise = promise.attach(kj::defer([]() {
  // This code will execute when the promise completes or is canceled.
}));
```

### Background tasks

If you construct a `Promise` and then just leave it be without calling `.then()` or `.wait()` to consume it, the task it represents will nevertheless execute when the event loop runs, "in the background". You can call `.then()` or `.wait()` later on, when you're ready. This makes it possible to run multiple concurrent tasks at once.

Note that, when possible, KJ evaluates continuations lazily. Continuations which merely transform the result (without returning a new `Promise` that might require more waiting) are only evaluated when the final result is actually needed. This is an optimization which allows a long chain of `.then()`s to be executed all at once, rather than turning the event loop for each one. However, it can lead to some confusion when storing an unconsumed `Promise`. For example:

```c++
kj::Promise<void> promise = timer.afterDelay(5 * kj::SECONDS)
    .then([]() {
  // This log line will never be written, because nothing
  // is waiting on the final result of the promise.
  KJ_LOG(WARNING, "It has been 5 seconds!!!");
});
kj::NEVER_DONE.wait(waitScope);
```

To solve this, use `.eagerlyEvaluate()`:

```c++
kj::Promise<void> promise = timer.afterDelay(5 * kj::SECONDS)
    .then([]() {
  // This log will correctly be written after 5 seconds.
  KJ_LOG(WARNING, "It has been 5 seconds!!!");
}).eagerlyEvaluate([](kj::Exception&& exception) {
  KJ_LOG(ERROR, exception);
});
kj::NEVER_DONE.wait(waitScope);
```

`.eagerlyEvaluate()` takes an error handler callback as its parameter, with the same semantics as `.catch_()` or the second parameter to `.then()`. This is required because otherwise, it is very easy to forget to install an error handler on background tasks, resulting in errors being silently discarded. However, if you are certain that errors will be properly handled elsewhere, you may pass `nullptr` as the parameter to skip error checking -- this is equivalent to passing a callback that merely re-throws the exception.

If you have lots of background tasks, use `kj::TaskSet` to manage them. Any promise added to a `kj::TaskSet` will be run to completion (with eager evaluation), with any exceptions being reported to a provided error handler callback.

### Cancellation

If you destroy a `Promise` before it has completed, any incomplete work will be immediately canceled.

Upon cancellation, no further continuations are executed at all, not even error handlers. Only destructors are executed. Hence, when there is cleanup that must be performed after a task, it is not sufficient to use `.then()` to perform the cleanup in continuations. You must instead use `.attach()` to attach an object whose destructor performs the cleanup (or perhaps `.attach(kj::defer(...))`, as mentioned earlier).

Promise cancellation has proven to be an extremely useful feature of KJ promises which is missing in other async frameworks, such as JavaScript's. However, it places new responsibility on the developer. Just as developers who allow exceptions must design their code to be "exception safe", developers using KJ promises must design their code to be "cancellation safe".

It is especially important to note that once a promise has been canceled, then any references that were received along with the promise may no longer be valid. For example, consider this function:

```
kj::Promise<void> write(kj::ArrayPtr<kj::byte> data);
```

The function receives a pointer to some data owned elsewhere. By KJ convention, the caller must ensure this pointer remains valid until the promise completes _or is canceled_. If the caller decides it needs to free the data early, it may do so as long as it cancels the promise first. This property is important as otherwise it becomes impossible to reason about ownership in complex systems.

This means that the implementation of `write()` must immediately stop using `data` as soon as cancellation occurs. For example, if `data` has been placed in some sort of queue where some other concurrent task takes items from the queue to write them, then it must be ensured that `data` will be removed from that queue upon cancellation. This "queued writes" pattern has historically been a frequent source of bugs in KJ code, to the point where experienced KJ developers now become immediately suspicious of such queuing. The `kj::AsyncOutputStream` interface explicitly prohibits overlapping calls to `write()` specifically so that the implementation need not worry about maintaining queues.

### Promise-Fulfiller Pairs and Adapted Promises

Sometimes, it's difficult to express asynchronous control flow as a simple chain of continuations. For example, imagine a producer-consumer queue, where producers and consumers are executing concurrently on the same event loop. The consumer doesn't directly call the producer, nor vice versa, but the consumer would like to wait for the producer to produce an item for consumption.

For these situations, you may use a `Promise`-`Fulfiller` pair.

```c++
kj::PromiseFulfillerPair<int> paf = kj::newPromiseAndFulfiller<int>();

// Consumer waits for the promise.
paf.promise.then([](int i) { ... });

// Producer calls the fulfiller to fulfill the promise.
paf.fulfiller->fulfill(123);

// Producer can also reject the promise.
paf.fulfiller->reject(KJ_EXCEPTION(FAILED, "something went wrong"));
```

**WARNING! DANGER!** When using promise-fulfiller pairs, it is very easy to forget about both exception propagation and, more importantly, cancellation-safety.

* **Exception-safety:** If your code stops early due to an exception, it may forget to invoke the fulfiller. Upon destroying the fulfiller, the consumer end will receive a generic, unhelpful exception, merely saying that the fulfiller was destroyed unfulfilled. To aid in debugging, you should make sure to catch exceptions and call `fulfiller->reject()` to propagate them.
* **Cancellation-safety:** Either the producer or the consumer task could be canceled, and you must consider how this affects the other end.
    * **Canceled consumer:** If the consumer is canceled, the producer may waste time producing an item that no one is waiting for. Or, worse, if the consumer has provided references to the producer (for example, a buffer into which results should be written), those references may become invalid upon cancellation, but the producer will continue executing, possibly resulting in a use-after-free. To avoid these problems, the producer can call `fulfiller->isWaiting()` to check if the consumer is still waiting -- this method returns false if either the consumer has been canceled, or if the producer has already fulfilled or rejected the promise previously. However, `isWaiting()` requires polling, which is not ideal. For better control, consider using an adapted promise (see below).
    * **Canceled producer:** If the producer is canceled, by default it will probably destroy the fulfiller without fulfilling or reject it. As described previously, the consumer will receive a non-descript exception, which is likely unhelpful for debugging. To avoid this scenario, the producer could perhaps use `.attach(kj::defer(...))` with a lambda that checks `fulfiller->isWaiting()` and rejects it if not.

Because of the complexity of the above issues, it is generally recommended that you **avoid promise-fulfiller pairs** except in cases where these issues very clearly don't matter (such as unit tests).

Instead, when cancellation concerns matter, consider using "adapted promises", a more sophisticated alternative. `kj::newAdaptedPromise<T, Adapter>()` constructs an instance of the class `Adapter` (which you define) encapsulated in a returned `Promise<T>`. `Adapter`'s constructor receives a `kj::PromiseFulfiller<T>&` used to fulfill the promise. The constructor should then register the fulfiller with the desired producer. If the promise is canceled, `Adapter`'s destructor will be invoked, and should un-register the fulfiller. One common technique is for `Adapter` implementations to form a linked list with other `Adapter`s waiting for the same producer. Adapted promises make consumer cancellation much more explicit and easy to handle, at the expense of requiring more code.

### Loops

Promises, due to their construction, don't lend themselves easily to classic `for()`/`while()` loops. Instead, loops should be expressed recursively, as in a functional language. For example:

```c++
kj::Promise<void> boopEvery5Seconds(kj::Timer& timer) {
  return timer.afterDelay(5 * kj::SECONDS).then([&timer]() {
    boop();
    // Loop by recursing.
    return boopEvery5Seconds(timer);
  });
}
```

KJ promises include "tail call optimization" for loops like the one above, so that the promise chain length remains finite no matter how many times the loop iterates.

**WARNING!** It is very easy to accidentally break tail call optimization, creating a memory leak. Consider the following:

```c++
kj::Promise<void> boopEvery5Seconds(kj::Timer& timer) {
  // WARNING! MEMORY LEAK!
  return timer.afterDelay(5 * kj::SECONDS).then([&timer]() {
    boop();
    // Loop by recursing.
    return boopEvery5Seconds(timer);
  }).catch_([](kj::Exception&& exception) {
    // Oh no, an error! Log it and end the loop.
    KJ_LOG(ERROR, exception);
    kj::throwFatalException(kj::mv(exception));
  });
}
```

The problem in this example is that the recursive call is _not_ a tail call, due to the `.catch_()` appended to the end. Every time around the loop, a new `.catch_()` is added to the promise chain. If an exception were thrown, that exception would end up being logged many times -- once for each time the loop has repeated so far. Or if the loop iterated enough times, and the top promise was then canceled, the chain could be so long that the destructors overflow the stack.

In this case, the best fix is to pull the `.catch_()` out of the loop entirely:

```c++
kj::Promise<void> boopEvery5Seconds(kj::Timer& timer) {
  return boopEvery5SecondsLoop(timer)
      .catch_([](kj::Exception&& exception) {
    // Oh no, an error! Log it and end the loop.
    KJ_LOG(ERROR, exception);
    kj::throwFatalException(kj::mv(exception));
  })
}

kj::Promise<void> boopEvery5SecondsLoop(kj::Timer& timer) {
  // No memory leaks now!
  return timer.afterDelay(5 * kj::SECONDS).then([&timer]() {
    boop();
    // Loop by recursing.
    return boopEvery5SecondsLoop(timer);
  });
}
```

Another possible fix would be to make sure the recursive continuation and the error handler are passed to the same `.then()` invocation:

```c++
kj::Promise<void> boopEvery5Seconds(kj::Timer& timer) {
  // No more memory leaks, but hard to reason about.
  return timer.afterDelay(5 * kj::SECONDS).then([&timer]() {
    boop();
  }).then([&timer]() {
    // Loop by recursing.
    return boopEvery5Seconds(timer);
  }, [](kj::Exception&& exception) {
    // Oh no, an error! Log it and end the loop.
    KJ_LOG(ERROR, exception);
    kj::throwFatalException(kj::mv(exception));
  });
}
```

Notice that in this second case, the error handler is scoped so that it does _not_ catch exceptions thrown by the recursive call; it only catches exceptions from `boop()`. This solves the problem, but it's a bit trickier to understand and to ensure that exceptions can't accidentally slip past the error handler.

### Forking and splitting promises

As mentioned above, `.then()` and similar functions consume the promise on which they are called, so they can only be called once. But what if you want to start multiple tasks using the result of a promise? You could solve this in a convoluted way using adapted promises, but KJ has a built-in solution: `.fork()`

```c++
kj::Promise<int> promise = ...;
kj::ForkedPromise<int> forked = promise.fork();
kj::Promise<int> branch1 = promise.addBranch();
kj::Promise<int> branch2 = promise.addBranch();
kj::Promise<int> branch3 = promise.addBranch();
```

A forked promise can have any number of "branches" which represent different consumers waiting for the same result.

Forked promises use reference counting. The `ForkedPromise` itself, and each branch created from it, each represent a reference to the original promise. The original promise will only be canceled if all branches are canceled and the `ForkedPromise` itself is destroyed.

Forked promises require that the result type has a copy constructor, so that it can be copied to each branch. (Regular promises only require the result type to be movable, not copyable.) Or, alternatively, if the result type is `kj::Own<T>` -- which is never copyable -- then `T` must have a method `kj::Own<T> T::addRef()`; this method will be invoked to create each branch. Typically, `addRef()` would be implemented using reference counting.

Sometimes, the copyable requirement of `.fork()` can be burdensome and unnecessary. If the result type has multiple components, and each branch really only needs one of the components, then being able to copy (or refcount) is unnecessary. In these cases, you can use `.split()` instead. `.split()` converts a promise for a `kj::Tuple` into a `kj::Tuple` of promises. That is:

```c++
kj::Promise<kj::Tuple<kj::Own<Foo>, kj::String>> promise = ...;
kj::Tuple<kj::Promise<kj::Own<Foo>>, kj::Promise<kj::String>> promises = promise.split();
```

### Joining promises

The opposite of forking promises is joining promises. There are two types of joins:
* **Exclusive** joins wait for any one input promise to complete, then cancel the rest, returning the result of the promise that completed.
* **Inclusive** joins wait for all input promises to complete, and render all of the results.

For an exclusive join, use `promise.exclusiveJoin(kj::mv(otherPromise))`. The two promises must return the same type. The result is a promise that returns whichever result is produced first, and cancels the other promise at that time. (To exclusively join more than two promises, call `.exclusiveJoin()` multiple times in a chain.)

To perform an inclusive join, use `kj::joinPromises()` or `kj::joinPromisesFailFast()`. These turn a `kj::Array<kj::Promise<T>>` into a `kj::Promise<kj::Array<T>>`. However, note that `kj::joinPromises()` has a couple common gotchas:
* Trailing continuations on the promises passed to `kj::joinPromises()` are evaluated lazily after all the promises become ready. Use `.eagerlyEvaluate()` on each one to force trailing continuations to happen eagerly. (See earlier discussion under "Background Tasks".)
* If any promise in the array rejects, the exception will be held until all other promises have completed (or rejected), and only then will the exception propagate. In practice we've found that most uses of `kj::joinPromises()` would prefer "exclusive" or "fail-fast" behavior in the case of an exception.

`kj::joinPromisesFailFast()` addresses the gotchas described above: promise continuations are evaluated eagerly, and if any promise results in an exception, the join promise is immediately rejected with that exception.

### Threads

The KJ async framework is designed around single-threaded event loops. However, you can have multiple threads, with each running its own loop.

All KJ async objects, unless specifically documented otherwise, are intrinsically tied to the thread and event loop on which they were created. These objects must not be accessed from any other thread.

To communicate between threads, you may use `kj::Executor`. Each thread (that has an event loop) may call `kj::getCurrentThreadExecutor()` to get a reference to its own `Executor`. That reference may then be shared with other threads. The other threads can use the methods of `Executor` to queue functions to execute on the owning thread's event loop.

The threads which call an `Executor` do not have to have KJ event loops themselves. Thus, you can use an `Executor` to signal a KJ event loop thread from a non-KJ thread.

### Fibers

Fibers allow code to be written in a synchronous / blocking style while running inside the KJ event loop, by executing the code on an alternate call stack. The code running on this alternate stack is given a special `kj::WaitScope&`, which it can pass to `promise.wait()` to perform synchronous waits. When such a `.wait()` is invoked, the thread switches back to the main call stack and continues running the event loop there. When the waited promise resolves, execution switches back to the alternate call stack and `.wait()` returns (or throws).

```c++
constexpr size_t STACK_SIZE = 65536;
kj::Promise<int> promise =
    kj::startFiber(STACK_SIZE, [](kj::WaitScope& waitScope) {
  int i = someAsyncFunc().wait(waitScope);
  i += anotherAsyncFunc().wait(waitScope);
  return i;
});
```

**CAUTION:** Fibers produce attractive-looking code, but have serious drawbacks. Every fiber must allocate a new call stack, which is typically rather large. The above example allocates a 64kb stack, which is the _minimum_ supported size. Some programs and libraries expect to be able to allocate megabytes of data on the stack. On modern Linux systems, a default stack size of 8MB is typical. Stack space is allocated lazily on page faults, but just setting up the memory mapping is much more expensive than a typical `malloc()`. If you create lots of fibers, you should use `kj::FiberPool` to reduce allocation costs -- but while this reduces allocation overhead, it will increase memory usage.

Because of this, fibers should not be used just to make code look nice (C++20's `co_await`, described below, is a better way to do that). Instead, the main use case for fibers is to be able to call into existing libraries that are not designed to operate in an asynchronous way. For example, say you find a library that performs stream I/O, and lets you provide your own `read()`/`write()` implementations, but expects those implementations to operate in a blocking fashion. With fibers, you can use such a library within the asynchronous KJ event loop.

### Coroutines

C++20 brings us coroutines, which, like fibers, allow code to be written in a synchronous / blocking style while running inside the KJ event loop. Coroutines accomplish this with a different strategy than fibers: instead of running code on an alternate stack and switching stacks on suspension, coroutines save local variables and temporary objects in a heap-allocated "coroutine frame" and always unwind the stack on suspension.

A C++ function is a KJ coroutine if it follows these two rules:
- The function returns a `kj::Promise<T>`.
- The function uses a `co_await` or `co_return` keyword in its implementation.

```c++
kj::Promise<int> aCoroutine() {
  int i = co_await someAsyncFunc();
  i += co_await anotherAsyncFunc();
  co_return i;
});

// Call like any regular promise-returning function.
auto promise = aCoroutine();
```

The promise returned by a coroutine owns the coroutine frame. If you destroy the promise, any objects alive in the frame will be destroyed, and the frame freed, thus cancellation works exactly as you'd expect.

There are some caveats one should be aware of while writing coroutines:
- Lambda captures **do not** live inside of the coroutine frame, meaning lambda objects must outlive any coroutine Promises they return, or else the coroutine will encounter dangling references to captured objects. This is a defect in the C++ standard: https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#Rcoro-capture. To safely use a capturing lambda as a coroutine, first wrap it using `kj::coCapture([captures]() { ... })`, then invoke that object.
- Holding a mutex lock across a `co_await` is almost always a bad idea, with essentially the same problems as holding a lock while calling `promise.wait(waitScope)`. This would cause the coroutine to hold the lock for however many turns of the event loop is required to drive the coroutine to release the lock; if I/O is involved, this could cause significant problems. Additionally, a reentrant call to the coroutine on the same thread would deadlock. Instead, if a coroutine must temporarily hold a lock, always keep the lock in a new lexical scope without any `co_await`.
- Attempting to define (and use) a variable-length array will cause a compile error, because the size of coroutine frames must be knowable at compile-time. The error message that clang emits for this, "Coroutines cannot handle non static allocas yet", suggests this may be relaxed in the future.

As of this writing, KJ supports C++20 coroutines and Coroutines TS coroutines, the latter being an experimental precursor to C++20 coroutines. They are functionally the same thing, but enabled with different compiler/linker flags:

- Enable C++20 coroutines by requesting that language standard from your compiler.
- Enable Coroutines TS coroutines with `-fcoroutines-ts` in C++17 clang, and `/await` in MSVC.

KJ prefers C++20 coroutines when both implementations are available.

### Unit testing tips

When unit-testing promise APIs, two tricky challenges frequently arise:

* Testing that a promise has completed when it is supposed to. You can use `promise.wait()`, but if the promise has not completed as expected, then the test may simply hang. This can be frustrating to debug.
* Testing that a promise has not completed prematurely. You obviously can't use `promise.wait()`, because you _expect_ the promise has not completed, and therefore this would hang. You might try using `.then()` with a continuation that sets a flag, but if the flag is not set, it's hard to tell whether this is because the promise really has not completed, or merely because the event loop hasn't yet called the `.then()` continuation.

To solve these problems, you can use `promise.poll(waitScope)`. This function runs the event loop until either the promise completes, or there is nothing left to do except to wait. This includes running any continuations in the queue as well as checking for I/O events from the operating system, repeatedly, until nothing is left. The only thing `.poll()` will not do is block. `.poll()` returns true if the promise has completed, false if it hasn't.

```c++
// In a unit test...
kj::Promise<void> promise = waitForBoop();

// The promise should not be done yet because we haven't booped yet.
KJ_ASSERT(!promise.poll(waitScope));

boop();

// Assert the promise is done, to make sure wait() won't hang!
KJ_ASSERT(promise.poll(waitScope));

promise.wait(waitScope);
```

Sometimes, you may need to ensure that some promise has completed that you don't have a reference to, so you can observe that some side effect has occurred. You can use `waitScope.poll()` to flush the event loop without waiting for a specific promise to complete.

## System I/O

### Async I/O

On top of KJ's async framework (described earlier), KJ provides asynchronous APIs for byte streams, networking, and timers.

As mentioned previously, `kj::setupAsyncIo()` allocates an appropriate OS-specific event queue (such as `epoll` on Linux), returning implementations of `kj::AsyncIoProvider` and `kj::LowLevelAsyncIoProvider` implemented in terms of that queue. `kj::AsyncIoProvider` provides an OS-independent API for byte streams, networking, and timers. `kj::LowLevelAsyncIoProvider` allows native OS handles (file descriptors on Unix, `HANDLE`s on Windows) to be wrapped in KJ byte stream APIs, like `kj::AsyncIoStream`.

Please refer to the API reference (the header files) for details on these APIs.

### Synchronous I/O

Although most complex KJ applications use async I/O, sometimes you want something a little simpler.

`kj/io.h` provides some more basic, synchronous streaming interfaces, like `kj::InputStream` and `kj::OutputStream`. Implementations are provided on top of file descriptors and Windows `HANDLE`s.

Additionally, the important utility class `kj::AutoCloseFd` (and `kj::AutoCloseHandle` for Windows) can be found here. This is an RAII wrapper around a file descriptor (or `HANDLE`), which you will likely want to use any time you are manipulating raw file descriptors (or `HANDLE`s) in KJ code.

### Filesystem

KJ provides an advanced, cross-platform filesystem API in `kj/filesystem.h`. Features include:

* Paths represented using `kj::Path`. In addition to providing common-sense path parsing and manipulation functions, this class is designed to defend against path injection attacks.
* All interfaces are abstract, allowing multiple implementations.
* An in-memory implementation is provided, useful in particular for mocking the filesystem in unit tests.
* On Unix, disk `kj::Directory` objects are backed by open file descriptors and use the `openat()` family of system calls.
* Makes it easy to use atomic replacement when writing new files -- and even whole directories.
* Symlinks, hard links, listing directories, recursive delete, recursive create parents, recursive copy directory, memory mapping, and unnamed temporary files are all exposed and easy to use.
* Sparse files ("hole punching"), copy-on-write file cloning (`FICLONE`, `FICLONERANGE`), `sendfile()`-based copying, `renameat2()` atomic replacements, and more will automatically be used when available.

See the API reference (header file) for details.

### Clocks and time

KJ provides a time library in `kj/time.h` which uses the type system to enforce unit safety.

`kj::Duration` represents a length of time, such as a number of seconds. Multiply an integer by `kj::SECONDS`, `kj::MINUTES`, `kj::NANOSECONDS`, etc. to get a `kj::Duration` value. Divide by the appropriate constant to get an integer.

`kj::Date` represents a point in time in the real world. `kj::UNIX_EPOCH` represents January 1st, 1970, 00:00 UTC. Other dates can be constructed by adding a `kj::Duration` to `kj::UNIX_EPOCH`. Taking the difference between to `kj::Date`s produces a `kj::Duration`.

`kj::TimePoint` represents a time point measured against an unspecified origin time. This is typically used with monotonic clocks that don't necessarily reflect calendar time. Unlike `kj::Date`, there is no implicit guarantee that two `kj::TimePoint`s are measured against the same origin and are therefore comparable; it is up to the application to track which clock any particular `kj::TimePoint` came from.

`kj::Clock` is a simple interface whose `now()` method returns the current `kj::Date`. `kj::MonotonicClock` is a similar interface returning a `kj::TimePoint`, but with the guarantee that times returned always increase (whereas a `kj::Clock` might go "back in time" if the user manually modifies their system clock).

`kj::systemCoarseCalendarClock()`, `kj::systemPreciseCalendarClock()`, `kj::systemCoarseMonotonicClock()`, `kj::systemPreciseMonotonicClock()` are global functions that return implementations of `kj::Clock` or `kJ::MonotonicClock` based on system time.

`kj::Timer` provides an async (promise-based) interface to wait for a specified time to pass. A `kj::Timer` is provided via `kj::AsyncIoProvider`, constructed using `kj::setupAsyncIo()` (see earlier discussion on async I/O).

## Program Harness

TODO: kj::Main, unit test framework

Libraries
======================================================================

TODO: parser combinator framework, HTTP, TLS, URL, encoding, JSON
