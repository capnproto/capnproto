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

#include "common.h"
#include "atomic.h"

#ifndef KJ_ASSERT_PTR_COUNTERS
#define KJ_ASSERT_PTR_COUNTERS KJ_DEBUG_MEMORY
#endif

// KJ_WARN_REFCOUNTED_ATTACH == 1 enables deprecation warnings when using kj::Own<T>::attach() on
// refcounted objects.
#if !defined(KJ_WARN_REFCOUNTED_ATTACH)
#define KJ_WARN_REFCOUNTED_ATTACH 0
#endif

KJ_BEGIN_HEADER

namespace kj {

template <typename T>
inline constexpr bool _kj_internal_isPolymorphic(T*) {
  // If you get a compiler error here complaining that T is incomplete, it's because you are trying
  // to use kj::Own<T> with a type that has only been forward-declared. Since KJ doesn't know if
  // the type might be involved in inheritance (especially multiple inheritance), it doesn't know
  // how to correctly call the disposer to destroy the type, since the object's true memory address
  // may differ from the address used to point to a superclass.
  //
  // However, if you know for sure that T is NOT polymorphic (i.e. it doesn't have a vtable and
  // isn't involved in inheritance), then you can use KJ_DECLARE_NON_POLYMORPHIC(T) to declare this
  // to KJ without actually completing the type. Place this macro invocation either in the global
  // scope, or in the same namespace as T is defined.
  return __is_polymorphic(T);
}

#define KJ_DECLARE_NON_POLYMORPHIC(...) \
  inline constexpr bool _kj_internal_isPolymorphic(__VA_ARGS__*) { \
    return false; \
  }
// If you want to use kj::Own<T> for an incomplete type T that you know is not polymorphic, then
// write `KJ_DECLARE_NON_POLYMORPHIC(T)` either at the global scope or in the same namespace as
// T is declared.
//
// This also works for templates, e.g.:
//
//     template <typename X, typename Y>
//     struct MyType;
//     template <typename X, typename Y>
//     KJ_DECLARE_NON_POLYMORPHIC(MyType<X, Y>)

class AtomicRefcounted;
class Refcounted;

namespace _ {  // private

template <typename T, typename U>
concept DerivedFrom = requires(const T* t) { static_cast<const U*>(t); };
template <typename T>
concept IsRefcounted = DerivedFrom<T, Refcounted> || DerivedFrom<T, AtomicRefcounted>;

template <typename T> struct RefOrVoid_ { typedef T& Type; };
template <> struct RefOrVoid_<void> { typedef void Type; };
template <> struct RefOrVoid_<const void> { typedef void Type; };

template <typename T>
using RefOrVoid = typename RefOrVoid_<T>::Type;
// Evaluates to T&, unless T is `void`, in which case evaluates to `void`.
//
// This is a hack needed to avoid defining Own<void> as a totally separate class.

template <typename T>
void* castToVoid(T* ptr) {
  if constexpr (_kj_internal_isPolymorphic((T*)nullptr)) {
    return dynamic_cast<void*>(ptr);
  } else {
    return static_cast<void*>(ptr);
  }
}

template <typename T>
const void* castToConstVoid(T* ptr) {
  if constexpr (_kj_internal_isPolymorphic((T*)nullptr)) {
    const T* cptr = ptr;
    return dynamic_cast<const void*>(cptr);
  } else {
    const T* cptr = ptr;
    return static_cast<const void*>(cptr);
  }
}

KJ_NORETURN(void throwWrongDisposerError());

template <typename From, typename To>
using EnableIfCanConvertPtr = EnableIf<canConvert<From*, To*>() && isConst<From>() == isConst<To>()>;
// Checks if Ptr<From> can be converted into Ptr<To> following const-correctness.

class OwnControl;
class PtrTargetRef;

}  // namespace _ (private)

// =======================================================================================
// Disposer -- Implementation details.

class Disposer {
  // Abstract interface for a thing that "disposes" of objects, where "disposing" usually means
  // calling the destructor followed by freeing the underlying memory.  `Own<T>` encapsulates an
  // object pointer with corresponding Disposer.
  //
  // Few developers will ever touch this interface.  It is primarily useful for those implementing
  // custom memory allocators.

protected:
  // Do not declare a destructor, as doing so will force a global initializer for each HeapDisposer
  // instance.  Eww!

  virtual void disposeImpl(void* pointer) const = 0;
  // Disposes of the object, given a pointer to the beginning of the object.  If the object is
  // polymorphic, this pointer is determined by dynamic_cast<void*>().  For non-polymorphic types,
  // Own<T> does not allow any casting, so the pointer exactly matches the original one given to
  // Own<T>.

public:

  template <typename T>
  void dispose(T* object) const;
  // Helper wrapper around disposeImpl().
  //
  // If T is polymorphic, calls `disposeImpl(dynamic_cast<void*>(object))`, otherwise calls
  // `disposeImpl(implicitCast<void*>(object))`.
  //
  // Callers must not call dispose() on the same pointer twice, even if the first call throws
  // an exception.

  virtual _::OwnControl* getOwnControl() const { return nullptr; }
  // Returns Own pointer-lifetime bookkeeping owned by this disposer, if present.
};

template <typename T>
class DestructorOnlyDisposer: public Disposer {
  // A disposer that merely calls the type's destructor and nothing else.

public:
  static const DestructorOnlyDisposer instance;

  void disposeImpl(void* pointer) const override {
    reinterpret_cast<T*>(pointer)->~T();
  }
};

template <typename T>
const DestructorOnlyDisposer<T> DestructorOnlyDisposer<T>::instance = DestructorOnlyDisposer<T>();

class NullDisposer: public Disposer {
  // A disposer that does nothing.

public:
  static const NullDisposer instance;

  void disposeImpl(void* pointer) const override {}
};


// =======================================================================================
// Pointer tracking

template <typename T>
class Ptr;

template <typename T>
class Weak;

template <typename T>
class Pin;

class PtrTarget;

namespace _ {

#if KJ_ASSERT_PTR_COUNTERS
using PtrCounter = AtomicPtrCounter;
#else
using PtrCounter = EmptyPtrCounter;
#endif

class AtomicPtrCounterPtr {
  // Pointer to an atomic pointer-lifetime counter.
public:
  AtomicPtrCounterPtr() = default;
  explicit AtomicPtrCounterPtr(AtomicPtrCounter* ptr): ptr(ptr) {}

