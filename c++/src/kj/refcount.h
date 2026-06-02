// Copyright (c) 2013-2014 Sandstorm Development Group, Inc. and contributors
// Licensed under the MIT License:
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#pragma once

#include "memory.h"

#if _MSC_VER && !defined(__clang__)
#include <intrin0.h> // _InterlockedXX
#endif

KJ_BEGIN_HEADER

namespace kj {

// =======================================================================================
// Non-atomic (thread-unsafe) refcounting

template<typename T>
class Rc;

template <typename T, typename... Params>
Rc<T> rc(Params&&... params);

namespace _ {  // private

template <typename T> class RcWrapper;
template <typename T> class RcOwnWrapper;

}  // namespace _ (private)

class Refcounted: private Disposer {
  // Subclass this to create a class that contains a reference count. Then, use
  // `kj::refcounted<T>()` to allocate a new refcounted pointer.
  //
  // Do NOT use this lightly.  Refcounting is a crutch.  Good designs should strive to make object
  // ownership clear, so that refcounting is not necessary.  All that said, reference counting can
  // sometimes simplify code that would otherwise become convoluted with explicit ownership, even
  // when ownership relationships are clear at an abstract level.
  //
  // NOT THREADSAFE:  This refcounting implementation assumes that an object's references are
  // manipulated only in one thread, because atomic (thread-safe) refcounting is surprisingly slow.
  //
  // In general, abstract classes should _not_ subclass this.  The concrete class at the bottom
  // of the hierarchy should be the one to decide how it implements refcounting.  Interfaces should
  // expose only an `addRef()` method that returns `Own<InterfaceType>`.  There are two reasons for
  // this rule:
  // 1. Interfaces would need to virtually inherit Refcounted, otherwise two refcounted interfaces
  //    could not be inherited by the same subclass.  Virtual inheritance is awkward and
  //    inefficient.
  // 2. An implementation may decide that it would rather return a copy than a refcount, or use
  //    some other strategy.
  //
  // TODO(cleanup):  Rethink above.  Virtual inheritance is not necessarily that bad.  OTOH, a
  //   virtual function call for every refcount is sad in its own way.  A Ref<T> type to replace
  //   Own<T> could also be nice.

public:
  Refcounted() = default;
  virtual ~Refcounted() noexcept(false);
  KJ_DISALLOW_COPY_AND_MOVE(Refcounted);

  inline bool isShared() const { return refcount > 1; }
  // Check if there are multiple references to this object. This is sometimes useful for deciding
  // whether it's safe to modify the object vs. make a copy.

protected:
  inline auto addRefToThis(this auto&& self) {
    return addRcRefInternal(&self);
  }

private:
  mutable uint refcount = 0;
  // "mutable" because disposeImpl() is const.  Bleh.

  void disposeImpl(void* pointer) const override;

  template <typename T>
  static Own<T> addRefInternal(T* object);

  template <typename T>
  static Rc<T> addRcRefInternal(T* object);

  template <typename T>
  friend Own<T> addRef(T& object);
  template <typename T, typename... Params>
  friend Own<T> refcounted(Params&&... params);

  template <typename T>
  friend class RefcountedWrapper;

  template <typename T, typename... Params>
  friend Rc<T> rc(Params&&... params);

  template <typename T>
  friend class Rc;

