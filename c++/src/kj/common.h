// Copyright (c) 2013, Kenton Varda <temporal@gmail.com>
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this
//    list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
// ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// Header that should be #included by everyone.
//
// This defines very simple utilities that are widely applicable.

#include <stddef.h>

#ifndef KJ_COMMON_H_
#define KJ_COMMON_H_

#ifndef KJ_NO_COMPILER_CHECK
#if __cplusplus < 201103L && !__CDT_PARSER__
  #error "This code requires C++11. Either your compiler does not support it or it is not enabled."
  #ifdef __GNUC__
    // Compiler claims compatibility with GCC, so presumably supports -std.
    #error "Pass -std=c++11 on the compiler command line to enable C++11."
  #endif
#endif

#ifdef __GNUC__
  #if __clang__
    #if __clang_major__ < 3 || (__clang_major__ == 3 && __clang_minor__ < 2)
      #warning "This library requires at least Clang 3.2."
    #elif defined(__apple_build_version__) && __apple_build_version__ <= 4250028
      #warning "This library requires at least Clang 3.2.  XCode 4.6's Clang, which claims to be "\
               "version 4.2 (wat?), is actually built from some random SVN revision between 3.1 "\
               "and 3.2.  Unfortunately, it is insufficient for compiling this library.  You can "\
               "download the real Clang 3.2 (or newer) from the Clang web site.  Step-by-step "\
               "instructions can be found in Cap'n Proto's documentation: "\
               "http://kentonv.github.io/capnproto/install.html#clang_32_on_mac_osx"
    #elif __cplusplus >= 201103L && !__has_include(<initializer_list>)
      #warning "Your compiler supports C++11 but your C++ standard library does not.  If your "\
               "system has libc++ installed (as should be the case on e.g. Mac OSX), try adding "\
               "-stdlib=libc++ to your CXXFLAGS."
    #endif
  #else
    #if __GNUC__ < 4 || (__GNUC__ == 4 && __GNUC_MINOR__ < 7)
      #warning "This library requires at least GCC 4.7."
    #endif
  #endif
#elif defined(_MSC_VER)
  #warning "As of June 2013, Visual Studio's C++11 support was hopelessly behind what is needed to compile this code."
#else
  #warning "I don't recognize your compiler.  As of this writing, Clang and GCC are the only "\
           "known compilers with enough C++11 support for this library.  "\
           "#define KJ_NO_COMPILER_CHECK to make this warning go away."
#endif
#endif

// =======================================================================================

