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

template<typename T>
class EnableAddRefToThis;

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

  template <typename T>
  friend class EnableAddRefToThis;
};

template <typename T, typename... Params>
inline Own<T> refcounted(Params&&... params) {
  // Allocate a new refcounted instance of T, passing `params` to its constructor.  Returns an
  // initial reference to the object.  More references can be created with `kj::addRef()`.

  return Refcounted::addRefInternal(new T(kj::fwd<Params>(params)...));
}

template <typename T, typename... Params>
inline Rc<T> rc(Params&&... params) {
  // Allocate a new refcounted instance of T, passing `params` to its constructor.
  // Returns smart pointer that can be used to manage references.

  return Refcounted::addRcRefInternal(new T(kj::fwd<Params>(params)...));
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
  return Rc<T>(object);
}

template<typename T>
class Rc {
  // Smart pointer for reference counted objects. 
  //
  // There are only three ways to obtain new Rc instances:
  // - use kj::rc<T>(...) function to create new T.
  // - use kj::Rc::addRef() and the existing Rc instance.
  // - use EnableAddRefToThis to allow T instance to add new references to itself.
  //
  // Suggested usage patterns are:
  // - return kj::Rc as value from factory functions: 
  //     kj::Rc<MyService> createMyService();
  // - pass kj::Rc as rvalue to functions that need to extend T's lifetime:
  //     void setMyService(kj::Rc<MyService>&& service)
  // - store kj::Rc as data member:
  //     struct MyComputation { kj::Rc<MyService> service; };
  // - use toOwn to convert kj::Rc<T> instance to kj::Own<T> and use it
  //     without being concerned of reference counting behavior. 
  //     To improve the transparency of the code, kj::Own<T> shouldn't be used 
  //     to call addRef() without kj::Rc.

public:
  KJ_DISALLOW_COPY(Rc);
  Rc() { }
  Rc(decltype(nullptr)) { }
  inline Rc(Rc&& other) noexcept = default;

  template <typename U, typename = EnableIf<canConvert<U*, T*>()>>
  inline Rc(Rc<U>&& other) noexcept : own(kj::mv(other.own)) { }

  kj::Own<T> toOwn() {
    // Convert Rc<T> to Own<T>.
    // Nullifies the original Rc<T>.
    return kj::mv(own);
  }

  kj::Rc<T> addRef() {
    T* refcounted = own.get();
    if (refcounted != nullptr) {
      return Refcounted::addRcRefInternal(refcounted);
    } else {
      return kj::Rc<T>();
    }
  }

  Rc& operator=(decltype(nullptr)) {
    own = nullptr;
    return *this;
  }

  Rc& operator=(Rc&& other) = default;

  template <typename U>
  Rc<U> downcast() {
    return Rc<U>(own.template downcast<U>());
  }

  inline bool operator==(const Rc<T>& other) const { return own.get() == other.own.get(); }
  inline bool operator==(decltype(nullptr)) const { return own.get() == nullptr; }
  inline bool operator!=(decltype(nullptr)) const { return own.get() != nullptr; }

  inline T* operator->() { return own.get(); }
  inline const T* operator->() const { return own.get(); }

  inline T* get() { return own.get(); }
  inline const T* get() const { return own.get(); }

private:
  Rc(T* t) : own(t, *t) { }
  Rc(Own<T>&& t) : own(kj::mv(t)) { }

  Own<T> own;

  friend class Refcounted;

  template <typename>
  friend class Rc;

  template <typename>
  friend class EnableAddRefToThis;
};

template<typename Self>
class EnableAddRefToThis {
  // Exposes addRefToThis member function for objects to add
  // references to themselves.
  // Can be used both with Refcounted and AtomicRefcounted objects.

protected:
  auto addRefToThis() const {
    const Self* self = static_cast<const Self*>(this);
    return Self::addRcRefInternal(self);
  }

  auto addRefToThis() {
    Self* self = static_cast<Self*>(this);
    return Self::addRcRefInternal(self);
  }
};

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

private:
#if _MSC_VER && !defined(__clang__)
  mutable volatile long refcount = 0;
#else
  mutable volatile uint refcount = 0;