  inline AtomicPtrCounter* get() const { return ptr; }
  inline void set(AtomicPtrCounter* value) { ptr = value; }
  inline void reset() { ptr = nullptr; }

private:
  AtomicPtrCounter* ptr = nullptr;
};

class EmptyPtrCounterPtr {
  // Zero-size optimized-build substitute for AtomicPtrCounterPtr.
public:
  EmptyPtrCounterPtr() = default;
  explicit EmptyPtrCounterPtr(EmptyPtrCounter*) {}

  inline EmptyPtrCounter* get() const { return nullptr; }
  inline void set(EmptyPtrCounter*) {}
  inline void reset() {}
};

#if KJ_ASSERT_PTR_COUNTERS
using PtrCounterPtr = AtomicPtrCounterPtr;
#else
using PtrCounterPtr = EmptyPtrCounterPtr;
#endif

class WeakCell {
  // Shared validity cell for kj::Weak<T>. The referent owns one reference while it is alive; Weak
  // owns one reference per weak pointer. When the referent is destroyed or moved, ptr is nulled
  // before it releases its reference, allowing outstanding Weak pointers to observe expiration.

public:
  explicit WeakCell(const void* ptr, PtrCounter* ptrCounter)
      : ptr(ptr), ptrCounter(ptrCounter) {}

  inline void addRef() { ++refcount; }
  inline void decRef() { if (--refcount == 0) { delete this; } }
  inline PtrCounter* getPtrCounter() const { return ptrCounter.get(); }

  const void* ptr;

private:
  size_t refcount = 1;
  KJ_NO_UNIQUE_ADDRESS PtrCounterPtr ptrCounter;
};

class OwnControl {
  // Lazily-allocated lifetime bookkeeping for Own<T>.
public:
  OwnControl() = default;
  KJ_DISALLOW_COPY_AND_MOVE(OwnControl);

  ~OwnControl() noexcept(false) {
    if (weakCell != nullptr) {
      weakCell->ptr = nullptr;
      weakCell->decRef();
    }
    ptrCounter.assertEmpty();
  }

  inline WeakCell* getWeakCell(const void* ptr) {
    if (weakCell == nullptr) {
      weakCell = new WeakCell(ptr, getPtrCounter());
    }
    return weakCell;
  }

  inline PtrCounter* getPtrCounter() { return &ptrCounter; }

private:
  WeakCell* weakCell = nullptr;
  KJ_NO_UNIQUE_ADDRESS PtrCounter ptrCounter;
};

}  // namespace _ (private)

class PtrTarget {
  // PtrTarget integrates a type with kj::Ptr<T> and kj::Weak<T>.
  //
  // Subclass this to allow creating strong and weak pointers that refer directly to `this`,
  // similar to how kj::Refcounted enables kj::addRef(). From within the subclass use
  // addPtrToThis() to obtain a Ptr<Self> and addWeakToThis() to obtain a Weak<Self>.
  //
  // PtrTarget provides the same lifetime tracking as kj::Pin<T>: it must not be moved or destroyed
  // while there are active Ptr<T>s referring to it; outstanding Weak<T>s are nulled instead. When
  // KJ_ASSERT_PTR_COUNTERS is defined, pointers are tracked and these constraints are asserted.
  //
  // PtrTarget *is* the control block reused by kj::Pin<T>: it exposes only private bookkeeping
  // methods to its friends Ptr, Weak and Pin, and adds one pointer of overhead, allocating a
  // shared cell lazily when the first weak reference is created.

public:
  PtrTarget() = default;
  ~PtrTarget() noexcept(false) { dispose(); }
  KJ_DISALLOW_COPY_AND_MOVE(PtrTarget);

protected:
  template <typename Self>
  inline Ptr<Self> addPtrToThis(this Self& self) {
    // Obtain a new strong pointer to `this`. Like kj::Pin<T>, the PtrTarget must outlive all
    // strong pointers obtained this way.
    return Ptr<Self>(&self, asPtrTarget(self));
  }

  template <typename Self>
  inline Weak<Self> addWeakToThis(this Self& self) {
    // Obtain a new weak pointer to `this`. Weak pointers do not keep the PtrTarget alive; they
    // become null when it is destroyed.
    return Weak<Self>(&self, asPtrTarget(self));
  }

  inline void invalidateWeak() {
    // Permanently invalidate weak pointers to this object. Derived class destructors can call this
    // before tearing down state that may otherwise be accessed while the rest of the object is
    // destroyed.
    if (weakCell == nullptr) {
      weakCell = new _::WeakCell(nullptr, getPtrCounter());
    } else {
      weakCell->ptr = nullptr;
    }
  }

private:
  template <typename Self>
  static inline PtrTarget* asPtrTarget(Self& self) {
    // The control bookkeeping is logically mutable (like a refcount), so it's fine to strip const
    // here. This allows PtrTarget subclasses to be used as Ptr<const Self>/Weak<const Self>.
    return const_cast<PtrTarget*>(static_cast<const PtrTarget*>(&self));
  }

  inline _::WeakCell* getWeakCell(const void* ptr) {
    if (weakCell == nullptr) {
      weakCell = new _::WeakCell(ptr, getPtrCounter());
    }
    return weakCell;
  }

  inline _::PtrCounter* getPtrCounter() { return &ptrCounter; }

  inline void dispose() {
    if (weakCell != nullptr) {
      weakCell->ptr = nullptr;
      weakCell->decRef();
      weakCell = nullptr;
    }

    assertEmpty();
  }

  inline void assertEmpty() { ptrCounter.assertEmpty(); }

  _::WeakCell* weakCell = nullptr;
  KJ_NO_UNIQUE_ADDRESS _::PtrCounter ptrCounter;

  template <typename>
  friend class Ptr;
  template <typename>
  friend class Weak;
  template <typename>
  friend class Pin;
  friend class _::PtrTargetRef;
};

namespace _ {

class PtrTargetRef {
  // Registers one live Ptr with its pointer-lifetime target. Has zero size when pointer assertions
  // are disabled.
public:
  PtrTargetRef() = default;
  explicit PtrTargetRef(PtrTarget* target): PtrTargetRef(target->getPtrCounter()) {}
  explicit PtrTargetRef(OwnControl* control): PtrTargetRef(control->getPtrCounter()) {}
  explicit PtrTargetRef(WeakCell* cell): PtrTargetRef(cell->getPtrCounter()) {}
  PtrTargetRef(const PtrTargetRef& other): ptrCounter(other.ptrCounter.get()) { inc(); }
  PtrTargetRef(PtrTargetRef&& other) noexcept: ptrCounter(other.ptrCounter.get()) {
    other.ptrCounter.reset();
  }
  PtrTargetRef& operator=(PtrTargetRef&& other) noexcept {
    if (this == &other) return *this;
    dec();
    ptrCounter.set(other.ptrCounter.get());
    other.ptrCounter.reset();
    return *this;
  }
  ~PtrTargetRef() { dec(); }

private:
  explicit PtrTargetRef(PtrCounter* ptrCounter): ptrCounter(ptrCounter) { inc(); }
  inline void inc() { if (auto* ptr = ptrCounter.get()) ptr->inc(); }
  inline void dec() { if (auto* ptr = ptrCounter.get()) ptr->dec(); }