namespace kj {

typedef unsigned int uint;
typedef unsigned char byte;

// =======================================================================================
// Common macros, especially for common yet compiler-specific features.

// Detect whether RTTI and exceptions are enabled, assuming they are unless we have specific
// evidence to the contrary.  Clients can always define KJ_NO_RTTI or KJ_NO_EXCEPTIONS explicitly
// to override these checks.
#ifdef __GNUC__
  #if !defined(KJ_NO_RTTI) && !__GXX_RTTI
    #define KJ_NO_RTTI 1
  #endif
  #if !defined(KJ_NO_EXCEPTIONS) && !__EXCEPTIONS
    #define KJ_NO_EXCEPTIONS 1
  #endif
#elif defined(_MSC_VER)
  #if !defined(KJ_NO_RTTI) && !defined(_CPPRTTI)
    #define KJ_NO_RTTI 1
  #endif
  #if !defined(KJ_NO_EXCEPTIONS) && !defined(_CPPUNWIND)
    #define KJ_NO_EXCEPTIONS 1
  #endif
#endif

#define KJ_DISALLOW_COPY(classname) \
  classname(const classname&) = delete; \
  classname& operator=(const classname&) = delete
// Deletes the implicit copy constructor and assignment operator.

#define KJ_LIKELY(condition) __builtin_expect(condition, true)
#define KJ_UNLIKELY(condition) __builtin_expect(condition, false)
// Branch prediction macros.  Evaluates to the condition given, but also tells the compiler that we
// expect the condition to be true/false enough of the time that it's worth hard-coding branch
// prediction.

#if defined(NDEBUG) && !__NO_INLINE__
#define KJ_ALWAYS_INLINE(prototype) inline prototype __attribute__((always_inline))
// Force a function to always be inlined.  Apply only to the prototype, not to the definition.
#else
#define KJ_ALWAYS_INLINE(prototype) inline prototype
// Don't force inline in debug mode.
#endif

#define KJ_NORETURN __attribute__((noreturn))
#define KJ_UNUSED __attribute__((unused))

#if __clang__
#define KJ_UNUSED_MEMBER __attribute__((unused))
// Inhibits "unused" warning for member variables.  Only Clang produces such a warning, while GCC
// complains if the attribute is set on members.
#else
#define KJ_UNUSED_MEMBER
#endif

namespace _ {  // private

void inlineRequireFailure(
    const char* file, int line, const char* expectation, const char* macroArgs,
    const char* message = nullptr) KJ_NORETURN;

}  // namespace _ (private)

#ifdef NDEBUG
#define KJ_IREQUIRE(condition, ...)
#else
#define KJ_IREQUIRE(condition, ...) \
    if (KJ_LIKELY(condition)); else ::kj::_::inlineRequireFailure( \
        __FILE__, __LINE__, #condition, #__VA_ARGS__, ##__VA_ARGS__)
// Version of KJ_REQUIRE() which is safe to use in headers that are #included by users.  Used to
// check preconditions inside inline methods.  KJ_IREQUIRE is particularly useful in that
// it will be enabled depending on whether the application is compiled in debug mode rather than
// whether libkj is.
#endif

// #define KJ_STACK_ARRAY(type, name, size, minStack, maxStack)
//
// Allocate an array, preferably on the stack, unless it is too big.  On GCC this will use
// variable-sized arrays.  For other compilers we could just use a fixed-size array.  `minStack`
// is the stack array size to use if variable-width arrays are not supported.  `maxStack` is the
// maximum stack array size if variable-width arrays *are* supported.
#if __clang__
#define KJ_STACK_ARRAY(type, name, size, minStack, maxStack) \
  size_t name##_size = (size); \
  bool name##_isOnStack = name##_size <= (minStack); \
  type name##_stack[minStack]; \
  ::kj::Array<type> name##_heap = name##_isOnStack ? \
      nullptr : kj::heapArray<type>(name##_size); \
  ::kj::ArrayPtr<type> name = name##_isOnStack ? \
      kj::arrayPtr(name##_stack, name##_size) : name##_heap
#else
#define KJ_STACK_ARRAY(type, name, size, minStack, maxStack) \
  size_t name##_size = (size); \
  bool name##_isOnStack = name##_size <= (maxStack); \
  type name##_stack[name##_isOnStack ? size : 0]; \
  ::kj::Array<type> name##_heap = name##_isOnStack ? \
      nullptr : kj::heapArray<type>(name##_size); \
  ::kj::ArrayPtr<type> name = name##_isOnStack ? \
      kj::arrayPtr(name##_stack, name##_size) : name##_heap
#endif

#define KJ_ARRAY_SIZE(arr) (sizeof(arr) / sizeof(arr[0]))

#define KJ_CONCAT_(x, y) x##y
#define KJ_CONCAT(x, y) KJ_CONCAT_(x, y)
#define KJ_UNIQUE_NAME(prefix) KJ_CONCAT(prefix, __LINE__)
// Create a unique identifier name.  We use concatenate __LINE__ rather than __COUNTER__ so that
// the name can be used multiple times in the same macro.

// =======================================================================================
// Template metaprogramming helpers.

template <typename T> struct NoInfer_ { typedef T Type; };
template <typename T> using NoInfer = typename NoInfer_<T>::Type;
// Use NoInfer<T>::Type in place of T for a template function parameter to prevent inference of
// the type based on the parameter value.

template <typename T> struct RemoveConst_ { typedef T Type; };
template <typename T> struct RemoveConst_<const T> { typedef T Type; };
template <typename T> using RemoveConst = typename RemoveConst_<T>::Type;

template <typename> struct IsLvalueReference_ { static constexpr bool value = false; };
template <typename T> struct IsLvalueReference_<T&> { static constexpr bool value = true; };
template <typename T>
inline constexpr bool isLvalueReference() { return IsLvalueReference_<T>::value; }

template <typename T> struct Decay_ { typedef T Type; };
template <typename T> struct Decay_<T&> { typedef typename Decay_<T>::Type Type; };
template <typename T> struct Decay_<T&&> { typedef typename Decay_<T>::Type Type; };
template <typename T> struct Decay_<T[]> { typedef typename Decay_<T*>::Type Type; };
template <typename T> struct Decay_<const T[]> { typedef typename Decay_<const T*>::Type Type; };
template <typename T, size_t s> struct Decay_<T[s]> { typedef typename Decay_<T*>::Type Type; };
template <typename T, size_t s> struct Decay_<const T[s]> { typedef typename Decay_<const T*>::Type Type; };
template <typename T> struct Decay_<const T> { typedef typename Decay_<T>::Type Type; };
template <typename T> struct Decay_<volatile T> { typedef typename Decay_<T>::Type Type; };
template <typename T> using Decay = typename Decay_<T>::Type;

template <bool b> struct EnableIf_;
template <> struct EnableIf_<true> { typedef void Type; };
template <bool b> using EnableIf = typename EnableIf_<b>::Type;
// Use like:
//
//     template <typename T, typename = EnableIf<isValid<T>()>
//     void func(T&& t);

template <typename T>
T instance() noexcept;
// Like std::declval, but doesn't transform T into an rvalue reference.  If you want that, specify
// instance<T&&>().

struct DisallowConstCopy {
  // Inherit from this, or declare a member variable of this type, to prevent the class from being
  // copyable from a const reference -- instead, it will only be copyable from non-const references.
  // This is useful for enforcing transitive constness of contained pointers.
  //
  // For example, say you have a type T which contains a pointer.  T has non-const methods which
  // modify the value at that pointer, but T's const methods are designed to allow reading only.
  // Unfortunately, if T has a regular copy constructor, someone can simply make a copy of T and
  // then use it to modify the pointed-to value.  However, if T inherits DisallowConstCopy, then
  // callers will only be able to copy non-const instances of T.  Ideally, there is some
  // parallel type ImmutableT which is like a version of T that only has const methods, and can
  // be copied from a const T.
  //
  // Note that due to C++ rules about implicit copy constructors and assignment operators, any
  // type that contains or inherits from a type that disallows const copies will also automatically
  // disallow const copies.  Hey, cool, that's exactly what we want.

  DisallowConstCopy() = default;
  DisallowConstCopy(DisallowConstCopy&);
  DisallowConstCopy(DisallowConstCopy&&) = default;
  DisallowConstCopy& operator=(DisallowConstCopy&);
  DisallowConstCopy& operator=(DisallowConstCopy&&) = default;
};

// Apparently these cannot be defaulted inside the class due to some obscure C++ rule.
inline DisallowConstCopy::DisallowConstCopy(DisallowConstCopy&) = default;
inline DisallowConstCopy& DisallowConstCopy::operator=(DisallowConstCopy&) = default;

template <typename T>
struct DisallowConstCopyIfNotConst: public DisallowConstCopy {
  // Inherit from this when implementing a template that contains a pointer to T and which should
  // enforce transitive constness.  If T is a const type, this has no effect.  Otherwise, it is
  // an alias for DisallowConstCopy.
};

template <typename T>
struct DisallowConstCopyIfNotConst<const T> {};

template <typename T> struct IsConst_ { static constexpr bool value = false; };
template <typename T> struct IsConst_<const T> { static constexpr bool value = true; };
template <typename T> constexpr bool isConst() { return IsConst_<T>::value; }

template <typename T> struct EnableIfNotConst_ { typedef T Type; };
template <typename T> struct EnableIfNotConst_<const T>;
template <typename T> using EnableIfNotConst = typename EnableIfNotConst_<T>::Type;

template <typename T> struct EnableIfConst_;
template <typename T> struct EnableIfConst_<const T> { typedef T Type; };
template <typename T> using EnableIfConst = typename EnableIfConst_<T>::Type;

template <typename T> struct RemoveConstOrDisable_ { struct Type; };
template <typename T> struct RemoveConstOrDisable_<const T> { typedef T Type; };
template <typename T> using RemoveConstOrDisable = typename RemoveConstOrDisable_<T>::Type;

template <typename T> struct IsReference_ { static constexpr bool value = false; };
template <typename T> struct IsReference_<T&> { static constexpr bool value = true; };
template <typename T> constexpr bool isReference() { return IsReference_<T>::value; }

// =======================================================================================
// Equivalents to std::move() and std::forward(), since these are very commonly needed and the
// std header <utility> pulls in lots of other stuff.
//
// We use abbreviated names mv and fwd because these helpers (especially mv) are so commonly used
// that the cost of typing more letters outweighs the cost of being slightly harder to understand
// when first encountered.

template<typename T> constexpr T&& mv(T& t) noexcept { return static_cast<T&&>(t); }
template<typename T> constexpr T&& fwd(NoInfer<T>& t) noexcept { return static_cast<T&&>(t); }

template <typename T, typename U>
inline constexpr auto min(T&& a, U&& b) -> decltype(a < b ? a : b) { return a < b ? a : b; }
template <typename T, typename U>
inline constexpr auto max(T&& a, U&& b) -> decltype(a > b ? a : b) { return a > b ? a : b; }

// =======================================================================================
// Manually invoking constructors and destructors
//
// ctor(x, ...) and dtor(x) invoke x's constructor or destructor, respectively.

// We want placement new, but we don't want to #include <new>.  operator new cannot be defined in
// a namespace, and defining it globally conflicts with the definition in <new>.  So we have to
// define a dummy type and an operator new that uses it.

namespace _ {  // private
struct PlacementNew {};
}  // namespace _ (private)
} // namespace kj

inline void* operator new(size_t, kj::_::PlacementNew, void* __p) noexcept {
  return __p;
}

namespace kj {

template <typename T, typename... Params>
inline void ctor(T& location, Params&&... params) {
  new (_::PlacementNew(), &location) T(kj::fwd<Params>(params)...);
}

template <typename T>
inline void dtor(T& location) {
  location.~T();
}

// =======================================================================================
// Maybe
//
// Use in cases where you want to indicate that a value may be null.  Using Maybe<T&> instead of T*
// forces the caller to handle the null case in order to satisfy the compiler, thus reliably
// preventing null pointer dereferences at runtime.
//
// Maybe<T> can be implicitly constructed from T and from nullptr.  Additionally, it can be
// implicitly constructed from T*, in which case the pointer is checked for nullness at runtime.
// To read the value of a Maybe<T>, do:
//
//    KJ_IF_MAYBE(value, someFuncReturningMaybe()) {
//      doSomething(*value);
//    } else {
//      maybeWasNull();
//    }
//
// KJ_IF_MAYBE's first parameter is a variable name which will be defined within the following
// block.  The variable will behave like a (guaranteed non-null) pointer to the Maybe's value,
// though it may or may not actually be a pointer.
//
// Note that Maybe<T&> actually just wraps a pointer, whereas Maybe<T> wraps a T and a boolean
// indicating nullness.

template <typename T>
class Maybe;

namespace _ {  // private

template <typename T>
class NullableValue {
  // Class whose interface behaves much like T*, but actually contains an instance of T and a
  // boolean flag indicating nullness.

public:
  inline NullableValue(NullableValue&& other) noexcept(noexcept(T(instance<T&&>())))
      : isSet(other.isSet) {
    if (isSet) {
      ctor(value, kj::mv(other.value));
    }
  }
  inline NullableValue(const NullableValue& other)
      : isSet(other.isSet) {
    if (isSet) {
      ctor(value, other.value);
    }
  }
  inline ~NullableValue() noexcept(noexcept(instance<T&>().~T())) {
    if (isSet) {
      dtor(value);
    }
  }

  inline T& operator*() { return value; }
  inline const T& operator*() const { return value; }
  inline T* operator->() { return &value; }
  inline const T* operator->() const { return &value; }
  inline operator T*() { return isSet ? &value : nullptr; }
  inline operator const T*() const { return isSet ? &value : nullptr; }

private:  // internal interface used by friends only
  inline NullableValue() noexcept: isSet(false) {}
  inline NullableValue(T&& t) noexcept(noexcept(T(instance<T&&>())))
      : isSet(true) {
    ctor(value, kj::mv(t));
  }
  inline NullableValue(const T& t)
      : isSet(true) {
    ctor(value, t);
  }
  inline NullableValue(const T* t)
      : isSet(t != nullptr) {
    if (isSet) ctor(value, *t);
  }
  template <typename U>
  inline NullableValue(NullableValue<U>&& other) noexcept(noexcept(T(instance<U&&>())))
      : isSet(other.isSet) {
    if (isSet) {
      ctor(value, kj::mv(other.value));
    }
  }
  template <typename U>
  inline NullableValue(const NullableValue<U>& other)
      : isSet(other.isSet) {
    if (isSet) {
      ctor(value, other.value);
    }
  }
  template <typename U>
  inline NullableValue(const NullableValue<U&>& other)
      : isSet(other.isSet) {
    if (isSet) {
      ctor(value, *other.ptr);
    }
  }
  inline NullableValue(decltype(nullptr)): isSet(false) {}

  inline NullableValue& operator=(NullableValue&& other) {
    if (&other != this) {
      if (isSet) {
        dtor(value);
      }
      isSet = other.isSet;
      if (isSet) {
        ctor(value, kj::mv(other.value));
      }
    }
    return *this;
  }

  inline NullableValue& operator=(const NullableValue& other) {
    if (&other != this) {
      if (isSet) {
        dtor(value);
      }
      isSet = other.isSet;
      if (isSet) {
        ctor(value, other.value);
      }
    }
    return *this;
  }

  inline bool operator==(decltype(nullptr)) const { return !isSet; }
  inline bool operator!=(decltype(nullptr)) const { return isSet; }

private:
  bool isSet;
  union {
    T value;
  };

  friend class kj::Maybe<T>;
  template <typename U>
  friend NullableValue<U>&& readMaybe(Maybe<U>&& maybe);
};

template <typename T>
inline NullableValue<T>&& readMaybe(Maybe<T>&& maybe) { return kj::mv(maybe.ptr); }
template <typename T>
inline T* readMaybe(Maybe<T>& maybe) { return maybe.ptr; }
template <typename T>
inline const T* readMaybe(const Maybe<T>& maybe) { return maybe.ptr; }
template <typename T>
inline T* readMaybe(Maybe<T&>&& maybe) { return maybe.ptr; }
template <typename T>
inline T* readMaybe(const Maybe<T&>& maybe) { return maybe.ptr; }

}  // namespace _ (private)

#define KJ_IF_MAYBE(name, exp) if (auto name = ::kj::_::readMaybe(exp))

template <typename T>
class Maybe {
  // A T, or nullptr.

  // IF YOU CHANGE THIS CLASS:  Note that there is a specialization of it in memory.h.

public:
  Maybe(): ptr(nullptr) {}
  Maybe(T&& t) noexcept(noexcept(T(instance<T&&>()))): ptr(kj::mv(t)) {}
  Maybe(const T& t): ptr(t) {}
  Maybe(const T* t) noexcept: ptr(t) {}
  Maybe(Maybe&& other) noexcept(noexcept(T(instance<T&&>()))): ptr(kj::mv(other.ptr)) {}
  Maybe(const Maybe& other): ptr(other.ptr) {}

  template <typename U>
  Maybe(Maybe<U>&& other) noexcept(noexcept(T(instance<U&&>()))) {
    KJ_IF_MAYBE(val, kj::mv(other)) {
      ptr = *val;
    }
  }
  template <typename U>
  Maybe(const Maybe<U>& other) {
    KJ_IF_MAYBE(val, other) {
      ptr = *val;
    }
  }

  Maybe(decltype(nullptr)) noexcept: ptr(nullptr) {}

  inline Maybe& operator=(Maybe&& other) { ptr = kj::mv(other.ptr); return *this; }
  inline Maybe& operator=(const Maybe& other) { ptr = other.ptr; return *this; }

  inline bool operator==(decltype(nullptr)) const { return ptr == nullptr; }
  inline bool operator!=(decltype(nullptr)) const { return ptr != nullptr; }

  template <typename Func>
  auto map(Func&& f) -> Maybe<decltype(f(instance<T&>()))> {
    if (ptr == nullptr) {
      return nullptr;
    } else {
      return f(*ptr);
    }
  }

  template <typename Func>
  auto map(Func&& f) const -> Maybe<decltype(f(instance<const T&>()))> {
    if (ptr == nullptr) {
      return nullptr;
    } else {
      return f(*ptr);
    }
  }

  // TODO(someday):  Once it's safe to require GCC 4.8, use ref qualifiers to provide a version of
  //   map() that uses move semantics if *this is an rvalue.

private:
  _::NullableValue<T> ptr;

  template <typename U>
  friend class Maybe;
  template <typename U>
  friend _::NullableValue<U>&& _::readMaybe(Maybe<U>&& maybe);
  template <typename U>
  friend U* _::readMaybe(Maybe<U>& maybe);
  template <typename U>
  friend const U* _::readMaybe(const Maybe<U>& maybe);
};

template <typename T>
class Maybe<T&>: public DisallowConstCopyIfNotConst<T> {
public:
  Maybe() noexcept: ptr(nullptr) {}
  Maybe(T& t) noexcept: ptr(&t) {}
  Maybe(T* t) noexcept: ptr(t) {}

  template <typename U>
  inline Maybe(Maybe<U&>& other) noexcept: ptr(other.ptr) {}
  template <typename U>
  inline Maybe(const Maybe<const U&>& other) noexcept: ptr(other.ptr) {}
  inline Maybe(decltype(nullptr)) noexcept: ptr(nullptr) {}

  inline Maybe& operator=(T& other) noexcept { ptr = &other; }
  inline Maybe& operator=(T* other) noexcept { ptr = other; }
  template <typename U>
  inline Maybe& operator=(Maybe<U&>& other) noexcept { ptr = other.ptr; return *this; }
  template <typename U>
  inline Maybe& operator=(const Maybe<const U&>& other) noexcept { ptr = other.ptr; return *this; }

  inline bool operator==(decltype(nullptr)) const { return ptr == nullptr; }
  inline bool operator!=(decltype(nullptr)) const { return ptr != nullptr; }

  template <typename Func>
  auto map(Func&& f) -> Maybe<decltype(f(instance<T&>()))> {
    if (ptr == nullptr) {
      return nullptr;
    } else {
      return f(*ptr);
    }
  }

private:
  T* ptr;

  template <typename U>
  friend class Maybe;
  template <typename U>
  friend U* _::readMaybe(Maybe<U&>&& maybe);
  template <typename U>
  friend U* _::readMaybe(const Maybe<U&>& maybe);
};

// =======================================================================================
// ArrayPtr
//
// So common that we put it in common.h rather than array.h.

template <typename T>
class ArrayPtr: public DisallowConstCopyIfNotConst<T> {
  // A pointer to an array.  Includes a size.  Like any pointer, it doesn't own the target data,
  // and passing by value only copies the pointer, not the target.

public:
  inline constexpr ArrayPtr(): ptr(nullptr), size_(0) {}
  inline constexpr ArrayPtr(decltype(nullptr)): ptr(nullptr), size_(0) {}
  inline constexpr ArrayPtr(T* ptr, size_t size): ptr(ptr), size_(size) {}
  inline constexpr ArrayPtr(T* begin, T* end): ptr(begin), size_(end - begin) {}

  inline operator ArrayPtr<const T>() const {
    return ArrayPtr<const T>(ptr, size_);
  }
  inline ArrayPtr<const T> asConst() const {
    return ArrayPtr<const T>(ptr, size_);
  }

  inline size_t size() const { return size_; }
  inline const T& operator[](size_t index) const {
    KJ_IREQUIRE(index < size_, "Out-of-bounds ArrayPtr access.");
    return ptr[index];
  }
  inline T& operator[](size_t index) {
    KJ_IREQUIRE(index < size_, "Out-of-bounds ArrayPtr access.");
    return ptr[index];
  }

  inline T* begin() { return ptr; }
  inline T* end() { return ptr + size_; }
  inline T& front() { return *ptr; }
  inline T& back() { return *(ptr + size_ - 1); }
  inline const T* begin() const { return ptr; }
  inline const T* end() const { return ptr + size_; }
  inline const T& front() const { return *ptr; }
  inline const T& back() const { return *(ptr + size_ - 1); }

  inline ArrayPtr<const T> slice(size_t start, size_t end) const {
    KJ_IREQUIRE(start <= end && end <= size_, "Out-of-bounds ArrayPtr::slice().");
    return ArrayPtr<const T>(ptr + start, end - start);
  }
  inline ArrayPtr slice(size_t start, size_t end) {
    KJ_IREQUIRE(start <= end && end <= size_, "Out-of-bounds ArrayPtr::slice().");
    return ArrayPtr(ptr + start, end - start);
  }

  inline bool operator==(decltype(nullptr)) const { return size_ == 0; }
  inline bool operator!=(decltype(nullptr)) const { return size_ != 0; }

  inline bool operator==(const ArrayPtr& other) const {
    if (size_ != other.size_) return false;
    for (size_t i = 0; i < size_; i++) {
      if (ptr[i] != other[i]) return false;
    }
    return true;
  }
  inline bool operator!=(const ArrayPtr& other) const { return !(*this == other); }

private:
  T* ptr;
  size_t size_;
};

template <typename T>
inline constexpr ArrayPtr<T> arrayPtr(T* ptr, size_t size) {
  // Use this function to construct ArrayPtrs without writing out the type name.
  return ArrayPtr<T>(ptr, size);
}

template <typename T>
inline constexpr ArrayPtr<T> arrayPtr(T* begin, T* end) {
  // Use this function to construct ArrayPtrs without writing out the type name.
  return ArrayPtr<T>(begin, end);
}

// =======================================================================================
// Casts

template <typename To, typename From>
To implicitCast(From&& from) {
  // `implicitCast<T>(value)` casts `value` to type `T` only if the conversion is implicit.  Useful
  // for e.g. resolving ambiguous overloads without sacrificing type-safety.
  return kj::fwd<From>(from);
}

template <typename To, typename From>
Maybe<To&> dynamicDowncastIfAvailable(From& from) {
  // If RTTI is disabled, always returns nullptr.  Otherwise, works like dynamic_cast.  Useful
  // in situations where dynamic_cast could allow an optimization, but isn't strictly necessary
  // for correctness.  It is highly recommended that you try to arrange all your dynamic_casts
  // this way, as a dynamic_cast that is necessary for correctness implies a flaw in the interface
  // design.

  // Force a compile error if To is not a subtype of From.  Cross-casting is rare; if it is needed
  // we should have a separate cast function like dynamicCrosscastIfAvailable().
  if (false) {
    kj::implicitCast<From*>(kj::implicitCast<To*>(nullptr));
  }

#if KJ_NO_RTTI
  return nullptr;
#else
  return dynamic_cast<To*>(&from);
#endif
}

template <typename To, typename From>
To& downcast(From& from) {
  // Down-cast a value to a sub-type, asserting that the cast is valid.  In opt mode this is a
  // static_cast, but in debug mode (when RTTI is enabled) a dynamic_cast will be used to verify
  // that the value really has the requested type.

  // Force a compile error if To is not a subtype of From.
  if (false) {
    kj::implicitCast<From*>(kj::implicitCast<To*>(nullptr));
  }

#if !KJ_NO_RTTI
  KJ_IREQUIRE(dynamic_cast<To*>(&from) != nullptr, "Value cannot be downcast() to requested type.");
#endif

  return static_cast<To&>(from);
}

// =======================================================================================
// Defer

namespace _ {  // private

template <typename Func>
class Deferred {
public:
  inline Deferred(Func func): func(func), canceled(false) {}
  inline ~Deferred() { if (!canceled) func(); }
  KJ_DISALLOW_COPY(Deferred);

  // This move constructor is usually optimized away by the compiler.
  inline Deferred(Deferred&& other): func(kj::mv(other.func)) {
    other.canceled = true;
  }
private:
  Func func;
  bool canceled;
};

}  // namespace _ (private)

template <typename Func>
_::Deferred<Decay<Func>> defer(Func&& func) {
  // Returns an object which will invoke the given functor in its destructor.  The object is not
  // copyable but is movable with the semantics you'd expect.  Since the return type is private,
  // you need to assign to an `auto` variable.
  //
  // The KJ_DEFER macro provides slightly more convenient syntax for the common case where you
  // want some code to run at function exit.

  return _::Deferred<Decay<Func>>(kj::fwd<Func>(func));
}

#define KJ_DEFER(code) auto KJ_UNIQUE_NAME(_kjDefer) = ::kj::defer([&](){code;})
// Run the given code when the function exits, whether by return or exception.

}  // namespace kj

#endif  // KJ_COMMON_H_