  template <typename T> friend class _::RcWrapper;
  template <typename T> friend class _::RcOwnWrapper;
};

template <typename T, typename... Params>
inline Own<T> refcounted(Params&&... params) {
  // Allocate a new refcounted instance of T, passing `params` to its constructor.  Returns an
  // initial reference to the object.  More references can be created with `kj::addRef()`.

  return Refcounted::addRefInternal(new T(kj::fwd<Params>(params)...));
}

template <typename T>
Own<T> addRef(T& object) {
  // Return a new reference to `object`, which must subclass Refcounted and have been allocated
  // using `kj::refcounted<>()`.  It is suggested that subclasses implement a non-static addRef()
  // method which wraps this and returns the appropriate type.

  KJ_IREQUIRE(object.Refcounted::refcount > 0, "Object not allocated with kj::refcounted().");
  return Refcounted::addRefInternal(&object);
}

template <typename T>
Own<T> Refcounted::addRefInternal(T* object) {
  Refcounted* refcounted = object;
  ++refcounted->refcount;
  return Own<T>(object, *refcounted);
}

template <typename T>
Rc<T> Refcounted::addRcRefInternal(T* object) {
  static_assert(kj::canConvert<T&, Refcounted&>());
  Refcounted* refcounted = object;
  ++refcounted->refcount;
  return Rc<T>(refcounted, object);
}

namespace _ {  // private

template <typename T>
class RcWrapper final: public Refcounted {
public:
  template <typename... Params>
  explicit RcWrapper(Params &&...params) : wrapped(kj::fwd<Params>(params)...) { ++refcount; }
  T* getWrappedPtr() { return &wrapped; }
  const T *getWrappedPtr() const { return &wrapped; }

private:
  T wrapped;
};

template <typename T>
class RcOwnWrapper final: public Refcounted {
public:
  explicit RcOwnWrapper(Own<T> &&wrapped) : wrapped(kj::mv(wrapped)) { ++refcount; }
  T* getWrappedPtr() { return wrapped.get(); }
  const T *getWrappedPtr() const { return wrapped.get(); }

private:
  Own<T> wrapped;
};

}  // namespace _ (private)

template<typename T>
class Rc {
  // Rc<T> is a smart pointer providing reference counting capabilities for all kinds of Ts.
  //
  // The primary way to obtain new `Rc<T>` instance is to use `kj::rc<T>(...)`, which allocates
  // a new T on the heap. If T extends Refcounted, T's `refcount` field is used for counting.
  // Otherwise, `kj::rc` allocates `RcWrapper<T>` to provide a `refcount`.
  //
  // Rc<T> can also be constructed from:
  // - kj::Own<T> for all types of T. Allocates a wrapper.
  // - T for non-`Refcounted` Ts with move constructor. Allocates a wrapper.
  //
  // Once you have `Rc<T>` you can `addRef` or `clone` it to increment the refcount and obtain new
  // smart pointer.
  //
  // Suggested usage patterns are:
  // - return kj::Rc as value from factory functions:
  //     `kj::Rc<MyService> createMyService();`
  // - pass kj::Rc as value to functions that need to extend T's lifetime:
  //     void setMyService(kj::Rc<MyService> service)
  // - store kj::Rc as data member:
  //     struct MyComputation { kj::Rc<MyService> service; };
  // - use toOwn to convert kj::Rc<T> instance to kj::Own<T> and use it
  //     without being concerned of reference counting behavior.
  //     To improve the transparency of the code, kj::Own<T> shouldn't be used
  //     to call addRef() without kj::Rc.
  // - convert kj::Own<T> to kj::Rc<T> to wrap an object into refcounted hold.
public:
  KJ_DISALLOW_COPY(Rc);
  Rc() { }
  Rc(decltype(nullptr)) { }
  inline Rc(Rc&& other) noexcept : refcounted(other.refcounted), ptr(other.ptr) {
    other.refcounted = nullptr;
    other.ptr = nullptr;
  }

  ~Rc() noexcept(false) { dispose(); }

  template <typename U = T, typename = EnableIf<canConvert<U*, T*>()>>
  inline Rc(Rc<U>&& other) noexcept : refcounted(other.refcounted), ptr(other.ptr) {
    other.refcounted = nullptr;
    other.ptr = nullptr;
  }

  template <typename U, typename = EnableIf<isSameType<U, T>()>>
  inline Rc(U t) noexcept {
    // This and below do not use concepts, but templates and static_asserts.
    // Concepts require T to be fully defined, but Rc<T> is often used with forward-declared T.
    // This function is declared as template to help msvc in polymorphic base class case.
    static_assert(!canConvert<T*, Refcounted*>());
    auto wrapper = new _::RcWrapper<U>(mv(t));
    refcounted = wrapper;
    ptr = wrapper->getWrappedPtr();
  }