  KJ_NO_UNIQUE_ADDRESS PtrCounterPtr ptrCounter;
};

}  // namespace _ (private)

// =======================================================================================
// Own<T> -- An owned pointer.

template <typename T, typename StaticDisposer = decltype(nullptr)>
class Own;

template <typename T>
class Own<T, decltype(nullptr)> {
  // A transferrable title to a T.  When an Own<T> goes out of scope, the object's Disposer is
  // called to dispose of it.  An Own<T> can be efficiently passed by move, without relocating the
  // underlying object; this transfers ownership.
  //
  // This is much like std::unique_ptr, except:
  // - You cannot release().  An owned object is not necessarily allocated with new (see next
  //   point), so it would be hard to use release() correctly.
  // - The deleter is made polymorphic by virtual call rather than by template.  This is much
  //   more powerful -- it allows the use of custom allocators, freelists, etc.  This could
  //   _almost_ be accomplished with unique_ptr by forcing everyone to use something like
  //   std::unique_ptr<T, kj::Deleter>, except that things get hairy in the presence of multiple
  //   inheritance and upcasting, and anyway if you force everyone to use a custom deleter
  //   then you've lost any benefit to interoperating with the "standard" unique_ptr.

public:
  KJ_DISALLOW_COPY(Own);
  inline Own(): disposer(nullptr), ptr(nullptr) {}
  inline Own(Own&& other) noexcept
      : disposer(other.disposer), ptr(other.ptr) { other.ptr = nullptr; }
  inline Own(Own<RemoveConstOrDisable<T>>&& other) noexcept
      : disposer(other.disposer), ptr(other.ptr) { other.ptr = nullptr; }
  template <typename U, typename = EnableIf<canConvert<U*, T*>()>>
  inline Own(Own<U>&& other) noexcept
      : disposer(other.disposer), ptr(cast(other.ptr)) {
    other.ptr = nullptr;
  }
  template <typename U, typename StaticDisposer, typename = EnableIf<canConvert<U*, T*>()>>
  inline Own(Own<U, StaticDisposer>&& other) noexcept;
  // Convert statically-disposed Own to dynamically-disposed Own.
  inline Own(T* ptr, const Disposer& disposer) noexcept: disposer(&disposer), ptr(ptr) {}

  ~Own() noexcept(false) { dispose(); }

  inline Own& operator=(Own&& other) {
    // Move-assignnment operator.

    // Careful, this might own `other`.  Therefore we have to transfer the pointers first, then
    // dispose.
    const Disposer* disposerCopy = disposer;
    T* ptrCopy = ptr;
    disposer = other.disposer;
    ptr = other.ptr;
    other.ptr = nullptr;
    if (ptrCopy != nullptr) {
      disposerCopy->dispose(const_cast<RemoveConst<T>*>(ptrCopy));
    }
    return *this;
  }

  inline Own& operator=(decltype(nullptr)) {
    dispose();
    return *this;
  }

  template <typename... Attachments> requires (!::kj::_::IsRefcounted<T>)
  Own<T> attach(Attachments&&... attachments) KJ_WARN_UNUSED_RESULT;
  // Returns an Own<T> which points to the same object but which also ensures that all values
  // passed to `attachments` remain alive until after this object is destroyed. Normally
  // `attachments` are other Own<?>s pointing to objects that this one depends on.
  //
  // Note that attachments will eventually be destroyed in the order they are listed. Hence,
  // foo.attach(bar, baz) is equivalent to (but more efficient than) foo.attach(bar).attach(baz).

  template <typename... Attachments> requires (::kj::_::IsRefcounted<T>)
#if KJ_WARN_REFCOUNTED_ATTACH
  KJ_DEPRECATED("using attach() with refcounted objects can be a bug; if intentional, use attachToThisReference()")
#endif
  Own<T> attach(Attachments&&... attachments) KJ_WARN_UNUSED_RESULT;

  template <typename... Attachments> requires (::kj::_::IsRefcounted<T>)
  Own<T> attachToThisReference(Attachments&&... attachments) KJ_WARN_UNUSED_RESULT;
  // Like attach(), but only for objects deriving from kj::Refcounted or kj::AtomicRefcounted.
  // attach() is commonly used to keep dependencies alive for a T value, but this is only reliable
  // if T has a single kj::Own<T>. Refcounted objects can have multiple kj::Own<T>s, so using
  // attach() for their dependencies is a potential source of memory errors.
  //
  // When an attachment only needs to live as long as a single reference to a refcounted
  // object, attachToThisReference() can be called to explicitly opt into this behavior.

  template <typename U>
  Own<U> downcast() {
    // Downcast the pointer to Own<U>, destroying the original pointer.  If this pointer does not
    // actually point at an instance of U, the results are undefined (throws an exception in debug
    // mode if RTTI is enabled, otherwise you're on your own).

    Own<U> result;
    if (ptr != nullptr) {
      result.ptr = &kj::downcast<U>(*ptr);
      result.disposer = disposer;
      ptr = nullptr;
    }
    return result;
  }

#define NULLCHECK KJ_IREQUIRE(ptr != nullptr, "null Own<> dereference")
  inline T* operator->() { NULLCHECK; return ptr; }
  inline const T* operator->() const { NULLCHECK; return ptr; }
  inline _::RefOrVoid<T> operator*() { NULLCHECK; return *ptr; }
  inline _::RefOrVoid<const T> operator*() const { NULLCHECK; return *ptr; }
#undef NULLCHECK
  inline T* get() { return ptr; }
  inline const T* get() const { return ptr; }
  inline operator T*() { return ptr; }
  inline operator const T*() const { return ptr; }

