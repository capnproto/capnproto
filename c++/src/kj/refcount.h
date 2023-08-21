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

#include "kj/common.h"
#include "kj/debug.h"
#include "memory.h"

#if _MSC_VER
#if _MSC_VER < 1910
#include <intrin.h>
#else
#include <intrin0.h>
#endif
#endif

KJ_BEGIN_HEADER

namespace kj {


template<typename T>
class Rc;

template<typename T>
class Arc;

template<typename T>
class EnableSharedFromThis;

// =======================================================================================
// Non-atomic (thread-unsafe) refcounting

class Refcounted: public Disposer {
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
  virtual ~Refcounted() noexcept(false) {
    KJ_ASSERT(refcount == 0, "Refcounted object deleted with non-zero refcount.");
  }
  KJ_DISALLOW_COPY_AND_MOVE(Refcounted);

  inline bool isShared() const { return refcount > 1; }
  // Check if there are multiple references to this object. This is sometimes useful for deciding
  // whether it's safe to modify the object vs. make a copy.

  void addRefInternal() { ++refcount; }

private:
  mutable uint refcount = 0;
  // "mutable" because disposeImpl() is const.  Bleh.

  void disposeImpl(void* pointer) const override {
    if (--refcount == 0) {
      delete this;
    }
  }

  template <typename T, typename... Params>
  friend Rc<T> refcounted(Params&&... params);

  template <typename T>
  friend class RefcountedWrapper;

  template <typename>
  friend class Rc;
};

template <typename T, typename... Params>
inline Rc<T> refcounted(Params&&... params) {
  // Allocate a new refcounted instance of T, passing `params` to its constructor.  Returns an
  // initial reference to the object.  More references can be created with `kj::addRef()`.
  auto t = new T(kj::fwd<Params>(params)...);
  t->addRefInternal();
  return Rc(t);
}

// template <typename T>
// Own<T> addRef(T& object) {
//   // Return a new reference to `object`, which must subclass Refcounted and have been allocated
//   // using `kj::refcounted<>()`.  It is suggested that subclasses implement a non-static addRef()
//   // method which wraps this and returns the appropriate type.

//   KJ_IREQUIRE(object.Refcounted::refcount > 0, "Object not allocated with kj::refcounted().");
//   object.addRefInternal();
//   return ARC(&object);
// }

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
    this->addRefInternal();
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
    this->addRefInternal();
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


class AtomicRefcounted: public kj::Disposer {
public:
  AtomicRefcounted() = default;
  virtual ~AtomicRefcounted() noexcept(false) {}
  KJ_DISALLOW_COPY_AND_MOVE(AtomicRefcounted);


  inline bool isShared() const {
#if _MSC_VER && !defined(__clang__)
    return KJ_MSVC_INTERLOCKED(Or, acq)(&refcount, 0) > 1;
#else
    return __atomic_load_n(&refcount, __ATOMIC_ACQUIRE) > 1;
#endif
  }

  template <typename U>
  Rc<U> refDowncast() {
    KJ_FAIL_REQUIRE("NOT IMPLEMENTED");
  }

  inline void addRefInternal() const {
#if _MSC_VER && !defined(__clang__)
  KJ_MSVC_INTERLOCKED(Increment, nf)(&refcount);
#else
  __atomic_add_fetch(&refcount, 1, __ATOMIC_RELAXED);
#endif
  }

private:
#if _MSC_VER && !defined(__clang__)
  mutable volatile long refcount = 1;
#else
  mutable volatile uint refcount = 1;
#endif

  bool addRefWeakInternal() const;

  void disposeImpl(void* pointer) const override {
#if _MSC_VER && !defined(__clang__)
  if (KJ_MSVC_INTERLOCKED(Decrement, rel)(&refcount) == 0) {
    std::atomic_thread_fence(std::memory_order_acquire);
    delete this;
  }
#else
  if (__atomic_sub_fetch(&refcount, 1, __ATOMIC_RELEASE) == 0) {
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    delete this;
  }
#endif
  }

  // template <typename T>
  // static kj::Own<T> addRefInternal(T* object);
  // template <typename T>
  // static kj::Own<const T> addRefInternal(const T* object);

  // template <typename T>
  // friend kj::Own<T> atomicAddRef(T& object);
  // template <typename T>
  // friend kj::Own<const T> atomicAddRef(const T& object);
  // template <typename T>
  // friend kj::Maybe<kj::Own<const T>> atomicAddRefWeak(const T& object);
  template <typename T, typename... Params>
  friend kj::Arc<T> atomicRefcounted(Params&&... params);

  template<typename>
  friend class Arc;
};


template<typename T>
class Rc: public Own<T> {
public:
  KJ_DISALLOW_COPY(Rc);
  Rc() { }
  Rc(decltype(nullptr)) { }

  inline Rc(Rc&& other) noexcept: Own<T>(kj::mv(other)) { }

  template <typename U, typename = EnableIf<canConvert<U*, T*>()>>
  inline Rc(Rc<U>&& other) noexcept : Own<T>(kj::mv(other)) { }

  kj::Rc<T> addRef() {
    auto refcounted = this->get();
    if (refcounted) {
      refcounted->addRefInternal();
    }
    return kj::Rc<T>(refcounted);
  }