  inline Rc(Own<T> t) noexcept {
    if (t.get() == nullptr) return;
    auto wrapper = new _::RcOwnWrapper<T>(mv(t));
    refcounted = wrapper;
    ptr = wrapper->getWrappedPtr();
  }

  Own<T> toOwn() {
    // Convert Rc<T> to Own<T>.
    // Nullifies the original Rc<T>.
    if (ptr == nullptr) return Own<T>();
    auto result = Own<T>(ptr, *refcounted);
    refcounted = nullptr;
    ptr = nullptr;
    return result;
  }

  Rc<T> addRef() {
    if (ptr != nullptr) {
      ++refcounted->refcount;
      return Rc(refcounted, ptr);
    } else {
      return Rc<T>();
    }
  }

  Rc<T> clone() {
    return addRef();
  }

  Rc& operator=(decltype(nullptr)) {
    dispose();
    return *this;
  }

  Rc& operator=(Rc&& other) {
    if (this == &other) return *this;
    swp(refcounted, other.refcounted);
    swp(ptr, other.ptr);
    other.dispose();
    return *this;
  }

  template <typename U>
  Rc<U> downcast() {
    Rc<U> result(refcounted, &kj::downcast<U>(*ptr));
    refcounted = nullptr;
    ptr = nullptr;
    return result;
  }

  inline bool operator==(const Rc<T>& other) const { return ptr == other.ptr; }
  inline bool operator==(decltype(nullptr)) const { return ptr == nullptr; }

  inline T* operator->() { KJ_IREQUIRE(ptr != nullptr, "null Rc<> dereference"); return ptr; }
  inline const T* operator->() const { KJ_IREQUIRE(ptr != nullptr, "null Rc<> dereference"); return ptr; }
  inline T& operator*() { KJ_IREQUIRE(ptr != nullptr, "null Rc<> dereference"); return *ptr; }
  inline const T& operator*() const { KJ_IREQUIRE(ptr != nullptr, "null Rc<> dereference"); return *ptr; }

  inline T* get() { return ptr; }
  inline const T* get() const { return ptr; }

private:
  Rc(Refcounted *wrapper, T *ptr) : refcounted(wrapper), ptr(ptr) {}
  void dispose() {
    if (ptr == nullptr) return;
    auto refcountedCopy = refcounted;
    refcounted = nullptr;
    ptr = nullptr;
    // refcounted dispose ignores the pointer
    refcountedCopy->dispose(static_cast<Refcounted*>(nullptr));
  }

  Refcounted* refcounted = nullptr;
  T* ptr = nullptr;

  friend class Refcounted;

  template <typename U, typename... Params>
  friend Rc<U> rc(Params&&... params);

  template <typename>
  friend class Rc;
};

template <typename T, typename... Params>
inline Rc<T> rc(Params&&... params) {
  // Allocate a new refcounted instance of T, passing `params` to its constructor.
  // Returns smart pointer that can be used to manage references.

  if constexpr (canConvert<T*, Refcounted*>()) {
    return Refcounted::addRcRefInternal(new T(fwd<Params>(params)...));
  } else {
    auto wrapper = new _::RcWrapper<T>(fwd<Params>(params)...);
    return Rc<T>(wrapper, wrapper->getWrappedPtr());
  }
}

template <typename T>
class RefcountedWrapper: public Refcounted {
  // Adds refcounting as a wrapper around an existing type, allowing you to construct references
  // with type Own<T> that appears to point directly to the underlying object.

public:
  template <typename... Params>
  RefcountedWrapper(Params&&... params): wrapped(kj::fwd<Params>(params)...) {}

  T& getWrapped() { return wrapped; }
  const T& getWrapped() const { return wrapped; }

  Own<T> addWrappedRef() {
    // Return an owned reference to the wrapped object that is backed by a refcount.
    ++refcount;
    return Own<T>(&wrapped, *this);
  }

private:
  T wrapped;
};

template <typename T>
class RefcountedWrapper<Own<T>>: public Refcounted {
  // Specialization for when the wrapped type is itself Own<T>. We don't want this to result in
  // Own<Own<T>>.

public:
  RefcountedWrapper(Own<T> wrapped): wrapped(kj::mv(wrapped)) {}