  inline operator Ptr<T>();
  inline Ptr<T> asPtr();
  template <typename U, typename = _::EnableIfCanConvertPtr<T, U>>
  inline operator Ptr<U>();
  template <typename U, typename = _::EnableIfCanConvertPtr<T, U>>
  inline Ptr<U> asPtr();
  // Obtain a non-owning pointer. Creating the first Ptr may allocate lifetime-tracking state.

  inline Weak<T> addWeak();
  // Create a weak pointer which expires when this Own disposes of its object.

  // Surrenders ownership of the underlying object to the caller. The caller must pass in the
  // correct disposer to prove that they know how the object is meant to be disposed of.
  inline T* disown(const Disposer* d) {
    if (d != disposer) _::throwWrongDisposerError();
    T* ptrCopy = ptr;
    ptr = nullptr;
    return ptrCopy;
  }

private:
  const Disposer* disposer;  // Only valid if ptr != nullptr.
  T* ptr;

  inline explicit Own(decltype(nullptr)): disposer(nullptr), ptr(nullptr) {}

  inline bool operator==(decltype(nullptr)) { return ptr == nullptr; }
  // Only called by Maybe<Own<T>>.

  inline void dispose() {
    // Make sure that if an exception is thrown, we are left with a null ptr, so we won't possibly
    // dispose again.
    T* ptrCopy = ptr;
    if (ptrCopy != nullptr) {
      ptr = nullptr;
      disposer->dispose(const_cast<RemoveConst<T>*>(ptrCopy));
    }
  }

  template <typename U>
  static inline T* cast(U* ptr) {
    static_assert(_kj_internal_isPolymorphic((T*)nullptr),
        "Casting owned pointers requires that the target type is polymorphic.");
    return ptr;
  }

  template <typename... Attachments>
  Own<T> attachImpl(Attachments&&... attachments) KJ_WARN_UNUSED_RESULT;

  _::OwnControl* ensureOwnControl();

  template <typename, typename>
  friend class Own;
};

template <>
template <typename U>
inline void* Own<void>::cast(U* ptr) {
  return _::castToVoid(ptr);
}

template <>
template <typename U>
inline const void* Own<const void>::cast(U* ptr) {
  return _::castToConstVoid(ptr);
}

template <typename T, typename StaticDisposer>
class Own {
  // If a `StaticDisposer` is specified (which is not the norm), then the object will be deleted
  // by calling StaticDisposer::dispose(pointer). The pointer passed to `dispose()` could be a
  // superclass of `T`, if the pointer has been upcast.
  //
  // This type can be useful for micro-optimization, if you've found that you are doing excessive
  // heap allocations to the point where the virtual call on destruction is costing non-negligible
  // resources. You should avoid this unless you have a specific need, because it precludes a lot
  // of power.

public:
  KJ_DISALLOW_COPY(Own);
  inline Own(): ptr(nullptr) {}
  inline Own(Own&& other) noexcept
      : ptr(other.ptr) { other.ptr = nullptr; }
  inline Own(Own<RemoveConstOrDisable<T>, StaticDisposer>&& other) noexcept
      : ptr(other.ptr) { other.ptr = nullptr; }
  template <typename U, typename = EnableIf<canConvert<U*, T*>()>>
  inline Own(Own<U, StaticDisposer>&& other) noexcept
      : ptr(cast(other.ptr)) {
    other.ptr = nullptr;
  }
  inline explicit Own(T* ptr) noexcept: ptr(ptr) {}

  ~Own() noexcept(false) {
    if constexpr (noexcept(StaticDisposer::dispose(kj::instance<T*>()))) {
      // dispose doesn't throw, we can be more optimal.
      StaticDisposer::dispose(ptr);
    } else {
      dispose();
    }
  }

  inline Own& operator=(Own&& other) {
    // Move-assignnment operator.

    // Careful, this might own `other`.  Therefore we have to transfer the pointers first, then
    // dispose.
    T* ptrCopy = ptr;
    ptr = other.ptr;
    other.ptr = nullptr;
    if (ptrCopy != nullptr) {
      StaticDisposer::dispose(ptrCopy);
    }
    return *this;
  }

  inline Own& operator=(decltype(nullptr)) {
    dispose();
    return *this;
  }

  template <typename U>
  Own<U, StaticDisposer> downcast() {
    // Downcast the pointer to Own<U>, destroying the original pointer.  If this pointer does not
    // actually point at an instance of U, the results are undefined (throws an exception in debug
    // mode if RTTI is enabled, otherwise you're on your own).

    Own<U, StaticDisposer> result;
    if (ptr != nullptr) {
      result.ptr = &kj::downcast<U>(*ptr);
      ptr = nullptr;
    }
    return result;
  }

#define NULLCHECK KJ_IREQUIRE(ptr != nullptr, "null Own<> dereference")
  inline T* operator->() { NULLCHECK; return ptr; }
  inline const T* operator->() const { NULLCHECK; return ptr; }
  inline _::RefOrVoid<T> operator*() { NULLCHECK; return *ptr; }
  inline _::RefOrVoid<const T> operator*() const { NULLCHECK; return *ptr; }
#undef NULLCHECK
  inline T* get() { return ptr; }
  inline const T* get() const { return ptr; }
  inline operator T*() { return ptr; }
  inline operator const T*() const { return ptr; }

  // Surrenders ownership of the underlying object to the caller. The caller must pass in the
  // correct disposer to prove that they know how the object is meant to be disposed of.
  template<typename SD>
  inline T* disown() {
    static_assert(kj::isSameType<StaticDisposer, SD>(), "disposer must be the same as Own's disposer");
    T* ptrCopy = ptr;
    ptr = nullptr;
    return ptrCopy;
  }

private:
  T* ptr;

  inline explicit Own(decltype(nullptr)): ptr(nullptr) {}

  inline bool operator==(decltype(nullptr)) { return ptr == nullptr; }
  // Only called by Maybe<Own<T>>.

  inline void dispose() {
    // Make sure that if an exception is thrown, we are left with a null ptr, so we won't possibly
    // dispose again.
    T* ptrCopy = ptr;
    if (ptrCopy != nullptr) {
      ptr = nullptr;
      StaticDisposer::dispose(ptrCopy);
    }
  }

  template <typename U>
  static inline T* cast(U* ptr) {
    return ptr;
  }