  kj::Rc<const T> addRef() const {
    auto refcounted = this->get();
    refcounted->addRefInternal();
    return kj::Rc<T>(refcounted);
  }

  Rc& operator=(decltype(nullptr)) {
    Own<T>::operator=(nullptr);
    return *this;
  }

  Rc& operator=(Rc&& other) {
    Own<T>::operator=(kj::mv(other));
    return *this;
  }

  template <typename U>
  Rc<U> downcast() {
    auto own = Own<T>::template downcast<U>();
    return Rc<U>(kj::mv(own));
  }

  inline bool operator!=(decltype(nullptr)) { return this->get() != nullptr; }

private:
  Rc(T* t) : Own<T>(t, *t) { }
  Rc(Own<T>&& t) : Own<T>(kj::mv(t)) { }

  template <typename X, typename... Params>
  friend kj::Rc<X> refcounted(Params&&... params);

  template<typename>
  friend class EnableSharedFromThis;

  template <typename>
  friend class Rc;
};


template<typename T>
class Arc: public Own<T> {
public:
  KJ_DISALLOW_COPY(Arc);
  Arc() { }

  inline Arc(Arc&& other) noexcept: Own<T>(kj::mv(other)) { }

  operator Arc<const T>() {
    KJ_FAIL_REQUIRE("NOT IMPLEMEMTED");
  }

  kj::Arc<T> addRef() {
    auto refcounted = this->get();
    refcounted->addRefInternal();
    return kj::Arc<T>(refcounted);
  }

  kj::Arc<const T> addRef() const {
    auto refcounted = this->get();
    refcounted->addRefInternal();
    return kj::Arc<T>(refcounted);
  }

  Arc& operator=(decltype(nullptr)) {
    Own<T>::operator=(nullptr);
    return *this;
  }

  Arc& operator=(Arc&& other) {
    Own<T>::operator=(kj::mv(other));
    return *this;
  }

  template <typename U>
  Arc<U> downcast() {
    KJ_FAIL_REQUIRE("NOT IMPLEMENTED");
  }


private:
  Arc(T* t) : Own<T>(t, *t) { }

  template <typename X, typename... Params>
  friend kj::Arc<X> atomicRefcounted(Params&&... params);
};

template <typename T, typename... Params>
inline kj::Arc<T> atomicRefcounted(Params&&... params) {
  return kj::Arc(new T(kj::fwd<Params>(params)...));
}

// template <typename T>
// kj::Own<T> atomicAddRef(T& object) {
//   KJ_IREQUIRE(object.AtomicRefcounted::refcount > 0,
//       "Object not allocated with kj::atomicRefcounted().");
//   return AtomicRefcounted::addRefInternal(&object);
// }

// template <typename T>
// kj::Own<const T> atomicAddRef(const T& object) {
//   KJ_IREQUIRE(object.AtomicRefcounted::refcount > 0,
//       "Object not allocated with kj::atomicRefcounted().");
//   return AtomicRefcounted::addRefInternal(&object);
// }

// template <typename T>
// kj::Maybe<kj::Own<const T>> atomicAddRefWeak(const T& object) {
//   // Try to addref an object whose refcount could have already reached zero in another thread, and
//   // whose destructor could therefore already have started executing. The destructor must contain
//   // some synchronization that guarantees that said destructor has not yet completed when
//   // attomicAddRefWeak() is called (so that the object is still valid). Since the destructor cannot
//   // be canceled once it has started, in the case that it has already started, this function
//   // returns nullptr.

//   const AtomicRefcounted* refcounted = &object;
//   if (refcounted->addRefWeakInternal()) {
//     return kj::Own<const T>(&object, *refcounted);
//   } else {
//     return nullptr;
//   }
// }

// template <typename T>
// kj::Own<T> AtomicRefcounted::addRefInternal(T* object) {
// }

// template <typename T>
// kj::Own<const T> AtomicRefcounted::addRefInternal(const T* object) {
//   const AtomicRefcounted* refcounted = object;
// #if _MSC_VER && !defined(__clang__)
//   KJ_MSVC_INTERLOCKED(Increment, nf)(&refcounted->refcount);
// #else
//   __atomic_add_fetch(&refcounted->refcount, 1, __ATOMIC_RELAXED);
// #endif
//   return kj::Own<const T>(object, *refcounted);
// }

template<typename Self>
class EnableSharedFromThis {
protected:
  kj::Rc<const Self> addRefToThis() const {
    const Self* self = static_cast<const Self*>(this);
    self->addRefInternal();
    kj::Rc<const Self> result(self);
    return kj::mv(result);
  }

  kj::Rc<Self> addRefToThis() {
    Self* self = static_cast<Self*>(this);
    self->addRefInternal();
    kj::Rc<Self> result(self);
    return kj::mv(result);
  }
};

template<typename Self>
class ArcAddRefToThis {
protected:
  kj::Arc<const Self> addRefToThis() const {
    const Self* self = static_cast<const Self*>(this);
    self->addRefInternal();
    kj::Rc<const Self> result(self);
    return kj::mv(result);
  }

  kj::Arc<Self> addRefToThis() {
    Self* self = static_cast<Self*>(this);
    self->addRefInternal();
    kj::Rc<Self> result(self);
    return kj::mv(result);
  }
};



}  // namespace kj

KJ_END_HEADER