  T& getWrapped() { return *wrapped; }
  const T& getWrapped() const { return *wrapped; }

  Own<T> addWrappedRef() {
    // Return an owned reference to the wrapped object that is backed by a refcount.
    ++refcount;
    return Own<T>(wrapped.get(), *this);
  }

private:
  Own<T> wrapped;
};

template <typename T, typename... Params>
Own<RefcountedWrapper<T>> refcountedWrapper(Params&&... params) {
  return refcounted<RefcountedWrapper<T>>(kj::fwd<Params>(params)...);
}

template <typename T>
Own<RefcountedWrapper<Own<T>>> refcountedWrapper(Own<T>&& wrapped) {
  return refcounted<RefcountedWrapper<Own<T>>>(kj::mv(wrapped));
}

// =======================================================================================
// Atomic (thread-safe) refcounting
//
// Warning: Atomic ops are SLOW.

#if _MSC_VER && !defined(__clang__)
#if _M_ARM
#define KJ_MSVC_INTERLOCKED(OP, MEM) _Interlocked##OP##_##MEM
#else
#define KJ_MSVC_INTERLOCKED(OP, MEM) _Interlocked##OP
#endif
#endif

template<typename T>
class Arc;

template <typename T, typename... Params>
Arc<T> arc(Params&&... params);

namespace _ {  // private

template <typename T> class ArcWrapper;
template <typename T> class ArcOwnWrapper;

}  // namespace _ (private)

class AtomicRefcounted: private kj::Disposer {
public:
  AtomicRefcounted() = default;
  virtual ~AtomicRefcounted() noexcept(false);
  KJ_DISALLOW_COPY_AND_MOVE(AtomicRefcounted);

  inline bool isShared() const {
#if _MSC_VER && !defined(__clang__)
    return KJ_MSVC_INTERLOCKED(Or, acq)(&refcount, 0) > 1;
#else
    return __atomic_load_n(&refcount, __ATOMIC_ACQUIRE) > 1;
#endif
  }

protected:
  inline auto addRefToThis(this auto&& self) {
    return addRcRefInternal(&self);
  }

private:
#if _MSC_VER && !defined(__clang__)
  mutable volatile long refcount = 0;
#else
  mutable volatile uint refcount = 0;
#endif

  bool addRefWeakInternal() const;

  inline void incRefcount() const {
#if _MSC_VER && !defined(__clang__)
    KJ_MSVC_INTERLOCKED(Increment, nf)(&refcount);
#else
    __atomic_add_fetch(&refcount, 1, __ATOMIC_RELAXED);
#endif
  }

  void disposeImpl(void* pointer) const override;
  template <typename T>
  static kj::Own<T> addRefInternal(T* object);
  template <typename T>
  static kj::Own<const T> addRefInternal(const T* object);

  template <typename T>
  friend kj::Own<T> atomicAddRef(T& object);
  template <typename T>
  friend kj::Own<const T> atomicAddRef(const T& object);
  template <typename T>
  friend kj::Maybe<kj::Own<const T>> atomicAddRefWeak(const T& object);
  template <typename T, typename... Params>
  friend kj::Own<T> atomicRefcounted(Params&&... params);

  template <typename T>
  static kj::Arc<T> addRcRefInternal(const T* object);