  template <typename, typename>
  friend class Own;
};

// MaybeTraits specialization for Own<T, D>.
// This enables:
// 1. Niche optimization: Maybe<Own<T>> uses ptr == nullptr as "none", reducing size from 24 to 16.
// 2. Implicit conversion: If U is implicitly convertible to Own<T, D>, then U is implicitly
//    convertible to Maybe<Own<T, D>>. This allows: Maybe<Own<Base>> m = ownDerived;
// 3. Reference conversion: Maybe<Own<T>> can convert to Maybe<U&> when T* converts to U*.
template <typename T, typename D>
struct MaybeTraits<Own<T, D>> {
  // Niche optimization: nullptr is the "none" state
  static void initNone(Own<T, D>* ptr) noexcept { kj::ctor(*ptr); }
  static bool isNone(const Own<T, D>& o) noexcept { return o.get() == nullptr; }

  // Enable converting constructor: Maybe<Own<T>>(U&&) accepts types U convertible to Own<T>.
  // The constructor is implicit when U→Own<T> is implicit (e.g., Own<Derived>→Own<Base>).
  // Example: Maybe<Own<Base>> m = kj::heap<Derived>();
  static constexpr bool convertingConstructor = true;

  // Allow Maybe<Own<T>> -> Maybe<T&> via dereference.
  // This enables: void foo(Maybe<T&> b); foo(maybeOwn);
  static constexpr bool dereferencingConversion = true;

  // Own's move ctor just copies ptr and sets source to nullptr (the none state).
  // Moving a null Own is safe.
  static constexpr bool noneIsMoveSafe = true;
};

namespace _ {  // private

template <typename T>
class HeapDisposer final: public Disposer {
public:
  virtual void disposeImpl(void* pointer) const override { delete reinterpret_cast<T*>(pointer); }

  static const HeapDisposer instance;
};

#if _MSC_VER && _MSC_VER < 1920 && !defined(__clang__)
template <typename T>
__declspec(selectany) const HeapDisposer<T> HeapDisposer<T>::instance = HeapDisposer<T>();
// On MSVC 2017 we suddenly started seeing a linker error on one specific specialization of
// `HeapDisposer::instance` when seemingly-unrelated code was modified. Explicitly specifying
// `__declspec(selectany)` seems to fix it. But why? Shouldn't template members have `selectany`
// behavior by default? We don't know. It works and we're moving on.
#else
template <typename T>
const HeapDisposer<T> HeapDisposer<T>::instance = HeapDisposer<T>();
#endif

template <typename T, void(*F)(T*)>
class CustomDisposer: public Disposer {
public:
  void disposeImpl(void* pointer) const override {
    (*F)(reinterpret_cast<T*>(pointer));
  }
};

template <typename T, void(*F)(T*)>
static constexpr CustomDisposer<T, F> CUSTOM_DISPOSER_INSTANCE {};

}  // namespace _ (private)

template <typename T, typename... Params>
Own<T> heap(Params&&... params) {
  // heap<T>(...) allocates a T on the heap, forwarding the parameters to its constructor.  The
  // exact heap implementation is unspecified -- for now it is operator new, but you should not
  // assume this.  (Since we know the object size at delete time, we could actually implement an
  // allocator that is more efficient than operator new.)

  static_assert(!_::IsRefcounted<T>,
      "Don't use kj::heap() to allocate a refcounted type; use kj::refcounted() or kj::rc() "
      "instead so that the reference count is initialized correctly.");

  return Own<T>(new T(kj::fwd<Params>(params)...), _::HeapDisposer<T>::instance);
}

template <typename T>
Own<Decay<T>> heap(T&& orig) {
  // Allocate a copy (or move) of the argument on the heap.
  //
  // The purpose of this overload is to allow you to omit the template parameter as there is only
  // one argument and the purpose is to copy it.

  typedef Decay<T> T2;
  static_assert(!_::IsRefcounted<T2>,
      "Don't use kj::heap() to allocate a refcounted type; use kj::refcounted() or kj::rc() "
      "instead so that the reference count is initialized correctly.");
  return Own<T2>(new T2(kj::fwd<T>(orig)), _::HeapDisposer<T2>::instance);
}

template <auto F, typename T>
Own<T> disposeWith(T* ptr) {
  // Associate a pre-allocated raw pointer with a corresponding disposal function.
  // The first template parameter should be a function pointer e.g. disposeWith<freeInt>(new int(0)).

  return Own<T>(ptr, _::CUSTOM_DISPOSER_INSTANCE<T, F>);
}

template <typename T, typename... Attachments>
Own<Decay<T>> attachVal(T&& value, Attachments&&... attachments);
// Returns an Own<T> that takes ownership of `value` and `attachments`, and points to `value`.
//
// This is equivalent to heap(value).attach(attachments), but only does one allocation rather than
// two.

template <typename T, typename... Attachments>
Own<T> attachRef(T& value, Attachments&&... attachments);
// Like attach() but `value` is not moved; the resulting Own<T> points to its existing location.
// This is preferred if `value` is already owned by one of `attachments`.
//
// This is equivalent to Own<T>(&value, kj::NullDisposer::instance).attach(attachments), but
// is easier to write and allocates slightly less memory.

// =======================================================================================
// SpaceFor<T> -- assists in manual allocation

template <typename T>
class SpaceFor {
  // A class which has the same size and alignment as T but does not call its constructor or
  // destructor automatically.  Instead, call construct() to construct a T in the space, which
  // returns an Own<T> which will take care of calling T's destructor later.

public:
  inline SpaceFor() {}
  inline ~SpaceFor() {}

  template <typename... Params>
  Own<T> construct(Params&&... params) {
    ctor(value, kj::fwd<Params>(params)...);
    return Own<T>(&value, DestructorOnlyDisposer<T>::instance);
  }

private:
  union {
    T value;
  };
};

// =======================================================================================
// Pin<T>

template <typename T>
class Pin {
  // Pin<T> is a smart, in-place storage for T.
  //
  // Pin<T> should be created on the stack or used as a data member. It should not be
  // allocated on the heap.
  //
  // Pin is designed to be used both in single and multi-threaded context and relies on
  // const-correctness in its implementation:
  // - Pin<const T> and corresponding references types designate an object that is used by multiple
  //   threads.
  // - Pin<non-const T> designates a single-threaded object.
  // Conversion between const and non-const variants are not allowed.
  //
  // Pin<T> is integrated with Ptr<T> and Weak<T>. It is legal to move/destroy only when there are
  // no active Ptr<T>s; outstanding Weak<T>s are nulled instead.
  // When KJ_ASSERT_PTR_COUNTERS is defined, pointers are tracked and validity of these
  // operations are asserted.
  // Weak<T> support adds one pointer of overhead to Pin<T>, and allocates a shared cell lazily when
  // the first weak reference is created.

public:
  template <typename... Params>
  inline Pin(Params&&... params) : t(kj::fwd<Params>(params)...) {  }
  // Create new Pin<T> using corresponding T constructor.

