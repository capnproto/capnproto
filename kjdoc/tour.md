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

`kj::dynamicDowncastIfAvailable<T>(value)` is like `dynamic_cast<T*>(value)` with two differences. First, it returns `kj::Maybe<T&>` instead of `T*`. Second, if the program is compiled without RTTI enabled, the function always returns null. This function is intended to be used to implement optimizations, where the code can do something smarter if `value` happens to be of some specific type -- but if RTTI is not available, it is safe to skip the optimization. See [KJ idiomatic use of dynamic_cast](style-guide.md#dynamic_cast) for more background.

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

KJ style makes heavy use of [RAII](style-guide.md#raii-resource-acquisition-is-initialization). KJ-based code should never use `new` and `delete` directly. Instead, use the utilities in this section to manage memory in a RAII way.

### Owned pointers, heap allocation, and disposers

`kj::Own<T>` is a pointer to a value of type `T` which is "owned" by the holder. When a `kj::Own` goes out-of-scope, the value it points to will (typically) be destroyed and freed.

`kj::Own` has move semantics. Thus, when used as a function parameter or return type, `kj::Own` indicates that ownership of the object is being transferred.

`kj::heap<T>(args...)` allocates an object of type `T` on the heap, passing `args...` to its constructor, and returns a `kj::Own<T>`. This is the most common way to create owned objects.

However, a `kj::Own` does not necessarily refer to a heap object. A `kj::Own` is actually implemented as a pair of a pointer to the object, and a pointer to a `kj::Disposer` object that knows how to destroy it; `kj::Own`'s destructor invokes the disposer. `kj::Disposer` is an abstract interface with many implementations. `kj::heap` uses an implementation that invokes the object's destructor then frees its underlying space from the heap (like `delete` does), but other implementations exist. Alternative disposers allow an application to control memory allocation more precisely when desired.

Some example uses of disposers include:

* `kj::fakeOwn(ref)` returns a `kj::Own` that points to `ref` but doesn't actually destroy it. This is useful when you know for sure that `ref` will outlive the scope of the `kj::Own`, and therefore heap allocation is unnecessary. This is common in cases where, for example, the `kj::Own` is being passed into an object which itself will be destroyed before `ref` becomes invalid. It also makes sense when `ref` is actually a static value or global that lives forever.
* `kj::refcounted<T>(args...)` allocates a `T` which uses reference counting. It returns a `kj::Own<T>` that represents one reference to the object. Additional references can be created by calling `kj::addRef(*ptr)`. The object is destroyed when no more `kj::Own`s exist pointing at it. Note that `T` must be a subclass of `kj::Refcounted`. If references may be shared across threads, then atomic refcounting must be used; use `kj::atomicRefcounted<T>(args...)` and inherit `kj::AtomicRefcounted`. Reference counting should be using sparingly; see [KJ idioms around reference counting](style-guide.md#reference-counting) for a discussion of when it should be used and why it is designed the way it is.
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

`kj::ConstFunction` is like `kj::Function`, but is used to indicate that the function should be safe to call from multiple threads. (See [KJ idioms around constness and thread-safety](style-guide.md#constness).)

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

KJ believes that there is no such thing as bug-free code. Instead, we must expect that our code will go wrong, and try to extract as much information as possible when it does. To that end, KJ provides powerful assertion macros designed for observability. (Be sure also to read about [KJ's exception philosohpy](style-guide.md#exceptions); this section describes the actual APIs involved.)

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

As described in [KJ's exception philosohpy](style-guide.md#exceptions), KJ supports a small set of exception types. Regular assertions throw `FAILED` exceptions. `KJ_SYSCALL` usually throws `FAILED`, but identifies certain error codes as `DISCONNECTED` or `OVERLOADED`. For example, `ECONNRESET` is clearly a `DISCONNECTED` exception.

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

`kj::MutexGuarded<T>` holds an instance of `T` that is protected by a mutex. In order to access the protected value, you must first create a lock. `.lockExclusive()` returns `kj::Locked<T>` which can be used to access the underlying value. `.lockShared()` returns `kj::Locked<const T>`, [using constness to enforce thread-safe read-only access](style-guide.md#constness) so that multiple threads can take the lock concurrently. In this way, KJ mutexes make it difficult to forget to take a lock before accessing the protected object.

`kj::Locked<T>` has a method `.wait(cond)` which temporarily releases the lock and waits, taking the lock back as soon as `cond(value)` evaluates true. This provides a much cleaner and more readable interface than traditional conditional variables.

`kj::Lazy<T>` is an instance of `T` that is constructed on first access in a thread-safe way.

## Asynchronous Event Loop

### Promises

KJ makes asynchronous programming manageable using an API modeled on E-style Promises. E-style Promises were also the inspiration for JavaScript Promises, so modern JavaScript programmers should find KJ Promises familiar, although there are some important differences.

A `kj::Promise<T>` represents an asynchronous background task that, upon completion, will either "resolve" to a value of type `T`, or "reject" with an exception.

TODO: Many details.

## System I/O

TODO: async-io, etc.

## Program Harness

TODO: kj::Main, unit test framework

Libraries
======================================================================

TODO: parser combinator framework, HTTP, TLS, URL, encoding, JSON
