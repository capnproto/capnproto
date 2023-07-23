# Introducing KJ

KJ is Modern C++'s missing base library.

## What's wrong with `std`?

The C++ language has advanced rapidly over the last decade. However, its standard library (`std`) remains a weak point. Most modern languages ship with libraries that have built-in support for common needs, such as making HTTP requests. `std`, meanwhile, not only lacks HTTP, but doesn't even support basic networking. Developers are forced either to depend on low-level, non-portable OS APIs, or pull in a bunch of third-party dependencies with inconsistent styles and quality.

Worse, `std` was largely designed before C++ best practices were established. Much of it predates C++11, which changed almost everything about how C++ is written. Some critical parts of `std` -- such as the `iostreams` component -- were designed before anyone really knew how to write quality object-oriented code, and are atrociously bad by modern standards.

Finally, `std` is designed by committee, which has advantages and disadvantages. On one hand, committees are less likely to make major errors in design. However, they also struggle to make bold decisions, and they move slowly. Committees can also lose touch with real-world concerns, over-engineering features that aren't needed while missing essential basics.

## How is KJ different?

KJ was designed and implemented primarily by one developer, Kenton Varda. Every feature was designed to solve a real-world need in a project Kenton was working on -- first [Cap'n Proto](https://capnproto.org), then [Sandstorm](https://sandstorm.io), and more recently, [Cloudflare Workers](https://workers.dev). KJ was designed from the beginning to target exclusively Modern C++ (C++11 and later).

Since its humble beginnings in 2013, KJ has developed a huge range of practical functionality, including:

* RAII utilities, especially for memory management
* Basic types and data structures: `Array`, `Maybe`, `OneOf`, `Tuple`, `Function`, `Quantity` (unit analysis), `String`, `Vector`, `HashMap`, `HashSet`, `TreeMap`, `TreeSet`, etc.
* Convenient stringification
* Exception/assertion framework with friggin' stack traces
* Event loop framework with `Promise` API inspired by E (which also inspired JavaScript's `Promise`).
* Threads, fibers, mutexes, lazy initialization
* I/O: Clocks, filesystem, networking
* Protocols: HTTP (client and server), TLS (via OpenSSL/BoringSSL), gzip (via libz)
* Parsers: URL, JSON (using Cap'n Proto), parser combinator framework
* Encodings: UTF-8/16/32, base64, hex, URL encoding, C escapes
* Command-line argument parsing
* Unit testing framework
* And more!

KJ is not always perfectly organized, and admittedly has some quirks. But, it has proven pragmatic and powerful in real-world applications.

# Getting KJ

KJ is bundled with Cap'n Proto -- see [installing Cap'n Proto](https://capnproto.org/install.html). KJ is built as a separate set of libraries, so that you can link against it without Cap'n Proto if desired.

KJ is officially tested on Linux (GCC and Clang), Windows (Visual Studio, MinGW, and Cygwin), MacOS, and Android. It should additionally be easy to get working on any POSIX platform targeted by GCC or Clang.

# FAQ

## What does KJ stand for?

Nothing.

The name "KJ" was chosen to be a relatively unusual combination of two letters that is easy to type (on both Qwerty and Dvorak layouts). This is important, because users of KJ will find themselves typing `kj::` very frequently.

## Why reinvent modern `std` features that are well-designed?

Some features of KJ appear to replace `std` features that were introduced recently with decent, modern designs. Examples include `kj::Own` vs `std::unique_ptr`, `kj::Maybe` vs `std::optional`, and `kj::Promise` vs `std::task`.

First, in many cases, the KJ feature actually predates the corresponding `std` feature. `kj::Maybe` was one of the first KJ types, introduced in 2013; `std::optional` arrived in C++17. `kj::Promise` was also introduced in 2013; `std::task` is coming in C++20 (with coroutines).

Second, consistency. KJ uses somewhat different idioms from `std`, resulting in some friction when trying to use KJ and `std` types together. The most obvious friction is aesthetic (e.g. naming conventions), but some deeper issues exist. For example, KJ tries to treat `const` as transitive, especially so that it can be used to help enforce thread-safety. This can lead to subtle problems (e.g. unexpected compiler errors) with `std` containers not designed with transitive constness in mind. KJ also uses a very different [philosophy around exceptions](../style-guide.md#exceptions) compared to `std`; KJ believes exception-free code is a myth, but `std` sometimes requires it.

Third, even some modern `std` APIs have design flaws. For example, `std::optional`s can be dereferenced without an explicit null check, resulting in a crash if the value is null -- exactly what this type should have existed to prevent! `kj::Maybe`, in contrast, forces you to write an if/else block or an explicit assertion. For another example, `kj::Own` uses dynamic dispatch for deleters, which allows for lots of useful patterns that `std::unique_ptr`'s static dispatch cannot do.

## Shouldn't modern software be moving away from memory-unsafe languages?

Probably!

Similarly, modern software should also move away from type-unsafe languages. Type-unsafety and memory-unsafety are both responsible for a huge number of security bugs. (Think SQL injection for an example of a security bug resulting from type-unsafety.)

Hence, all other things being equal, I would suggest Rust for new projects.

But it's rare that all other things are really equal, and you may have your reasons for using C++. KJ is here to help, not to judge.