  inline Pin(Pin<T>&& other): t(kj::mv(other.t)) {
    // Move T's ownership.
    // Undefined behavior when live pointers exist, asserted when KJ_ASSERT_PTR_COUNTERS is defined.
    other.target.dispose();
  }

  inline ~Pin() {
    // Destroy a Pin with underlying object.
    // Undefined behavior when live pointers exist, asserted when KJ_ASSERT_PTR_COUNTERS is defined.
    target.dispose();
  }

  inline T* operator->() const { return get(); }
  inline T& operator*() const { return *get(); }
  inline T* get() const { return const_cast<T*>(&t); }

  inline operator Ptr<T>() { return Ptr<T>(this); }
  // Pin<T> can be implicitly converted to Ptr<T> to obtain new pointers.

  inline Ptr<T> asPtr() { return Ptr<T>(this); }
  // Explicit convenience method to create new pointers.

  template <typename U, typename = _::EnableIfCanConvertPtr<T, U>>
  inline operator Ptr<U>() { return Ptr<U>(this); }
  // Pin<T> can be implicitly converted to pointers of compatible types.

  template <typename U, typename = _::EnableIfCanConvertPtr<T, U>>
  inline Ptr<U> asPtr() { return Ptr<U>(this); }
  // Explicit convenience method to create new pointers of compatible types.

  inline Weak<T> addWeak() { return Weak<T>(this); }
  // Create a new weak pointer. Weak pointers do not prevent this Pin from moving or being
  // destroyed or moved; they become null when this Pin is destroyed or moved.

  void* operator new(size_t count) = delete;
  void* operator new[](size_t count) = delete;
  // Pin<T> can't be heap allocated, only local or data field usage is ok.

private:
  KJ_DISALLOW_COPY(Pin);

  inline Pin(T&& t): t(kj::mv(t)) {}

  inline _::WeakCell* getWeakCell() {
    return target.getWeakCell(&t);
  }

  T t;
  PtrTarget target;

  template <typename>
  friend class Ptr;
  template <typename>
  friend class Weak;
};

// =======================================================================================
// Ptr<T>

template <typename T>
class Ptr {
  // Ptr<T> is a non-owning smart alternative to T*. Debug-memory builds track outstanding
  // pointers and diagnose destruction of their referent. Optimized builds store exactly a T*.

public:
  ~Ptr() = default;

  Ptr(Ptr&& other) noexcept: ptr(other.ptr), target(kj::mv(other.target)) {
    other.ptr = nullptr;
  }

  template <typename U, typename = _::EnableIfCanConvertPtr<U, T>>
  Ptr(Ptr<U>&& other) noexcept: ptr(other.ptr), target(kj::mv(other.target)) {
    other.ptr = nullptr;
  }

  Ptr(const Ptr&) = default;

  template <typename U, typename = _::EnableIfCanConvertPtr<U, T>>
  Ptr(const Ptr<U>& other): ptr(other.ptr), target(other.target) {}

  // Ptr<T> can be freely copied.

  inline void operator=(decltype(nullptr)) {
    target = _::PtrTargetRef();
    ptr = nullptr;
  }

  inline T* operator->() const { return get(); }
  inline T* get() const { return ptr; }

  inline bool operator==(const Pin<T>& other) const { return get() == other.get(); }
  inline bool operator==(const Ptr<T>& other) const { return get() == other.get(); }
  inline bool operator==(const T* const other) const { return get() == other; }

  template <typename U>
  inline bool operator==(const Pin<U>& other) const { return get() == other.get(); }

  template <typename U>
  inline bool operator==(const Ptr<U>& other) const { return get() == other.get(); }

  inline _::RefOrVoid<T> asRef() const { return *get(); }
  // Obtain a `T&` reference.
  // This is an unsafe operation and should be avoided unless absolutely necessary.
  // It is undefined behavior to use the reference after the object managed by this Ptr<T>
  // ceased to exist.

private:
  inline explicit Ptr(decltype(nullptr)) noexcept: ptr(nullptr), target() {}

  inline explicit Ptr(T* ptr) noexcept: ptr(ptr), target() {}

  template <typename U, typename = _::EnableIfCanConvertPtr<U, T>>
  inline Ptr(Pin<U>* pin): ptr(pin->get()), target(&pin->target) {}

  inline Ptr(T* ptr, _::WeakCell* cell): ptr(ptr), target(cell) {}

  inline Ptr(T* ptr, _::OwnControl* control): ptr(ptr), target(control) {}

  inline Ptr(T* ptr, PtrTarget* target): ptr(ptr), target(target) {}
  // Construct a Ptr that refers directly to a PtrTarget-derived object. Used by
  // PtrTarget::addPtrToThis().

  T* ptr;
  KJ_NO_UNIQUE_ADDRESS _::PtrTargetRef target;

  template <typename>
  friend class Ptr;
  template <typename>
  friend class Pin;
  template <typename>
  friend class Weak;
  template <typename, typename>
  friend class Own;
  friend class PtrTarget;
  friend struct MaybeTraits<Ptr<T>>;
};

// MaybeTraits specialization for Ptr<T>.
// This enables niche optimization: Maybe<Ptr<T>> uses ptr == nullptr as "none".
template <typename T>
struct MaybeTraits<Ptr<T>> {
  static void initNone(Ptr<T>* ptr) noexcept { new (ptr, _::PlacementNew()) Ptr<T>(nullptr); }
  static bool isNone(const Ptr<T>& p) noexcept { return p.ptr == nullptr; }

  // Ptr's move ctor just copies ptr/counter and sets source.ptr to nullptr. Moving a null Ptr is
  // safe when the null state is constructed via initNone().
  static constexpr bool noneIsMoveSafe = true;

  // Allow `Maybe<Ptr<T>>` to be constructed from types convertible to `Ptr<T>`, like `Ptr<U>`.
  static constexpr bool convertingConstructor = true;
};

// =======================================================================================
// Weak<T>

template <typename T>
class Weak {
  // Weak<T> is a smart alternative to T& with expiration detection.
  //
  // Weak<T> is obtained from Pin<T>::addWeak(), Own<T>::addWeak(), or
  // PtrTarget::addWeakToThis(). It does not keep the target alive or prevent a Pin from moving; it
  // expires when the target is destroyed or the Pin is moved.
  // Common usage:
  // - KJ_IF_SOME on Weak<T> upgrades to Ptr<T>
  // - assertLive() obtains T& and throws on expired Weak<T>
  // - tryGet() obtains Maybe<T&> directly.
  // - upgrade() method upgrades to Maybe<Ptr<T>>