  template <typename T>
  friend class Arc;
  template <typename T> friend class _::ArcWrapper;
  template <typename T> friend class _::ArcOwnWrapper;
  template <typename T, typename... Params>
  friend kj::Arc<T> arc(Params&&... params);
};

template <typename T, typename... Params>
inline kj::Own<T> atomicRefcounted(Params&&... params) {
  return AtomicRefcounted::addRefInternal(new T(kj::fwd<Params>(params)...));
}

template <typename T>
kj::Own<T> atomicAddRef(T& object) {
  KJ_IREQUIRE(object.AtomicRefcounted::refcount > 0,
      "Object not allocated with kj::atomicRefcounted().");
  return AtomicRefcounted::addRefInternal(&object);
}

template <typename T>
kj::Own<const T> atomicAddRef(const T& object) {
  KJ_IREQUIRE(object.AtomicRefcounted::refcount > 0,
      "Object not allocated with kj::atomicRefcounted().");
  return AtomicRefcounted::addRefInternal(&object);
}

template <typename T>
kj::Maybe<kj::Own<const T>> atomicAddRefWeak(const T& object) {
  // Try to addref an object whose refcount could have already reached zero in another thread, and
  // whose destructor could therefore already have started executing. The destructor must contain
  // some synchronization that guarantees that said destructor has not yet completed when
  // attomicAddRefWeak() is called (so that the object is still valid). Since the destructor cannot
  // be canceled once it has started, in the case that it has already started, this function
  // returns nullptr.

  const AtomicRefcounted* refcounted = &object;
  if (refcounted->addRefWeakInternal()) {
    return kj::Own<const T>(&object, *refcounted);
  } else {
    return kj::none;
  }
}

template <typename T>
kj::Own<T> AtomicRefcounted::addRefInternal(T* object) {
  AtomicRefcounted* refcounted = object;
  refcounted->incRefcount();
  return kj::Own<T>(object, *refcounted);
}

template <typename T>
kj::Own<const T> AtomicRefcounted::addRefInternal(const T* object) {
  const AtomicRefcounted* refcounted = object;
  refcounted->incRefcount();
  return kj::Own<const T>(object, *refcounted);
}

template <typename T>
kj::Arc<T> AtomicRefcounted::addRcRefInternal(const T* object) {
  static_assert(kj::canConvert<T&, AtomicRefcounted&>());
  const AtomicRefcounted* refcounted = object;
  refcounted->incRefcount();
  return kj::Arc<T>(refcounted, object);
}

namespace _ {  // private

template <typename T>
class ArcWrapper final: public AtomicRefcounted {
public:
  template <typename... Params>
  explicit ArcWrapper(Params&&... params): wrapped(kj::fwd<Params>(params)...) {
    incRefcount();
  }

  const T* getWrappedPtr() const { return &wrapped; }

private:
  T wrapped;
};

template <typename T>
class ArcOwnWrapper final: public AtomicRefcounted {
public:
  explicit ArcOwnWrapper(Own<const T>&& wrapped): wrapped(kj::mv(wrapped)) {
    incRefcount();
  }

  const T* getWrappedPtr() const { return wrapped.get(); }

private:
  Own<const T> wrapped;
};

}  // namespace _ (private)

template<typename T>
class Arc {
  // Smart pointer providing atomic reference-counted ownership.
  //
  // The primary way to obtain a new `Arc<T>` is `kj::arc<T>(...)`, which allocates a new T on the
  // heap. If T extends AtomicRefcounted, T's `refcount` field is used for counting. Otherwise,
  // `kj::arc` allocates `ArcWrapper<T>` to provide a `refcount`.
  //
  // The usage is similar to `kj::Rc<T>` but with a "const"-ness twist:
  // since in kj multithreaded code "const" means "thread-safe", `Arc<T>`
  // exposes only `const` members of T and thus is closer to `kj::Rc<const T>`.

public:
  KJ_DISALLOW_COPY(Arc);
  Arc() { }
  Arc(decltype(nullptr)) { }
  inline Arc(Arc&& other) noexcept: refcounted(other.refcounted), ptr(other.ptr) {
    other.refcounted = nullptr;
    other.ptr = nullptr;
  }

  template <typename U, typename = EnableIf<canConvert<U*, T*>()>>
  inline Arc(Arc<U>&& other) noexcept: refcounted(other.refcounted), ptr(other.ptr) {
    other.refcounted = nullptr;
    other.ptr = nullptr;
  }

  template <typename U = T, typename = EnableIf<isSameType<U, T>()>>
  inline Arc(U t) {
    static_assert(!canConvert<const T*, const AtomicRefcounted*>());
    auto wrapper = new _::ArcWrapper<U>(kj::mv(t));
    refcounted = wrapper;
    ptr = wrapper->getWrappedPtr();
  }

