# KJ Style

This document describes how to write C++ code in KJ style. It may be compared to the [Google C++ Style Guide](http://google-styleguide.googlecode.com/svn/trunk/cppguide.html).

KJ style is used by KJ (obviously), [Cap'n Proto](https://capnproto.org), [Sandstorm.io](https://sandstorm.io), and possibly other projects. When submitting code to these projects, you should follow this guide.

**Table of Contents**

- [Rule #1: There are no rules](#rule-1-there-are-no-rules)
- [Design Philosophy](#design-philosophy)
  - [Value Types vs. Resource Types](#value-types-vs-resource-types)
  - [RAII (Resource Acquisition Is Initialization)](#raii-resource-acquisition-is-initialization)
  - [Ownership](#ownership)
  - [No Singletons](#no-singletons)
  - [Exceptions](#exceptions)
  - [Threads vs. Event Loops](#threads-vs-event-loops)
  - [Lazy input validation](#lazy-input-validation)
  - [Premature optimization fallacy](#premature-optimization-fallacy)
  - [Text is always UTF-8](#text-is-always-utf-8)
- [C++ usage](#c-usage)
  - [Use C++11 (or later)](#use-c11-or-later)
  - [Heap allocation](#heap-allocation)
  - [Pointers, references](#pointers-references)
  - [Constness](#constness)
  - [Inheritance](#inheritance)
  - [Exceptions Usage](#exceptions-usage)
  - [Template Metaprogramming](#template-metaprogramming)
  - [Global Constructors](#global-constructors)
  - [`dynamic_cast`](#dynamic_cast)
  - [Use of Standard libraries](#use-of-standard-libraries)
  - [Compiler warnings](#compiler-warnings)
  - [Tools](#tools)
- [Irrelevant formatting rules](#irrelevant-formatting-rules)
  - [Naming](#naming)
  - [Spacing and bracing](#spacing-and-bracing)
  - [Comments](#comments)
  - [File templates](#file-templates)

## Rule #1: There are no rules

This guide contains suggestions, not rules.

If you wish to submit code to a project following KJ style, you should follow the guide so long as there is no good reason not to. You should not break rules just because you feel like it -- consistency is important for future maintainability. But, if you have a good, pragmatic reason to break a rule, do it. Do not ask permission. Just do it.

## Design Philosophy

This section contains guidelines on software design that aren't necessarily C++-specific (though KJ's preferences here are obviously influenced by C++).

### Value Types vs. Resource Types

There are two kinds of types: values and resources. Value types are simple data structures; they serve no purpose except to represent pure data. Resource types represent live objects with state and behavior, and often represent resources external to the program.

* Value types make sense to copy (though they don't necessarily have copy constructors). Resource types are not copyable.
* Value types always have move constructors (and sometimes copy constructors). Resource types are not movable; if ownership transfer is needed, the resource must be allocated on the heap.
* Value types almost always have implicit destructors. Resource types may have an explicit destructor.
* Value types should only be compared by value, not identity. Resource types can only be compared by identity.
* Value types make sense to serialize. Resource types fundamentally cannot be serialized.
* Value types rarely use inheritance and never have virtual methods. Resource types commonly do.
* Value types generally use templates for polymorphism. Resource types generally use virtual methods / abstract interfaces.
* You might even say that value types are used in functional programming style while resource types are used in object-oriented style.

In Cap'n Proto there is a very clear distinction between values and resources: interfaces are resource types whereas everything else is a value.

### RAII (Resource Acquisition Is Initialization)

KJ code is RAII-strict. Whenever it is the case that "this block of code cannot exit cleanly without performing operation X", then X *must* be performed in a destructor, so that X will happen regardless of how the block is exited (including by exception).

Use the macros `KJ_DEFER`, `KJ_ON_SCOPE_SUCCESS`, and `KJ_ON_SCOPE_FAILURE` to easily specify some code that must be executed on exit from the current scope, without the need to define a whole class with a destructor.

Be careful when writing complicated destructors. If a destructor performs multiple cleanup actions, you generally need to make sure that the latter actions occur even if the former ones throw an exception. For this reason, a destructor should generally perform no more than one cleanup action. If you need to clean up multiple things, have your class contain multiple members representing the different things that need cleanup, each with its own destructor. This way, if one member's destructor throws, the others still run.

### Ownership

Every object has an "owner". The owner may be another object, or it may be a stack frame (which is in turn owned by its parent stack frame, and so on up to the top frame, which is owned by the thread, which itself is an object which is owned by something).

The owner decides when to destroy an object. If the owner itself is destroyed, everything it owns must be transitively destroyed. This should be accomplished through RAII style.

The owner specifies the lifetime of the object and how the object may be accessed. This specification may be through documented convention or actually enforced through the type system; the latter is preferred when possible.

An object can never own itself, including transitively.

When declaring a pointer to an object which is owned by the scope, always use `kj::Own<T>`. Regular C++ pointers and references always point to objects that are *not* owned.

When passing a regular C++ pointer or reference as a parameter or return value of a function, care must be taken to document assumptions about the lifetime of the object. In the absence of documentation, make the following assumptions:

* A pointer or reference passed as a constructor parameter must remain valid for the lifetime of the constructed object.
* A pointer or reference passed as a function or method parameter must remain valid until the function returns. In the case that the function returns a promise, then the object must remain live until the promise completes or is canceled.
* A pointer or reference returned by a method remains valid at least until the object whose method was called is destroyed.
* A pointer or reference returned by a stand-alone function likely refers to content of one of the function's parameters, and remains valid until that parameter is destroyed.

Note that ownership isn't just about memory management -- it matters even in languages that implement garbage collection! Unless an object is 100% immutable, you need to keep track of who is allowed to modify it, and that generally requires declaring an owner. Moreover, even with GC, resource types commonly need `close()` method that acts very much like a C++ destructor, leading to all the same considerations. It is therefore completely wrong to believe garbage collection absolves you of thinking about ownership -- and this misconception commonly leads to huge problems in large-scale systems written in GC languages.

#### Reference Counting

Reference counting is allowed, in which case an object will have multiple owners.

When using reference counting, care must be taken to ensure that there is a clear contract between all owners about how the object shall be accessed. In general, this should mean one of the following:

* Reference-counted value types should be immutable.
* Reference-counted resource types should have an interface which clearly specifies how multiple clients should coordinate.

Care must also be taken to avoid cyclic references (which would constitute self-ownership, and would cause a memory leak). Think carefully about what the object ownership graph looks like.

Avoid reference counting when it is not absolutely necessary.

Keep in mind that atomic (thread-safe) reference counting can be extremely slow. Consider non-atomic reference counting if it is feasible under your threading philosophy (under KJ's philosophy, non-atomic reference counting is OK).

### No Singletons

A "singleton" is any mutable object or value that is globally accessible. "Globally accessible" means that the object is declared as a global variable or static member variable, or that the object can be found by following pointers from such variables.

Never use singletons. Singletons cause invisible and unexpected dependencies between components of your software that appear unrelated. Worse, the assumption that "there should only be one of this object per process" is almost always wrong, but its wrongness only becomes apparent after so much code uses the singleton that it is infeasible to change. Singleton interfaces often turn into unusable monstrosities in an attempt to work around the fact that they should never have been a singleton in the first place.

See ["Singletons Considered Harmful"](http://www.object-oriented-security.org/lets-argue/singletons) for a complete discussion.

#### Global registries are singletons

An all-too-common-pattern in modular frameworks is to design a way to register named components via global-scope macros. For example:

    // BAD BAD BAD
    REGISTER_PLUGIN("foo", fooEntryPoint);

This global registry is a singleton, and has many of the same problems as singletons. Don't do this. Again, see ["Singletons Considered Harmful"](http://www.object-oriented-security.org/lets-argue/singletons) for discussion.

#### What to do instead

High-level code (such as your `main()` function) should explicitly initialize the components the program needs. If component Foo depends on component Bar, then Foo's constructor should take a pointer to Bar as a parameter; the high-level code can then point each component at its dependencies explicitly.

For example, instead of a global registry, have high-level code construct a registry object and explicitly call some `register()` method to register each component that should be available through it. This way, when you read your `main()` function it's easy to see what components your program is using.

#### Working around OS singletons

Unfortunately, operating system APIs are traditionally singleton-heavy. The most obvious example is, of course, the filesystem.

In order to use these APIs while avoiding the problems of singletons, try to encapsulate OS singletons inside non-singleton interfaces as early on as possible in your program. For example, you might define an abstract interface called `Directory` with an implementation `DiskDirectory` representing a directory on disk. In your `main()` function, create two `DiskDirectory`s representing the root directory and the current working directory. From then on, have all of your code operate in terms of `Directory`. Pass the original `DiskDirectory` pointers into the components that need it.

### Exceptions

An exception represents something that "should never happen", assuming everything is working as expected. Of course, things that "should never happen" in fact happen all the time. But, a program should never be written in such a way that it _expects_ an exception under normal circumstances.

Put another way, exceptions are a way to achieve _fault tolerance_. Throwing an exception is a less-disruptive alternative to aborting the process. Exceptions are a _logistical_ construct, as opposed to a semantic one: an exception should never be part of your "business logic".

For example, exceptions may indicate conditions like:

* Logistics of software development:
  * There is a bug in the code.
  * The requested method is not implemented.
* Logistics of software usage:
  * There is an error in the program's configuration.
  * The input is invalid.
* Logistics of distributed systems:
  * A network connection was reset.
  * An optimistic transaction was aborted due to concurrent modification.
* Logistics of physical computation:
  * The system's resources are exhausted (e.g. out of memory, out of disk space).
  * The system is overloaded and must reject some requests to avoid long queues.

#### Business logic should never catch

If you find that callers of your interface need to catch and handle certain kinds of exceptions in order to operate correctly, then you must change your interface (or overload it) such that those conditions can be handled without an exception ever being thrown. For example, if you have a method `Own<File> open(StringPtr name)` that opens a file, you may also want to offer `Maybe<Own<File>> openIfExists(StringPtr name)` that returns null rather than throwing an exception if the file is not found. (But you should probably keep `open()` as well, for the convenience of the common case where the caller will just throw an exception anyway.)

Note that with this exception philosophy, Java-style "checked exceptions" (exceptions which are explicitly declared to be thrown by an interface) make no sense.

#### How to handle an exception

In framework and logistical code, you may catch exceptions and try to handle them. Given the nature of exceptions, though, there are only a few things that are reasonable to do when receiving an exception:

* On network disconnect or transaction failures, back up and start over from the beginning (restore connections and state, redo operations).
* On resources exhausted / overloaded, retry again later, with exponential back-off.
* On unimplemented methods, retry with a different implementation strategy, if there is one.
* When no better option is available, report the problem to a human (the user and/or the developer).

#### Exceptions can happen anywhere (including destructors)

Any piece of code may contain a bug. Therefore, an exception can happen anywhere. This includes destructors. It doesn't matter how much you argue that destructors should not throw exceptions, because that is equivalent to arguing that code should not have bugs. We all wish our code never had bugs, but nevertheless it happens.

Unfortunately, C++ made the awful decision that an exception thrown from a destructor that itself is called during stack unwind due to some other exception should cause the process to abort. This is an error in the language specification. Apparently, the committee could not agree on any other behavior, so they chose the worst possible behavior.

If exceptions are merely a means to fault tolerance, then it is perfectly clear what should happen in the case that a second exception is thrown while unwinding due to a first: the second exception should merely be discarded, or perhaps attached to the first as a supplementary note. The catching code usually does not care about the exception details anyway; it's just going to report that something went wrong, then maybe try to continue executing other, unrelated parts of the program. In fact, in most cases discarding the secondary exception makes sense, because it is often simply a side-effect of the fact that the code didn't complete normally, and so provides no useful additional information.

Alas, C++ is what it is. So, in KJ, we work around the problem in a couple ways:

* `kj::UnwindDetector` may be used to detect when a destructor is called during unwind and squelch secondary exceptions.
* The `KJ_ASSERT` family of macros -- from which most exceptions are thrown in the first place -- implement a concept of "recoverable" exceptions, where it is safe to continue execution without throwing in cases where throwing would be bad. Assert macros in destructors must always be recoverable.

#### Allowing `-fno-exceptions`

KJ and Cap'n Proto are designed to function even when compiled with `-fno-exceptions`. In this case, throwing an exception behaves differently depending on whether the exception is "fatal" or "recoverable". Fatal exceptions abort the process. On a recoverable exception, on the other hand, execution continues normally, perhaps after replacing invalid data with some safe default. The exception itself is stored in a thread-local variable where code up the stack can check for it later on.

This compromise is made only so that C++ applications which eschew exceptions are still able to use Cap'n Proto. We do NOT recommend disabling exceptions if you have a choice. Moreover, code following this style guide (other than KJ and Cap'n Proto) is not required to be `-fno-exceptions`-safe, and in fact we recommend against it.

### Threads vs. Event Loops

Threads are hard, and synchronization between threads is slow. Even "lock-free" data structures usually require atomic operations, which are costly, and such algorithms are notoriously difficult to get right. Fine-grained synchronization will therefore be expensive at best and highly unstable at worst.

KJ instead prefers event loop concurrency. In this model, each event callback is effectively a transaction; it does not need to worry about concurrent modification within the body of the function.

Multiple threads may exist, but each one has its own event loop and is treated as sort of a lightweight process with shared memory. Every object in the process either belongs to a specific thread (who is allowed to read and modify it) or is transitively immutable (in which case all threads can safely read it concurrently). Threads communicate through asynchronous message-passing. In fact, the only big difference between KJ-style threads compared to using separate processes is that threads may transfer ownership of in-memory objects as part of a message send.

Note that with hardware transactional memory, it may become possible to execute a single event loop across multiple CPU cores while behaving equivalently to a single thread, by executing each event callback as a hardware transaction. If so, this will be implemented as part of KJ's event loop machinery, transparently to apps.

### Lazy input validation

As we all know, you should always validate your input.

But, when should you validate it? There are two plausible answers:

* Upfront, on receipt.
* Lazily, on use.

Upfront validation occasionally makes sense for the purpose of easier debugging of problems: if an error is reported earlier, it's easier to find where it came from.

However, upfront validation has some big problems.

* It is inefficient, as it requires a redundant pass over the data. Lazy validation, in contrast, occurs at a time when you have already loaded the data for the purpose of using it. Extra passes are often cache-unfriendly and/or entail redundant I/O operations.

* It encourages people to skip validation at time of use, on the assumption that it was already validated earlier. This is dangerous, as it entails a non-local assumption. E.g. are you really sure that there is no way to insert data into your database without having validated it? Are you really sure that the data hasn't been corrupted? Are you really sure that your code will never be called in a new situation where validation hasn't happened? Are you sure the data cannot have been modified between validation and use? In practice, you should be validating your input at time of use _even if_ you know it has already been checked previously.

* The biggest problem: Upfront validation tends not to match actual usage, because the validation site is far away from the usage site. Over time, as the usage code changes, the validator can easily get out-of-sync. Note that this could mean the code itself is out-of-sync, or it could be that running servers are out-of-sync, because they have different update schedules. Or, the validator may be written with incorrect assumptions in the first place. The consequences of this can be severe. Protocol Buffers' concept of "required fields" is essentially an upfront validation check that [has been responsible for outages of Google Search, GMail, and others](https://capnproto.org/faq.html#how-do-i-make-a-field-required-like-in-protocol-buffers).

We recommend, therefore, that validation occur at time of use. Code should be written to be tolerant of validation failures. For example, most code dealing with UTF-8 text should treat it as a blob of bytes, not worrying about invalid byte sequences. When you actually need to decode the code points -- such as to display them -- you should do something reasonable with invalid sequences -- such as display the Unicode replacement character.

With that said, when storing data in a database long-term, it can make sense to perform an additional validation check at time of storage, in order to more directly notify the caller that their input was invalid. This validation should be considered optional, since the data will be validated again when it is read from storage and used.

### Premature optimization fallacy

_"We should forget about small efficiencies, say about 97% of the time: premature optimization is the root of all evil."_ -- Donald Knuth

_"The improvement in speed from Example 2 to Example 2a is only about 12%, and many people would pronounce that insignificant. The conventional wisdom shared by many of today’s software engineers calls for ignoring efficiency in the small; but I believe this is simply an overreaction to the abuses they see being practiced by penny-wise- and-pound-foolish programmers, who can’t debug or maintain their “optimized” programs. In established engineering disciplines a 12% improvement, easily obtained, is never considered marginal; and I believe the same viewpoint should prevail in software engineering. Of course I wouldn’t bother making such optimizations on a one-shot job, but when it’s a question of preparing quality programs, I don’t want to restrict myself to tools that deny me such efficiencies."_ -- Donald Knuth, **in the same paper**.

(Credit: [Stop Misquoting Donald Knuth!](http://www.joshbarczak.com/blog/?p=580))

You should not obsess over optimization or write unmaintainable code for the sake of speed.

However, you _should_ be thinking about efficiency of all the code you write. When writing efficient code is not much harder and not much uglier than inefficient code, you should be writing efficient code. If the efficient approach to a problem would take _much_ longer than the inefficient way then go ahead and code the inefficient way first, but in many cases it's not that stark. Rewriting your code later is _much_ more expensive than writing it correctly the first time, because by then you'll have lost context.

You should be constantly aware of whether the code you are writing is low-level (called frequently) or high-level (called infrequently). You should consider optimizations relative to the code's level. In low-level code, optimizations like avoiding heap allocations may make sense. In high-level code you should not worry about heap, but you may still want to think about expensive operations like disk I/O or contacting remote servers (things that low-level code should never do in the first place, of course).

Programmers who ignore efficiency until they have no choice inevitably end up shipping slow, bloated software. Their code's speed is always pushing the boundary of bearability, because they only do anything about it when it becomes unbearable. But programs which are "bearable" can still make users intensely unhappy due to their slowness.

### Text is always UTF-8

Always encode text as UTF-8. Always assume text is encoded as UTF-8.

(Do not, however, assume text is _valid_ UTF-8; see the section on lazy validation.)

Do not write code that tries to distinguish characters. Unless you are writing code to render text to a display, you probably don't care about characters. Besides, Unicode itself contains code points which act as modifiers to previous characters; it's futile for you to handle these. Most code only really cares about bytes.

Note that even parsers for machine-readable text-based languages (config languages, programming languages, other DSLs) do not really care about "characters" in the Unicode sense because such languages are almost always pure-ASCII. They may allow arbitrary UTF-8 in, say, string literals, but only ASCII code points have any special meaning to the language. Therefore, they still only care about bytes (since ASCII characters are single-byte, and multi-byte UTF-8 codepoints never contain individual bytes in the ASCII range).

## C++ usage

This section contains guidelines for usage of C++ language features.

### Use C++11 (or later)

C++11 completely transformed the way the C++ language is used. New code should take heavy advantage of the new features, especially rvalue references (move semantics) and lambda expressions.

KJ requires C++11. Application code (not used as a library) may consider requiring C++14, or even requiring a specific compiler and tracking the latest language features implemented by it.

### Heap allocation

* Never write `new` or `delete` explicitly. Use `kj::heap` to allocate single objects or `kj::heapArray` for arrays; these return "owned" pointers (`kj::Own<T>` or `kj::Array<T>`, respectively) which enforce RAII/ownership semantics. You may transfer ownership of these pointers via move semantics, but otherwise the objects will be automatically deleted when they go out of scope. This makes memory leaks very rare in KJ code.
* Only allocate objects on the heap when you actually need to be able to move them. Otherwise, avoid a heap allocation by declaring the object directly on the stack or as a member of some other object.
* If a class's copy constructor would require memory allocation, consider providing a `clone()` method instead and deleting the copy constructor. Allocation in implicit copies is a common source of death-by-1000-cuts performance problems. `kj::String`, for example, is movable but not copyable.

### Pointers, references

* Pointers and references always point to things that are owned by someone else. Take care to think about the lifetime of that object compared to the lifetime of the pointer.
* Always use `kj::ArrayPtr<T>` rather than `T*` to point to an array.
* Always use `kj::StringPtr` rather than `const char*` to point to a NUL-terminated string.
* Always use `kj::Maybe<T&>` rather than `T*` when a pointer can be null. This forces the user to check for null-ness.
* In other cases, prefer references over pointers. Note, though, that members of an assignable type cannot be references, so you'll need to use pointers in that case (darn).

**Rationale:** There is an argument that says that references should always be const and pointers mutable, because then when you see `foo(&bar)` you know that the function modifies `bar`. This is a nice theory, but in practice real C++ code is rarely so consistent that you can use this as a real signal. We prefer references because they make it unambiguous that the value cannot be null.

### Constness

* Treat `const`-ness as transitive. So, if you have a const instance of a struct which in turn contains a pointer (or reference), treat that pointer as pointing to const even if it is not declared as such. To enforce this, copyable classes which contain pointer fields should declare their copy constructor as `T(T& other)` rather than `T(const T& other)` (and similarly for assignment operators) in order to prevent escalating a transitively-const pointer to non-const via copy. You may inherit `kj::DisallowConstCopy` to force the implicit copy constructor and assignment operator to be declared this way.
* Try to treat const/non-const pointers like shared/exclusive locks. So, when a new const pointer to an object is created, all other pointers should also be considered const at least until the new pointer is destroyed. When a new non-const pointer is created, all other pointers should be considered not dereferenceable until the non-const pointer is destroyed. In theory, these rules help keep different objects from interfering with each other by modifying some third object in incompatible ways. Note that these rules are (as I understand it) enforceable by the Rust type system.
* `const` methods are safe to call on the same object from multiple threads simultaneously. Conversely, it is unsafe to call a non-`const` method if any other thread might be calling methods on that object concurrently. Note that KJ defines synchronization primitives including `kj::Mutex` which integrate nicely with this rule.

### Inheritance

A class is either an interface or an implementation. Interfaces have no fields. Implementations have no non-final virtual methods. You should not mix these: a class with state should never have virtual methods, as this leads to fragile base class syndrome.

Interfaces should NOT declare a destructor, because:

* That destructor is never called anyway (because we don't use `delete`, and `kj::Own` has a different mechanism for dispatching the destructor).
* Declaring destructors for interfaces is tedious.
* If you declare a destructor but do not declare it `noexcept(false)`, C++11 will (regrettably) decide that it is `noexcept` and that all derived classes must also have a `noexcept` destructor, which is wrong. (See the exceptions philosophy section for discussion on exceptions in destructors.)

Multiple inheritance is allowed and encouraged, keeping in mind that you are usually inheriting interfaces.

You should think carefully about whether to use virtual inheritance; it's not often needed, and it is relatively inefficient, but in complex inheritance hierarchies it becomes critical.

Implementation inheritance (that is, inheriting an implementation class) is allowed as a way to compose classes without requiring extra allocations. For example, Cap'n Proto's `capnp::InputStreamMessageReader` implements the `capnp::MessageReader` interface by reading from a `kj::InputStream`, which is itself an interface. One implementation of `kj::InputStream` is `kj::FdInputStream`, which reads from a unix file descriptor. As a convenience, Cap'n Proto defines `capnp::StreamFdMessageReader` which multiply-inherits `capnp::InputStreamMessageReader` and `kj::FdInputStream` -- that is, it inherits two implementations, and even inherits the latter privately. Many style guides would consider this taboo. The benefit, though, is that people can declare this composed class on the stack as one unit, with no heap allocation, and end up with something that they can directly treat as a `capnp::MessageReader`; any other solution would lose one of these benefits.

### Exceptions Usage

KJ's exception philosophy is described earlier in this document. Here we describe only how to actually use exceptions in code.

Never use `throw` explicitly. Almost all exceptions should originate from the `KJ_ASSERT`, `KJ_REQUIRE`, and `KJ_SYSCALL` macros (see `kj/debug.h`). These macros allow you to easily attach useful debug information to the exception message without spending time on string formatting.

Never declare anything `noexcept`. As explained in the philosophy section, whether you like it or not, bugs can happen anywhere and therefore exceptions can happen anywhere. `noexcept` causes the process to abort on exceptions. Aborting is _never_ the right answer.

Explicit destructors must always be declared `noexcept(false)`, to work around C++11's regrettable decision that destructors should be `noexcept` by default. In destructors, always use `kj::UnwindDetector` or make all your asserts recoverable in order to ensure that an exception is not thrown during unwind.

Do not fret too much about recovering into a perfectly consistent state after every exception. That's not the point. The point is to be able to recover at all -- to _improve_ reliability, but not to make it perfect. So, write your code to do a reasonable thing in most cases.

For example, if you are implementing a data structure like a vector, do not worry about whether move constructors might throw. In practice, it is extraordinarily rare for move constructors to contain any code that could throw. So just assume they don't. Do NOT do what the C++ standard library does and require that all move constructors be explicitly `noexcept`, because people will not remember to mark their move constructors `noexcept`, and you'll just be creating a huge headache for everyone with _no practical benefit_.

### Template Metaprogramming

#### Reducing Verbosity

Before C++11, it was common practice to write "template functions" in the form of a templated struct which contained a single member representing the output of the function. For example, you might see `std::is_integral<int>::value` to check if `int` is integral. This pattern is excessively verbose, especially when composed into complex expressions.

In C++11, we can do better. Where before you would have declared a struct named `Foo<T>` with a single member as described above, in C++11 you should:

1. Define the struct as before, but with the name `Foo_<T>`.
2. Define a template `Foo<T>` which directly aliases the single member of `Foo_<T>`. If the output is a type, use a template `using`, whereas if the output is a value, use a `constexpr` function.

Example:

    template <typename T> struct IsConst_ { static constexpr bool value = false; };
    template <typename T> struct IsConst_<const T> { static constexpr bool value = true; };
    template <typename T> constexpr bool isConst() { return IsConst_<T>::value; }
    // Return true if T is const.

Or:

    template <typename T> struct UnConst_ { typedef T Type; };
    template <typename T> struct UnConst_<const T> { typedef T Type; };
    template <typename T> using UnConst = typename UnConst_<T>::Type;
    // If T is const, return the underlying non-const type.
    // Otherwise, just return T.

Now people can use your template metafunction without the pesky `::Type` or `::value` suffix.

#### Other hints

* To explicitly disable a template under certain circumstances, bind an unnamed template parameter to `kj::EnableIf`:

        template <typename T, typename = kj::EnableIf(!isConst<T>())>
        void mutate(T& ptr);
        // T must not be const.

* Say you're writing a template type with a constructor function like so:

        template <typename T>
        Wrapper<T> makeWrapper(T&& inner);
        // Wraps `inner` and returns the wrapper.
  
  Should `inner` be taken by reference or by value here? Both might be useful, depending on the use case. The right answer is actually to support both: if the input is an lvalue, take it by reference, but if it's an rvalue, take it by value (move). And as it turns out, if you write your declaration exactly as shown above, this is exactly what you get, because if the input is an lvalue, `T` will implicitly bind to a reference type, whereas if the input is an rvalue or rvalue reference, T will not be a reference.
  
  In general, you should assume KJ code in this pattern uses this rule, so if you are passing in an lvalue but don't actually want it wrapped by reference, wrap it in `kj::mv()`.

* Never use function or method pointers. Prefer templating across functors (like STL does), or for non-templates use `kj::Function` (which will handle this for you).

### Global Constructors

Do not declare global or static variables with dynamic constructors. Global constructors disproportionately hurt startup time because they force code to be paged in before it is really needed. They also are usually only needed by singletons, which you should not be using in general (see philosophy section).

You can have global constants of non-trivial class type as long as they are declared `constexpr`. If you want to declare complex data structures as constants, try to declare all the pieces as separate globals that reference each other, so that nothing has to be heap allocated and everything can be `constexpr`.

Use Clang's `-Wglobal-constructors` warning to catch mistakes.

### `dynamic_cast`

Do not use `dynamic_cast` as a way to implement polymorphism. That is, do not write long blocks of if/else statements each trying to cast an object to a different derived class to handle in a different way. Instead, extend the base class's interface to cover the functionality you need.

With that said, `dynamic_cast` is not always bad. It is fine to use `dynamic_cast` for "logistical" improvements, such as optimization. As a rule of thumb, imagine if `dynamic_cast` were replaced with a function that always returned null. Would your code still be correct (if, perhaps, slower, or with less detailed logging, etc.)? If so, then your use of `dynamic_cast` is fine.

#### `-fno-rtti`

The KJ and Cap'n Proto libraries are designed to function correctly when compiled with `-fno-rtti`. To that end, `kj::dynamicCastIfAvailable` is a version of `dynamic_cast` that, when compiled with `-fno-rtti`, always returns null, and KJ and Cap'n Proto code always uses this version.

We do NOT recommend disabling RTTI in your own code.

### Lambdas

Lamba capture lists must never use `=` to specify "capture all by value", because this makes it hard to review the capture list for possible lifetime issues.

Capture lists *may* use `&` ("capture all by reference") but *only* in cases where it is known that the lambda will not outlive the current stack frame. In fact, they generally *should* use `&` in this case, to make clear that there are no lifetime issues to think about.

### Use of Standard libraries

#### C++ Standard Library

The C++ standard library is old and full of a lot of cruft. Many APIs are designed in pre-C++11 styles that are no longer ideal. Mistakes like giving copy constructors to objects that own heap space (because in the absence of move semantics, it was needed for usability) and atomically-reference-counted strings (intended as an optimization to avoid so much heap copying, but actually a pessimization) are now baked into the library and cannot change. The `iostream` library was designed before anyone knew how to write good C++ code and is absolutely awful by today's standards. Some parts of the library, such as `<chrono>`, are over-engineered, designed by committees more interested in theoretical perfection than practicality. To add insult to injury, the library's naming style does not distinguish types from values.

For these reasons and others, KJ aims to be a replacement for the C++ standard libraries.

It is not there yet. As of this writing, the biggest missing piece is that KJ provides no implementation of maps or sets, nor a `sort()` function.

We recommend that KJ code use KJ APIs where available, falling back to C++ standard types when necessary. To avoid breaking clients later, avoid including C++ standard library headers from other headers; only include them from source files.

All users of the KJ library should familiarize themselves at least with the declarations in the following files, as you will use them all the time:

* `kj/common.h`
* `kj/memory.h`
* `kj/array.h`
* `kj/string.h`
* `kj/vector.h`
* `kj/debug.h`

#### C Library

As a general rule of thumb, C library functions documented in man section 3 should be treated with skepticism.

Do not use the C standard I/O functions -- your code should never contain `FILE*`. For formatting strings, `kj::str()` is much safer and easier than `sprintf()`. For debug logging, `KJ_DBG()` will produce more information with fewer keystrokes compared to `printf()`. For parsing, KJ's parser combinator library is cleaner and more powerful than `scanf()`. `fread()` and `fwrite()` imply buffering that you usually don't want; use `kj/io.h` instead, or raw file descriptors.

### Compiler warnings

Use the following warning settings with Clang or GCC:

* `-Wall -Wextra`: Enable most warnings.
* `-Wglobal-constructors`: (Clang-only) This catches global variables with constructors, which KJ style disallows (see above). You will, however, want to disable this warning in tests, since test frameworks use global constructors and are excepted from the style rule.
* `-Wno-sign-compare`: While comparison between signed and unsigned values could be a serious bug, we find that in practice this warning is almost always spurious.
* `-Wno-unused-parameter`: This warning is always spurious. I have never seen it find a real bug. Worse, it encourages people to delete parameter names which harms readability.

For development builds, `-Werror` should also be enabled. However, this should not be on by default in open source code as not everyone uses the same compiler or compiler version and different compiler versions often produce different warnings.

### Tools

We use:

* Clang for compiling.
* `KJ_DBG()` for simple debugging.
* Valgrind for complicated debugging.
* [Ekam](https://github.com/capnproto/ekam) for a build system.
* Git for version control.

## Irrelevant formatting rules

Many style guides dwell on formatting. We mention it only because it's vaguely nice to have some formatting consistency, but know that this section is the *least* relevant section of the document.

As a code reviewer, when you see a violation of formatting rules, think carefully about whether or not it really matters that you point it out. If you believe the author may be unfamiliar with the rules, it may be worth letting them know to read this document, if only so that they can try to be consistent in the future. However, it is NOT worth the time to comment on every misplaced whitespace. As long as the code is readable, move on.

### Naming

* Type names: `TitleCase`
* Variable, member, function, and method names: `camelCase`
* Constant and enumerant names: `CAPITAL_WITH_UNDERSCORES`
* Macro names: `CAPITAL_WITH_UNDERSCORES`, with an appropriate project-specific prefix like `KJ_` or `CAPNP_`.
* Namespaces: `oneword`. Namespaces should be kept short, because you'll have to type them a lot. The name of KJ itself was chosen for the sole purpose of making the namespace easy to type (while still being sufficiently unique). Use a nested namespace called `_` to contain package-private declarations.
* Files: `module-name.c++`, `module-name.h`, `module-name-test.c++`

**Rationale:** There has never been broad agreement on C++ naming style. The closest we have is the C++ standard library. Unfortunately, the C++ standard library made the awful decision of naming types and values in the same style, losing a highly useful visual cue that makes programming more pleasant, and preventing variables from being named after their type (which in many contexts is perfectly appropriate).

Meanwhile, the Java style, which KJ emulates, has been broadly adopted to varying degrees in other languages, from JavaScript to Haskell. Using a similar style in KJ code makes it less jarring to switch between C++ and those other languages. Being consistent with JavaScript is especially useful because it is the one language that everyone pretty much has to use, due to its use in the web platform.

There has also never been any agreement on C++ file extensions, for some reason. The extension `.c++`, though not widely used, is accepted by all reasonable tools and is clearly the most precise choice.

### Spacing and bracing

* Indents are two spaces.
* Never use tabs.
* Maximum line length is 100 characters.
* Indent continuation lines for braced init lists by two spaces.
* Indent all other continuation lines by four spaces.
* Alternatively, line up continuation lines with previous lines if it makes them easier to read.
* Place a space between a keyword and an open parenthesis, e.g.: `if (foo)`
* Do not place a space between a function name and an open parenthesis, e.g.: `foo(bar)`
* Place an opening brace at the end of the statement which initiates the block, not on its own line.
* Place a closing brace on a new line indented the same as the parent block. If there is post-brace code related to the block (e.g. `else` or `while`), place it on the same line as the closing brace.
* Always place braces around a block *unless* the block is so short that it can actually go on the same line as the introductory `if` or `while`, e.g.: `if (done) return;`.
* `case` statements are indented within the `switch`, and their following blocks are **further** indented (so the actual statements in a case are indented four spaces more than the `switch`).
* `public:`, `private:`, and `protected:` are reverse-indented by one stop.
* Statements inside a `namespace` are **not** indented unless the namespace is a short block that is just forward-declaring things at the top of a file.
* Set your editor to strip trailing whitespace on save, otherwise other people who use this setting will see spurious diffs when they edit a file after you.

<br>

    if (foo) {
      bar();
    } else if (baz) {
      qux(quux);
    } else {
      corge();
    }

    if (done) return;

    switch (grault) {
      case GARPLY:
        print("mew");
        break;
      case WALDO: {  // note: needs braces due to variable
        Location location = findWaldo();
        print(location);
        break;
      }
    }

<br>

    namespace external {
      class Forward;
      class Declarations;
      namespace nested {
        class More;
      }
    }

    namespace myproj {

    class Fred {
    public:
      Fred();
      ~Fred();
    private:
      int plugh;
    };

    }  // namespace myproj

**Rationale:** Code which is inconsistently or sloppily formatted gives the impression that the author is not observant or simply doesn't care about quality, and annoys other people trying to read your code.

Other than that, there is absolutely no good reason to space things one way or another.

### Comments

* Always use line comments (`//`). Never use block comments (`/**/`).

  **Rationale:** Block comments don't nest. Block comments tend to be harder to re-arrange, whereas a group of line comments can be moved easily. Also, typing `*` is just way harder than typing `/` so why would you want to?

* Write comments that add useful information that the reader might not already know. Do NOT write comments which say things that are already blatantly obvious from the code. For example, for a function `void frob(Bar bar)`, do not write a comment `// Frobs the Bar.`; that's already obvious. It's better to have no comment.

* Doc comments go **after** the declaration. If the declaration starts a block, the doc comment should go inside the block at the top. A group of related declarations can have a single group doc comment after the last one as long as there are no black lines between the declarations.

        int foo();
        // This is documentation for foo().

        class Bar {
          // This is documentation for Bar.
        public:
          Bar();

          inline int baz() { return 5; }
          inline int qux() { return 6; }
          // This is documentation for baz() and qux().
        };

  **Rationale:** When you start reading a doc comment, the first thing you want to know is *what the heck is being documented*. Having to scroll down through a long comment to see the declaration, then back up to read the docs, is bad. Sometimes, people actually repeat the declaration at the top of the comment just so that it's visible. This is silly. Let's just put the comment after the declaration.

* TODO comments are of the form `// TODO(type): description`, where `type` is one of:
  * `now`: Do before next `git push`.
  * `soon`: Do before next stable release.
  * `someday`: A feature that might be nice to have some day, but no urgency.
  * `perf`: Possible performance enhancement.
  * `security`: Possible security concern. (Used for low-priority issues. Obviously, code with serious security problems should never be written in the first place.)
  * `cleanup`: An improvement to maintainability with no user-visible effects.
  * `port`: Things to do when porting to a new platform.
  * `test`: Something that needs better testing.
  * `msvc`: Something to revisit when the next, hopefully less-broken version of Microsoft Visual Studio becomes available.
  * others: Additional TODO types may be defined for use in certain contexts.

  **Rationale:** Google's guide suggests that TODOs should bear the name of their author ("the person to ask for more information about the comment"), but in practice there's no particular reason why knowing the author is more useful for TODOs than for any other comment (or, indeed, code), and anyway that's what `git blame` is for. Meanwhile, having TODOs classified by type allows for useful searches, so that e.g. release scripts can error out if release-blocking TODOs are present.

### File templates

Generally, a "module" should consist of three files: `module.h`, `module.c++`, and `module-test.c++`. One or more of these can be omitted if it would otherwise be empty. Use the following templates when creating new files.

Headers:

    // Project Name - Project brief description
    // Copyright (c) 2015 Primary Author and contributors
    //
    // Licensed under the Whatever License blah blah no warranties.

    #pragma once
    // Documentation for file.

    #include <kj/common.h>

    namespace myproject {

    // declarations

    namespace _ {  // private

    // private declarations

    }  // namespace _ (private)

    }  // namespace myproject

Source code:

    // Project Name - Project brief description
    // Copyright (c) 2015 Primary Author and contributors
    //
    // Licensed under the Whatever License blah blah no warranties.

    #include "this-module.h"
    #include <other-module.h>

    namespace myproject {

    // definitions

    }  // namespace myproject

Test:

    // Project Name - Project brief description
    // Copyright (c) 2015 Primary Author and contributors
    //
    // Licensed under the Whatever License blah blah no warranties.

    #include "this-module.h"
    #include <other-module.h>

    namespace myproject {
    namespace {

    // KJ_TESTs

    }  // namespace
    }  // namespace myproject

Note that in both the source and test files, you should *always* include the corresponding header first, in order to ensure that it is self-contained (does not secretly require including some other header before it).