  static_assert(!isConst<T>(),
      "Weak<const T> signifies multi-threaded uses and is not implemented yet.");

public:
  inline Weak() noexcept = default;
  inline Weak(decltype(nullptr)) noexcept: cell(nullptr), ptr(nullptr) {}

  inline ~Weak() { dispose(); }

  Weak(Weak&& other) noexcept {
    kj::swp(cell, other.cell);
    kj::swp(ptr, other.ptr);
  }

  Weak(const Weak& other): cell(other.cell), ptr(other.ptr) {
    if (cell != nullptr) {
      cell->addRef();
    }
  }

  template <typename U, typename = _::EnableIfCanConvertPtr<U, T>>
  Weak(Weak<U>&& other) noexcept: ptr(other.ptr) {
    kj::swp(cell, other.cell);
    other.ptr = nullptr;
  }

  template <typename U, typename = _::EnableIfCanConvertPtr<U, T>>
  Weak(const Weak<U>& other): cell(other.cell), ptr(other.ptr) {
    if (cell != nullptr) {
      cell->addRef();
    }
  }

  inline Weak& operator=(decltype(nullptr)) {
    dispose();
    return *this;
  }

  Weak& operator=(Weak&& other) {
    if (this == &other) return *this;
    kj::swp(cell, other.cell);
    kj::swp(ptr, other.ptr);
    other.dispose();
    return *this;
  }

  template <typename U, typename = _::EnableIfCanConvertPtr<U, T>>
  Weak& operator=(Weak<U>&& other) {
    Weak tmp(kj::mv(other));
    kj::swp(cell, tmp.cell);
    kj::swp(ptr, tmp.ptr);
    return *this;
  }

  inline bool operator==(Pin<T>& other) const { return get() == other.get(); }
  inline bool operator==(const Weak<T>& other) const { return get() == other.get(); }
  inline bool operator==(const T* other) const { return get() == other; }

  template <typename U>
  inline bool operator==(Pin<U>& other) const { return get() == other.get(); }

  template <typename U>
  inline bool operator==(const Weak<U>& other) const { return get() == other.get(); }

  inline T& assertLive() const {
    // Obtain a `T&` reference, checking that the referent is still alive.
    T* ptr = get();
    KJ_IREQUIRE(ptr != nullptr, "null Weak<> dereference");
    return *ptr;
  }

  inline Maybe<T&> tryGet() const { return get(); }
  // Obtain a reference if the referent is still alive, otherwise return none.

  inline Maybe<Ptr<T>> upgrade() const {
    // Obtain a strong pointer if the referent is still alive, otherwise return none.
    if (get() == nullptr) {
      return kj::none;
    }
    return Ptr<T>(ptr, cell);
  }

private:
  _::WeakCell* cell = nullptr;
  T* ptr = nullptr;

  inline Weak(Pin<T>* pin): cell(pin->getWeakCell()), ptr(pin->get()) {
    cell->addRef();
  }

  inline Weak(T* ptr, _::WeakCell* cell): cell(cell), ptr(ptr) {
    if (cell != nullptr) {
      cell->addRef();
    }
  }

  inline Weak(T* ptr, PtrTarget* target): cell(target->getWeakCell(ptr)), ptr(ptr) {
    // Construct a Weak that refers directly to a PtrTarget-derived object. Used by
    // PtrTarget::addWeakToThis().
    cell->addRef();
  }

  inline void dispose() {
    if (cell != nullptr) {
      cell->decRef();
      cell = nullptr;
      ptr = nullptr;
    }
  }

  inline T* get() const {
    if (cell == nullptr || cell->ptr == nullptr) {
      return nullptr;
    }
    return ptr;
  }

  template <typename>
  friend class Pin;
  template <typename>
  friend class Weak;
  template <typename, typename>
  friend class Own;
  friend class PtrTarget;
  friend struct MaybeTraits<Weak<T>>;
};

template <typename T, typename U>
inline bool operator==(Pin<T>& pin, const Weak<U>& weak) { return weak == pin; }

// MaybeTraits specialization for Weak<T>.
// This enables niche optimization: Maybe<Weak<T>> uses cell == nullptr as "none".
template <typename T>
struct MaybeTraits<Weak<T>> {
  static void initNone(Weak<T>* ptr) noexcept { new (ptr, _::PlacementNew()) Weak<T>(nullptr); }
  static bool isNone(const Weak<T>& p) noexcept { return p.cell == nullptr; }

  // Weak's move ctor copies cell and sets source.cell to nullptr. Moving a null Weak is safe.
  static constexpr bool noneIsMoveSafe = true;

  // Allow `Maybe<Weak<T>>` to be constructed from types convertible to `Weak<T>`, like `Weak<U>`.
  static constexpr bool convertingConstructor = true;
};

namespace _ {  // private

template <typename T>
inline NullableValue<Ptr<T>> readMaybe(Weak<T>& weak) { return readMaybe(weak.upgrade()); }
template <typename T>
inline NullableValue<Ptr<T>> readMaybe(const Weak<T>& weak) {
  return readMaybe(weak.upgrade());
}
template <typename T>
inline NullableValue<Ptr<T>> readMaybe(Weak<T>&& weak) { return readMaybe(weak.upgrade()); }

}  // namespace _ (private)

// =======================================================================================
// Inline implementation details

template <typename T>
KJ_DISPOSE_ATTR void Disposer::dispose(T* object) const {
  if constexpr (_kj_internal_isPolymorphic((T*)nullptr)) {
    // Note that dynamic_cast<void*> does not require RTTI to be enabled, because the offset to
    // the top of the object is in the vtable -- as it obviously needs to be to correctly implement
    // operator delete.
    disposeImpl(dynamic_cast<void*>(object));
  } else {
    disposeImpl(static_cast<void*>(object));
  }
}

namespace _ {  // private

template <typename... T>
struct OwnedBundle;

template <>
struct OwnedBundle<> {};

template <typename First, typename... Rest>
struct OwnedBundle<First, Rest...>: public OwnedBundle<Rest...> {
  OwnedBundle(First&& first, Rest&&... rest)
      : OwnedBundle<Rest...>(kj::fwd<Rest>(rest)...), first(kj::fwd<First>(first)) {}