  inline Arc(Own<const T> t) {
    static_assert(!canConvert<const T*, const AtomicRefcounted*>());
    if (t.get() == nullptr) return;
    auto wrapper = new _::ArcOwnWrapper<T>(kj::mv(t));
    refcounted = wrapper;
    ptr = wrapper->getWrappedPtr();
  }

  ~Arc() noexcept(false) { dispose(); }

  kj::Own<const T> toOwn() {
    // Convert Arc<T> to Own<const T>.
    // Nullifies the original Arc<T>.
    if (ptr == nullptr) return Own<const T>();
    auto result = Own<const T>(ptr, *refcounted);
    refcounted = nullptr;
    ptr = nullptr;
    return result;
  }

  kj::Arc<T> addRef() const {
    if (ptr != nullptr) {
      refcounted->incRefcount();
      return Arc(refcounted, ptr);
    } else {
      return kj::Arc<T>();
    }
  }

  kj::Arc<T> clone() const {
    return addRef();
  }

  // Surrenders ownership of the underlying object to the caller. Unlike Own<T>::disown(), there
  // is no need for the caller to prove they know how to dispose of the object, because the object
  // is its own Disposer.
  const T* disown() {
    static_assert(canConvert<const T*, const AtomicRefcounted*>());
    const T* result = ptr;
    refcounted = nullptr;
    ptr = nullptr;
    return result;
  }

  // Assume ownership of an object without incrementing its refcount. Opposite of disown().
  static Arc reown(const T* ptr) {
    static_assert(canConvert<const T*, const AtomicRefcounted*>());
    return Arc(static_cast<const AtomicRefcounted*>(ptr), ptr);
  }

  Arc& operator=(decltype(nullptr)) {
    dispose();
    return *this;
  }

  Arc& operator=(Arc&& other) {
    if (this == &other) return *this;
    swp(refcounted, other.refcounted);
    swp(ptr, other.ptr);
    other.dispose();
    return *this;
  }

  template <typename U>
  Arc<U> downcast() {
    Arc<U> result;
    if (ptr != nullptr) {
      result = Arc<U>(refcounted, &kj::downcast<const U>(*ptr));
      refcounted = nullptr;
      ptr = nullptr;
    }
    return result;
  }

  inline bool operator==(const Arc<T>& other) const { return ptr == other.ptr; }
  inline bool operator==(decltype(nullptr)) const { return ptr == nullptr; }

#define NULLCHECK KJ_IREQUIRE(ptr != nullptr, "null Arc<> dereference")
  inline const T* operator->() const { NULLCHECK; return ptr; }
  inline const T& operator*() const { NULLCHECK; return *ptr; }
#undef NULLCHECK
  inline const T* get() const { return ptr; }

private:
  Arc(const AtomicRefcounted* refcounted, const T* ptr): refcounted(refcounted), ptr(ptr) {}

  void dispose() {
    if (ptr == nullptr) return;
    const AtomicRefcounted* refcountedCopy = refcounted;
    refcounted = nullptr;
    ptr = nullptr;
    // AtomicRefcounted dispose ignores the pointer.
    refcountedCopy->dispose(static_cast<AtomicRefcounted*>(nullptr));
  }

  const AtomicRefcounted* refcounted = nullptr;
  const T* ptr = nullptr;

  friend class AtomicRefcounted;

  template <typename U, typename... Params>
  friend Arc<U> arc(Params&&... params);

  template <typename>
  friend class Arc;
};

template <typename T, typename... Params>
inline Arc<T> arc(Params&&... params) {
  if constexpr (canConvert<T*, AtomicRefcounted*>()) {
    return AtomicRefcounted::addRcRefInternal(new T(kj::fwd<Params>(params)...));
  } else {
    auto wrapper = new _::ArcWrapper<T>(kj::fwd<Params>(params)...);
    return Arc<T>(wrapper, wrapper->getWrappedPtr());
  }
}


}  // namespace kj

KJ_END_HEADER