#endif

  bool addRefWeakInternal() const;

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
  static kj::Arc<T> addRcRefInternal(T* object);
  template <typename T>
  static kj::Arc<const T> addRcRefInternal(const T* object);

  template <typename T>
  friend class Arc;
  template <typename T, typename... Params>
  friend kj::Arc<T> arc(Params&&... params);

  template <typename>
  friend class EnableAddRefToThis;
};

template <typename T, typename... Params>
inline kj::Own<T> atomicRefcounted(Params&&... params) {
  return AtomicRefcounted::addRefInternal(new T(kj::fwd<Params>(params)...));
}

template <typename T, typename... Params>
inline kj::Arc<T> arc(Params&&... params) {
  return AtomicRefcounted::addRcRefInternal(new T(kj::fwd<Params>(params)...));
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
    return nullptr;
  }
}

template <typename T>
kj::Own<T> AtomicRefcounted::addRefInternal(T* object) {
  AtomicRefcounted* refcounted = object;
#if _MSC_VER && !defined(__clang__)
  KJ_MSVC_INTERLOCKED(Increment, nf)(&refcounted->refcount);
#else
  __atomic_add_fetch(&refcounted->refcount, 1, __ATOMIC_RELAXED);
#endif
  return kj::Own<T>(object, *refcounted);
}

template <typename T>
kj::Own<const T> AtomicRefcounted::addRefInternal(const T* object) {
  const AtomicRefcounted* refcounted = object;
#if _MSC_VER && !defined(__clang__)
  KJ_MSVC_INTERLOCKED(Increment, nf)(&refcounted->refcount);
#else
  __atomic_add_fetch(&refcounted->refcount, 1, __ATOMIC_RELAXED);
#endif
  return kj::Own<const T>(object, *refcounted);
}

template <typename T>
kj::Arc<T> AtomicRefcounted::addRcRefInternal(T* object) {
  static_assert(kj::canConvert<T&, AtomicRefcounted&>());
  return kj::Arc<T>(addRefInternal(object));
}

template <typename T>
kj::Arc<const T> AtomicRefcounted::addRcRefInternal(const T* object) {
  static_assert(kj::canConvert<T&, AtomicRefcounted&>());
  return kj::Arc<const T>(addRefInternal(object));
}

template<typename T>
class Arc {
  // Smart pointer for atomic reference counted objects. 
  //
  // Usage is similar to kj::Rc<T>.

public:
  KJ_DISALLOW_COPY(Arc);
  Arc() { }
  Arc(decltype(nullptr)) { }
  inline Arc(Arc&& other) noexcept = default;

  template <typename U, typename = EnableIf<canConvert<U*, T*>()>>
  inline Arc(Arc<U>&& other) noexcept : own(kj::mv(other.own)) { }

  kj::Own<T> toOwn() {
    // Convert Arc<T> to Own<T>.
    // Nullifies the original Arc<T>.
    return kj::mv(own);
  }

  kj::Arc<T> addRef() {
    T* refcounted = own.get();
    if (refcounted != nullptr) {
      return AtomicRefcounted::addRcRefInternal(refcounted);
    } else {
      return kj::Arc<T>();
    }
  }

  Arc& operator=(decltype(nullptr)) {
    own = nullptr;
    return *this;
  }

  Arc& operator=(Arc&& other) = default;

  template <typename U>
  Arc<U> downcast() {
    return Arc<U>(own.template downcast<U>());
  }

  inline bool operator==(const Arc<T>& other) const { return own.get() == other.own.get(); }
  inline bool operator==(decltype(nullptr)) const { return own.get() == nullptr; }
  inline bool operator!=(decltype(nullptr)) const { return own.get() != nullptr; }

  inline T* operator->() { return own.get(); }
  inline const T* operator->() const { return own.get(); }

  inline T* get() { return own.get(); }
  inline const T* get() const { return own.get(); }

private:
  Arc(T* t) : own(t, *t) { }
  Arc(Own<T>&& t) : own(kj::mv(t)) { }

  Own<T> own;

  friend class AtomicRefcounted;

  template <typename>
  friend class Arc;

  template <typename>
  friend class EnableAddRefToThis;
};


}  // namespace kj

KJ_END_HEADER