  // Note that it's intentional that `first` is destroyed before `rest`. This way, doing
  // ptr.attach(foo, bar, baz) is equivalent to ptr.attach(foo).attach(bar).attach(baz) in terms
  // of destruction order (although the former does fewer allocations).
  Decay<First> first;
};

template <typename... T>
struct DisposableOwnedBundle final: public Disposer, public OwnedBundle<T...> {
  DisposableOwnedBundle(T&&... values): OwnedBundle<T...>(kj::fwd<T>(values)...) {}
  void disposeImpl(void* pointer) const override { delete this; }
};

template <typename T, typename StaticDisposer>
class StaticDisposerAdapter final: public Disposer {
  // Adapts a static disposer to be called dynamically.
public:
  virtual void disposeImpl(void* pointer) const override {
    StaticDisposer::dispose(reinterpret_cast<T*>(pointer));
  }

  static const StaticDisposerAdapter instance;
};

template <typename T, typename D>
const StaticDisposerAdapter<T, D> StaticDisposerAdapter<T, D>::instance =
    StaticDisposerAdapter<T, D>();

template <typename T>
class OwnControlBundle final: public Disposer {
  // Adds lifetime bookkeeping to an Own without adding a field to every Own. This is allocated
  // lazily when the first Weak is requested (or when debug pointer tracking needs it), and takes
  // over disposal of the original Own.
public:
  explicit OwnControlBundle(Own<T>&& own): own(kj::mv(own)) {}

private:
  void disposeImpl(void*) const override { delete this; }
  OwnControl* getOwnControl() const override {
    return const_cast<OwnControl*>(&control);
  }

  Own<T> own;
  // Declared after own so it expires weak pointers before disposing of the object.
  OwnControl control;
};

}  // namespace _ (private)

template <typename T>
_::OwnControl* Own<T>::ensureOwnControl() {
  if (ptr == nullptr) return nullptr;
  if (auto* control = disposer->getOwnControl()) return control;

  T* ptrCopy = ptr;
  auto* bundle = new _::OwnControlBundle<T>(kj::mv(*this));
  disposer = bundle;
  ptr = ptrCopy;
  return disposer->getOwnControl();
}

template <typename T>
inline Own<T>::operator Ptr<T>() { return asPtr(); }
template <typename T>
inline Ptr<T> Own<T>::asPtr() {
  if (ptr == nullptr) return Ptr<T>(nullptr);
#if KJ_ASSERT_PTR_COUNTERS
  return Ptr<T>(ptr, ensureOwnControl());
#else
  // important not to allocate control in opt mode
  return Ptr<T>(ptr);
#endif
}
template <typename T>
template <typename U, typename>
inline Own<T>::operator Ptr<U>() { return asPtr<U>(); }
template <typename T>
template <typename U, typename>
inline Ptr<U> Own<T>::asPtr() {
  if (ptr == nullptr) return Ptr<U>(nullptr);
#if KJ_ASSERT_PTR_COUNTERS
  return Ptr<U>(ptr, ensureOwnControl());
#else
  // important not to allocate control in opt mode
  return Ptr<U>(ptr);
#endif
}
template <typename T>
inline Weak<T> Own<T>::addWeak() {
  if (ptr == nullptr) return nullptr;
  return Weak<T>(ptr, ensureOwnControl()->getWeakCell(ptr));
}

template <typename T>
template <typename... Attachments> requires (!::kj::_::IsRefcounted<T>)
Own<T> Own<T>::attach(Attachments&&... attachments) {
  return attachImpl(kj::fwd<Attachments>(attachments)...);
}

template <typename T>
template <typename... Attachments> requires (::kj::_::IsRefcounted<T>)
Own<T> Own<T>::attach(Attachments&&... attachments) {
  // TODO(someday): statically assert against IsRefcounted().
  return attachImpl(kj::fwd<Attachments>(attachments)...);
}

template <typename T>
template <typename... Attachments> requires (::kj::_::IsRefcounted<T>)
Own<T> Own<T>::attachToThisReference(Attachments&&... attachments) {
  return attachImpl(kj::fwd<Attachments>(attachments)...);
}

template <typename T>
template <typename... Attachments>
Own<T> Own<T>::attachImpl(Attachments&&... attachments) {
  T* ptrCopy = ptr;

  KJ_IREQUIRE(ptrCopy != nullptr, "cannot attach to null pointer");

  // HACK: If someone accidentally calls .attach() on a null pointer in opt mode, try our best to
  //   accomplish reasonable behavior: We turn the pointer non-null but still invalid, so that the
  //   disposer will still be called when the pointer goes out of scope.
  if (ptrCopy == nullptr) ptrCopy = reinterpret_cast<T*>(1);

  auto bundle = new _::DisposableOwnedBundle<Own<T>, Attachments...>(
      kj::mv(*this), kj::fwd<Attachments>(attachments)...);
  return Own<T>(ptrCopy, *bundle);
}

template <typename T, typename... Attachments>
Own<T> attachRef(T& value, Attachments&&... attachments) {
  // TODO(someday): maybe also assert against T deriving from kj::Refcounted here?
  auto bundle = new _::DisposableOwnedBundle<Attachments...>(kj::fwd<Attachments>(attachments)...);
  return Own<T>(&value, *bundle);
}

template <typename T, typename... Attachments>
Own<Decay<T>> attachVal(T&& value, Attachments&&... attachments) {
  // TODO(someday): maybe also assert against T deriving from kj::Refcounted here?
  auto bundle = new _::DisposableOwnedBundle<T, Attachments...>(
      kj::fwd<T>(value), kj::fwd<Attachments>(attachments)...);
  return Own<Decay<T>>(&bundle->first, *bundle);
}

template <typename T>
template <typename U, typename StaticDisposer, typename>
inline Own<T>::Own(Own<U, StaticDisposer>&& other) noexcept
    : ptr(cast(other.ptr)) {
  if (_::castToVoid(other.ptr) != reinterpret_cast<void*>(other.ptr)) {
    // Oh dangit, there's some sort of multiple inheritance going on and `StaticDisposerAdapter`
    // won't actually work because it'll receive a pointer pointing to the top of the object, which
    // isn't exactly the same as the `U*` pointer it wants. We have no choice but to allocate
    // a dynamic disposer here.
    disposer = new _::DisposableOwnedBundle<Own<U, StaticDisposer>>(kj::mv(other));
  } else {
    disposer = &_::StaticDisposerAdapter<U, StaticDisposer>::instance;
    other.ptr = nullptr;
  }
}

}  // namespace kj

KJ_END_HEADER
