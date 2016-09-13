---
layout: post
title: "Security Advisory -- And how to catch integer overflows with template metaprogramming"
author: kentonv
---

As the installation page has always stated, I do not yet recommend using Cap'n Proto's C++ library for handling possibly-malicious input, and will not recommend it until it undergoes a formal security review. That said, security is obviously a high priority for the project. The security of Cap'n Proto is in fact essential to the security of [Sandstorm.io](https://sandstorm.io), Cap'n Proto's parent project, in which sandboxed apps communicate with each other and the platform via Cap'n Proto RPC.

A few days ago, the first major security bugs were found in Cap'n Proto C++ -- two by security guru [Ben Laurie](http://en.wikipedia.org/wiki/Ben_Laurie) and one by myself during subsequent review (see below). You can read details about each bug in our new [security advisories directory](https://github.com/sandstorm-io/capnproto/tree/master/security-advisories):

* [Integer overflow in pointer validation.](https://github.com/sandstorm-io/capnproto/tree/master/security-advisories/2015-03-02-0-c++-integer-overflow.md)
* [Integer underflow in pointer validation.](https://github.com/sandstorm-io/capnproto/tree/master/security-advisories/2015-03-02-1-c++-integer-underflow.md)
* [CPU usage amplification attack.](https://github.com/sandstorm-io/capnproto/tree/master/security-advisories/2015-03-02-2-all-cpu-amplification.md)

I have backported the fixes to the last two release branches -- 0.5 and 0.4:

- Release 0.5.1.1: [source](https://capnproto.org/capnproto-c++-0.5.1.1.tar.gz), [win32](https://capnproto.org/capnproto-c++-win32-0.5.1.1.zip)
- Release 0.4.1.1: [source](https://capnproto.org/capnproto-c++-0.4.1.1.tar.gz)

Note that we added a "nano" component to the version number (rather than use 0.5.2/0.4.2) to indicate that this release is ABI-compatible with the previous release. If you are linking Cap'n Proto as a shared library, you only need to update the library, not re-compile your app.

To be clear, the first two bugs affect only the C++ implementation of Cap'n Proto; implementations in other languages are likely safe. The third bug probably affects all languages, and as of this writing only the C++ implementation (and wrappers around it) is fixed. However, this third bug is not as serious as the other two.

### Preventative Measures

It is our policy that any time a security problem is found, we will not only fix the problem, but also implement new measures to prevent the class of problems from occurring again. To that end, here's what we're doing doing to avoid problems like these in the future:

1. A fuzz test of each pointer type has been added to the standard unit test
   suite.
2. We will additionally add fuzz testing with American Fuzzy Lop to our
   extended test suite.
3. In parallel, we will extend our use of template metaprogramming for
   compile-time unit analysis (kj::Quantity in kj/units.h) to also cover
   overflow detection (by tracking the maximum size of an integer value across
   arithmetic expressions and raising an error when it overflows). More on this
   below.
4. We will continue to require that all tests (including the new fuzz test) run
   cleanly under Valgrind before each release.
5. We will commission a professional security review before any 1.0 release.
   Until that time, we continue to recommend against using Cap'n Proto to
   interpret data from potentially-malicious sources.

I am pleased to report that measures 1, 2, and 3 all detected both integer overflow/underflow problems, and AFL additionally detected the CPU amplification problem.

### Integer Overflow is Hard

Integer overflow is a nasty problem.

In the past, C and C++ code has been plagued by buffer overrun bugs, but these days, systems engineers have mostly learned to avoid them by simply never using static-sized buffers for dynamically-sized content. If we don't see proof that a buffer is the size of the content we're putting in it, our "spidey sense" kicks in.

But developing a similar sense for integer overflow is hard. We do arithmetic in code all the time, and the vast majority of it isn't an issue. The few places where overflow can happen all too easily go unnoticed.

And by the way, integer overflow affects many memory-safe languages too! Java and C# don't protect against overflow. Python does, using slow arbitrary-precision integers. Javascript doesn't use integers, and is instead succeptible to loss-of-precision bugs, which can have similar (but more subtle) consequences.

While writing Cap'n Proto, I made sure to think carefully about overflow and managed to correct for it most of the time. On learning that I missed a case, I immediately feared that I might have missed many more, and wondered how I might go about systematically finding them.

Fuzz testing -- e.g. using [American Fuzzy Lop](http://lcamtuf.coredump.cx/afl/) -- is one approach, and is indeed how Ben found the two bugs he reported. As mentioned above, we will make AFL part of our release process in the future. However, AFL cannot really _prove_ anything -- it can only try lots of possibilities. I want my compiler to refuse to compile arithmetic which might overflow.

### Proving Safety Through Template Metaprogramming

C++ Template Metaprogramming is powerful -- many would say _too_ powerful. As it turns out, it's powerful enough to do what we want.

I defined a new type:

{% highlight C++ %}
template <uint64_t maxN, typename T>
class Guarded {
  // Wraps T (a basic integer type) and statically guarantees
  // that the value can be no more than `maxN` and no less than
  // zero.

  static_assert(maxN <= T(kj::maxValue), "possible overflow detected");
  // If maxN is not representable in type T, we can no longer
  // guarantee no overflows.

public:
  // ...

  template <uint64_t otherMax, typename OtherT>
  inline constexpr Guarded(const Guarded<otherMax, OtherT>& other)
      : value(other.value) {
    // You cannot construct a Guarded from another Guarded
    // with a higher maximum.
    static_assert(otherMax <= maxN, "possible overflow detected");
  }

  // ...

  template <uint64_t otherMax, typename otherT>
  inline constexpr Guarded<guardedAdd<maxN, otherMax>(),
                           decltype(T() + otherT())>
      operator+(const Guarded<otherMax, otherT>& other) const {
    // Addition operator also computes the new maximum.
    // (`guardedAdd` is a constexpr template that adds two
    // constants while detecting overflow.)
    return Guarded<guardedAdd<maxN, otherMax>(),
                   decltype(T() + otherT())>(
        value + other.value, unsafe);
  }

  // ...

private:
  T value;
};
{% endhighlight %}

So, a `Guarded<10, int>` represents a `int` which is statically guaranteed to hold a non-negative value no greater than 10. If you add a `Guarded<10, int>` to `Guarded<15, int>`, the result is a `Guarded<25, int>`. If you try to initialize a `Guarded<10, int>` from a `Guarded<25, int>`, you'll trigger a `static_assert` -- the compiler will complain. You can, however, initialize a `Guarded<25, int>` from a `Guarded<10, int>` with no problem.

Moreover, because all of `Guarded`'s operators are inline and `constexpr`, a good optimizing compiler will be able to optimize `Guarded` down to the underlying primitive integer type. So, in theory, using `Guarded` has no runtime overhead. (I have not yet verified that real compilers get this right, but I suspect they do.)

Of course, the full implementation is considerably more complicated than this. The code has not been merged into the Cap'n Proto tree yet as we need to do more analysis to make sure it has no negative impact. For now, you can find it in the [overflow-safe](https://github.com/sandstorm-io/capnproto/tree/overflow-safe) branch, specifically in the second half of [kj/units.h](https://github.com/sandstorm-io/capnproto/blob/overflow-safe/c++/src/kj/units.h). (This header also contains metaprogramming for compile-time unit analysis, which Cap'n Proto has been using since its first release.)

### Results

I switched Cap'n Proto's core pointer validation code (`capnp/layout.c++`) over to `Guarded`. In the process, I found:

* Several overflows that could be triggered by the application calling methods with invalid parameters, but not by a remote attacker providing invalid message data. We will change the code to check these in the future, but they are not critical security problems.
* The overflow that Ben had already reported ([2015-03-02-0](https://github.com/sandstorm-io/capnproto/tree/master/security-advisories/2015-03-02-0-c++-integer-overflow.md)). I had intentionally left this unfixed during my analysis to verify that `Guarded` would catch it.
* One otherwise-undiscovered integer underflow ([2015-03-02-1](https://github.com/sandstorm-io/capnproto/tree/master/security-advisories/2015-03-02-1-c++-integer-underflow.md)).

Based on these results, I conclude that `Guarded` is in fact effective at finding overflow bugs, and that such bugs are thankfully _not_ endemic in Cap'n Proto's code.

With that said, it does not seem practical to change every integer throughout the Cap'n Proto codebase to use `Guarded` -- using it in the API would create too much confusion and cognitive overhead for users, and would force application code to be more verbose. Therefore, this approach unfortunately will not be able to find all integer overflows throughout the entire library, but fortunately the most sensitive parts are covered in `layout.c++`.

### Why don't programming languages do this?

Anything that can be implemented in C++ templates can obviously be implemented by the compiler directly. So, why have so many languages settled for either modular arithmetic or slow arbitrary-precision integers?

Languages could even do something which my templates cannot: allow me to declare relations between variables. For example, I would like to be able to declare an integer whose value is less than the size of some array. Then I know that the integer is a safe index for the array, without any run-time check.

Obviously, I'm not the first to think of this. "Dependent types" have been researched for decades, but we have yet to see a practical language supporting them. Apparently, something about them is complicated, even though the rules look like they should be simple enough from where I'm standing.

Some day, I would like to design a language that gets this right. But for the moment, I remain focused on [Sandstorm.io](https://sandstorm.io). Hopefully someone will beat me to it. Hint hint.
