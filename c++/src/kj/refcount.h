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
#include "atomic.h"

KJ_BEGIN_HEADER

namespace kj {

// =======================================================================================
// Non-atomic (thread-unsafe) refcounting

template<typename T>
class Rc;

template<typename T>
class WeakRc;

template <typename T, typename... Params>
Rc<T> rc(Params&&... params);

class Refcounted;
class AtomicRefcounted;

namespace _ {  // private

template <typename T> class RcWrapper;
template <typename T> class RcOwnWrapper;
template <typename T, typename Owner, bool = isPointerType<T>()> class HandleImpl;

class RcWeakCell {
  // Shared validity cell backing kj::WeakRc<T>, the weak companion of kj::Rc<T>.
  //
  // The strong side (the Refcounted object) owns one reference while the referent is alive; each
  // WeakRc owns one reference. When the last strong reference is dropped, `refcounted` is nulled
  // before the strong-side reference is released, allowing outstanding WeakRc pointers to observe
  // expiration safely. The cell outlives the referent so that expiration can be detected, and is
  // freed once both the strong side and all WeakRc references are gone.

public:
  explicit RcWeakCell(Refcounted* refcounted): refcounted(refcounted) {}

  inline void addRef() { ++refcount; }
  inline void decRef() { if (--refcount == 0) { delete this; } }

  Refcounted* refcounted;
  // The live Refcounted object, or nullptr once the last strong reference has been dropped.

private:
  size_t refcount = 1;
  // Starts at 1 to account for the strong-side reference held while `refcounted` is non-null.
};

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

  inline auto addWeakToThis(this auto&& self) {
    return addWeakRefInternal(&self);
  }

private:
  mutable uint refcount = 1;
  // A Refcounted object is born with a reference count of 1: it always comes into existence owned
  // by exactly one strong reference (the Own<T>/Rc<T> returned by kj::refcounted()/kj::rc()). This
  // means the object is in a valid, fully-counted state throughout its constructor, so addRefToThis()
  // and addWeakToThis() may be called from within the constructor.
  // "mutable" because disposeImpl() is const.  Bleh.

  mutable _::RcWeakCell* weakCell = nullptr;
  // Lazily-allocated shared cell backing any kj::WeakRc<T> referencing this object. Nulled and
  // released when the last strong reference is dropped (see disposeImpl()).
  // "mutable" because disposeImpl() is const.

  inline void incRefcount() const { ++refcount; }

  void disposeImpl(void* pointer) const override;

  inline _::RcWeakCell* getWeakCell() {
    // Lazily allocate (or return the existing) weak cell for this object.
    if (weakCell == nullptr) {
      weakCell = new _::RcWeakCell(this);
    }
    return weakCell;
  }

  template <typename T>
  static Own<T> addRefInternal(T* object);

  template <typename T>
  static Rc<T> addRcRefInternal(T* object);

  template <typename T>
  static WeakRc<T> addWeakRefInternal(T* object);

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
  friend class WeakRc;

  template <typename T> friend class _::RcWrapper;
  template <typename T> friend class _::RcOwnWrapper;
  template <typename T, typename Owner, bool> friend class _::HandleImpl;
};

template <typename T, typename... Params>
inline Own<T> refcounted(Params&&... params) {
  // Allocate a new refcounted instance of T, passing `params` to its constructor.  Returns an
  // initial reference to the object.  More references can be created with `kj::addRef()`.
  T* object = new T(kj::fwd<Params>(params)...);
  return Own<T>(object, *static_cast<Refcounted*>(object));
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
  return Rc<T>(refcounted, *object);
}

namespace _ {  // private

template <typename T>
class RcWrapper final: public Refcounted {
public:
  template <typename... Params>
  explicit RcWrapper(Params &&...params): wrapped(kj::fwd<Params>(params)...) {}
  T* getWrappedPtr() { return &wrapped; }
  const T *getWrappedPtr() const { return &wrapped; }

private:
  T wrapped;
};

template <typename T>
class RcOwnWrapper final: public Refcounted {
public:
  explicit RcOwnWrapper(Own<T> &&wrapped): wrapped(kj::mv(wrapped)) {}
  T* getWrappedPtr() { return wrapped.get(); }
  const T *getWrappedPtr() const { return wrapped.get(); }

private:
  Own<T> wrapped;
};

template <typename T>
struct RemoveReference { using Type = T; };
template <typename T>
struct RemoveReference<T&> { using Type = T; };
template <typename T>
struct RemoveReference<T&&> { using Type = T; };

template <typename Result>
using ProjectionType = typename RemoveReference<Result>::Type;

template <typename Result>
constexpr bool isProjectionResult() {
  using T = ProjectionType<Result>;
  if constexpr (isPointerType<T>()) {
    return (isLvalueReference<Result>() || isSameType<Result, T>()) &&
        isNoThrowMoveConstructible<RemoveConst<T>, Result>();
  } else {
    return isLvalueReference<Result>();
  }
}

template <typename U, typename T>
constexpr bool canConvertRc() {
  // Whether an Rc/Arc of U converts to one of T: raw pointer conversion for ordinary objects,
  // noexcept value conversion for pointer types. The two families never convert into each other.
  if constexpr (isSameType<U, T>()) {
    // The non-template move constructor handles this case. In particular, checking whether a
    // const pointer type converts to itself may try to copy a move-only pointer from const.
    return false;
  } else if constexpr (isPointerType<T>()) {
    return isPointerType<U>() && canConvert<U&&, RemoveConst<T>>() &&
        isNoThrowMoveConstructible<RemoveConst<T>, RemoveConst<U>>();
  } else {
    return !isPointerType<U>() && canConvert<U*, T*>();
  }
}

template <typename T, typename Owner, bool>
class HandleImpl {
  // Shared implementation of Rc<T> and Arc<T>; see the RcImpl and ArcImpl aliases below. A handle
  // is one reference to an `Owner` plus the target (a T) that the handle exposes.
  //
  // `Owner` is the refcounted base class of the object whose reference count this handle
  // participates in: kj::Refcounted for Rc, const kj::AtomicRefcounted for Arc. For a plain
  // kj::rc<T>() the owner is the T itself (or the RcWrapper around it); for a handle produced by
  // project() it is the original object that the projection was taken from, which is what keeps
  // the target alive. Both base classes are their own Disposer, so the reference is a single
  // pointer and releasing it is `owner->dispose(nullptr)`.
  //
  // Ordinary targets are referenced by raw pointer and need not be complete types. Pointer types
  // (PointerTraits) use the specialization below, which stores them inline. Both variants have the
  // same interface, so Rc and Arc are each written once for both kinds of T.
public:
  HandleImpl() = default;
  HandleImpl(Owner* owner, T& target): owner(owner), ptr(&target) {}
  KJ_DISALLOW_COPY(HandleImpl);
  HandleImpl(HandleImpl&& other) noexcept: ptr(other.ptr) { owner = other.release(); }
  template <typename U>
  HandleImpl(HandleImpl<U, Owner>&& other) noexcept: ptr(other.ptr) { owner = other.release(); }
  ~HandleImpl() noexcept(false) { dispose(); }

  HandleImpl& operator=(HandleImpl&& other) {
    // Take the new claim before dropping the previous one: disposing it may destroy `other`.
    HandleImpl previous(kj::mv(*this));
    ptr = other.ptr;
    owner = other.release();
    return *this;
  }

  Owner* getOwner() const { return owner; }
  T* get() const { return ptr; }

  HandleImpl clone() const {
    if (owner == nullptr) return {};
    owner->incRefcount();
    return HandleImpl(owner, *ptr);
  }

  Own<T> toOwn() {
    if (owner == nullptr) return Own<T>();
    auto result = Own<T>(ptr, *owner);
    release();
    return result;
  }

  Owner* release() {
    // Detach the claim without disposing it. The caller becomes responsible for the claim.
    auto result = owner;
    owner = nullptr;
    ptr = nullptr;
    return result;
  }
  void dispose() {
    if (auto released = release(); released != nullptr) {
      released->dispose(static_cast<RemoveConst<Owner>*>(nullptr));
    }
  }

private:
  Owner* owner = nullptr;
  T* ptr = nullptr;
  template <typename, typename, bool> friend class HandleImpl;
};

template <typename T, typename Owner>
class HandleImpl<T, Owner, true> {
  // Pointer type variant. `value` is constructed exactly while `owner` is non-null, so no extra
  // flag is needed. Since a copy of a pointer type is as good as the original, moving the handle
  // moves the value while transferring the reference.
  //
  // The value is always destroyed before its claim is released: a tracked ArrayPtr must unregister
  // before its backing Array's destructor asserts that no ArrayPtrs remain.
  using Value = RemoveConst<T>;
  static_assert(noexcept(Value(kj::instance<Value&>())) &&
      noexcept(Value(kj::instance<Value&&>())) &&
      noexcept(kj::instance<Value&>().~Value()),
      "pointer types must be copyable, movable, and destructible without throwing");
public:
  HandleImpl() {}
  template <typename U>
  HandleImpl(Owner* owner, U&& pointer): owner(owner) {
    static_assert(isNoThrowMoveConstructible<Value, U>(),
        "pointer types must be constructible from the supplied target without throwing");
    kj::ctor(value, kj::fwd<U>(pointer));
  }
  KJ_DISALLOW_COPY(HandleImpl);
  HandleImpl(HandleImpl&& other) noexcept { moveFrom(other); }
  template <typename U>
  HandleImpl(HandleImpl<U, Owner>&& other) noexcept { moveFrom(other); }
  ~HandleImpl() noexcept(false) { dispose(); }

  HandleImpl& operator=(HandleImpl&& other) {
    HandleImpl previous(kj::mv(*this));
    moveFrom(other);
    return *this;
  }

  Owner* getOwner() const { return owner; }
  T* get() { return owner == nullptr ? nullptr : &value; }
  const T* get() const { return owner == nullptr ? nullptr : &value; }

  HandleImpl clone(this auto& self) {
    // Copies from a mutable `value` when possible: Rc<const Pointer> still owns a mutable Value,
    // so a non-const Rc can copy a mutable view without exposing mutable access through the Rc.
    if (self.owner == nullptr) return {};
    self.owner->incRefcount();
    return HandleImpl(self.owner, self.value);
  }

  Owner* release() {
    if (owner != nullptr) kj::dtor(value);
    auto result = owner;
    owner = nullptr;
    return result;
  }
  void dispose() {
    if (auto released = release(); released != nullptr) {
      released->dispose(static_cast<RemoveConst<Owner>*>(nullptr));
    }
  }

private:
  Owner* owner = nullptr;
  union { Value value; };

  template <typename U>
  void moveFrom(HandleImpl<U, Owner>& other) {
    // Precondition: *this is empty.
    static_assert(isNoThrowMoveConstructible<Value, RemoveConst<U>>(),
        "pointer-type conversions must be noexcept");
    if (other.owner != nullptr) {
      kj::ctor(value, kj::mv(other.value));
      owner = other.release();
    }
  }

  template <typename, typename, bool> friend class HandleImpl;
};

template <typename T, bool = isPointerType<T>() && PointerTraits<RemoveConst<T>>::isReadOnly>
struct RcExposed { using Type = T; };
template <typename T>
struct RcExposed<T, true> { using Type = const T; };
// The type Rc<T> exposes through non-const access: read-only pointer types are exposed as const.

template <typename T>
using RcImpl = HandleImpl<T, Refcounted>;
template <typename T>
using ArcImpl = HandleImpl<T, const AtomicRefcounted>;

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
  // - T for ordinary non-`Refcounted` Ts with move constructor. Allocates a wrapper.
  //
  // Pointer types (registered via PointerTraits, e.g. ArrayPtr or Cap'n Proto readers) are stored
  // inline: the Rc holds the pointer by value plus a claim on the owner of the data it points to.
  // Create these by projecting from an owner that covers the pointer's target and context:
  //
  //     auto bytes = owner.addRef().project([](auto& array) { return array.asPtr(); });
  //
  // Returning a pointer type by value copies it, sharing the original refcount without an
  // allocation. rc<Pointer>(...) is not supported because a bare pointer does not identify its
  // owner. Copying *rc only borrows the pointer; addRef() copies ownership as well. The address of
  // the inline pointer changes when the Rc moves, so only null comparison is provided for pointer
  // types, and neither toOwn() nor WeakRc is available (both would have to point into the Rc
  // itself; downgrade the owner instead). Cross-type Rc/Arc conversions require a noexcept
  // pointer conversion; use project() for conversions that can throw.
  //
  // Read-only pointer types are always exposed as const, as in Arc, so `*rc = other` cannot
  // re-point the inline value. Mutable pointer types (e.g. builders) must be exposed as non-const
  // for their setters; assigning through *rc re-points only that handle, and the new target must
  // stay covered by the same owner.
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
  using Impl = _::RcImpl<T>;
  using Exposed = typename _::RcExposed<T>::Type;
public:
  KJ_DISALLOW_COPY(Rc);
  Rc() { }
  Rc(decltype(nullptr)) { }
  Rc(Rc&& other) noexcept = default;

  template <typename U = T, typename = EnableIf<_::canConvertRc<U, T>()>>
  inline Rc(Rc<U>&& other) noexcept: impl(kj::mv(other.impl)) {}

  template <typename U, typename = EnableIf<isSameType<U, T>() && !isPointerType<T>()>>
  inline Rc(U t) noexcept {
    // This and below do not use concepts, but templates and static_asserts.
    // Concepts require T to be fully defined, but Rc<T> is often used with forward-declared T.
    // This function is declared as template to help msvc in polymorphic base class case.
    static_assert(!canConvert<T*, Refcounted*>());
    auto wrapper = new _::RcWrapper<U>(mv(t));
    impl = Impl(wrapper, *wrapper->getWrappedPtr());
  }

  inline Rc(Own<T> t) noexcept {
    if (t.get() == nullptr) return;
    auto wrapper = new _::RcOwnWrapper<T>(mv(t));
    impl = Impl(wrapper, *wrapper->getWrappedPtr());
  }

  Own<T> toOwn() {
    // Convert Rc<T> to Own<T>.
    // Nullifies the original Rc<T>.
    static_assert(!isPointerType<T>(),
        "toOwn() is not available for pointer types, which live inline in the Rc");
    return impl.toOwn();
  }

  Rc<T> addRef() {
    return Rc(impl.clone());
  }

  Rc<T> clone() {
    return addRef();
  }

  template <typename Func>
  auto project(Func&& func) && {
    // Move this ownership claim to an object contained by the referent. The returned
    // Rc points at the projection, but disposal still applies to the original object. For example,
    // an Rc of a field can be obtained with:
    //
    //     Rc<Person> person = kj::rc<Person>();
    //     Rc<String> name = person.addRef().project(
    //         [](Person& person) -> String& { return person.name; });
    //
    // Like kj::attachRef(person->name, person.addRef()), but returns an Rc sharing the original
    // refcount without allocating an attachment bundle.
    //
    // This operation consumes the Rc. Use addRef().project() to retain the original Rc, or
    // kj::mv(rc).project() to transfer it explicitly. This Rc must not be null, and the callback
    // must return a reference or a registered pointer type whose lifetime is covered by the
    // original ownership claim. Pointer types are copied inline, including when returned by
    // reference, and must be copyable from that reference without throwing. If a const reference
    // to a mutable view cannot be copied, return its read-only view (e.g. asConst()/asReader()).
    // Returning the referent itself is supported; the result is then not a projection.
    // A WeakRc obtained from the result points
    // to the projected object, but expires based on the lifetime of the original object.
    using Result = decltype(kj::fwd<Func>(func)(*get()));
    static_assert(_::isProjectionResult<Result>(),
        "Rc::project() callback must return an lvalue reference or a pointer type; "
        "pointer results must be copyable/movable from the returned value without throwing");
    using U = _::ProjectionType<Result>;

    KJ_IREQUIRE(get() != nullptr, "null Rc<> projection");
    auto owner = kj::mv(*this);
    // Construct the result before releasing `owner`: it may copy from owner's inline value.
    Rc<U> result(owner.impl.getOwner(), kj::fwd<Func>(func)(*owner));
    owner.impl.release();
    return result;
  }

  // Surrenders ownership of the underlying object to the caller. Unlike Own<T>::disown(), there
  // is no need for the caller to prove they know how to dispose of the object, because the object
  // is its own Disposer.
  //
  // A projected Rc cannot be disowned because its pointer does not identify the object whose
  // refcount owns it. disown() throws if this Rc is a projection.
  T* disown() {
    static_assert(canConvert<T*, Refcounted*>());
    KJ_IREQUIRE(impl.getOwner() == static_cast<Refcounted*>(get()),
        "cannot disown a projected Rc");
    T* result = get();
    impl.release();
    return result;
  }

  // Assume ownership of an object without incrementing its refcount. Opposite of disown().
  static Rc reown(T* ptr) {
    static_assert(canConvert<T*, Refcounted*>());
    if (ptr == nullptr) return nullptr;
    return Rc(static_cast<Refcounted*>(ptr), *ptr);
  }

  WeakRc<T> downgrade();
  // Create a weak reference to the referent. The weak reference does not keep the object alive;
  // it expires once the last strong Rc<T> is dropped, but can be upgraded back to an Rc<T> while
  // the object is still alive. See kj::WeakRc<T>.

  WeakRc<T> addWeakRef() { return downgrade(); }
  // Synonym for downgrade().

  Rc& operator=(decltype(nullptr)) {
    impl.dispose();
    return *this;
  }

  Rc& operator=(Rc&& other) {
    if (this != &other) impl = kj::mv(other.impl);
    return *this;
  }

  template <typename U>
  Rc<U> downcast() {
    Rc<U> result(impl.getOwner(), kj::downcast<U>(*get()));
    impl.release();
    return result;
  }

  inline bool operator==(const Rc<T>& other) const requires (!isPointerType<T>()) {
    return get() == other.get();
  }
  inline bool operator==(decltype(nullptr)) const { return get() == nullptr; }

#define NULLCHECK KJ_IREQUIRE(get() != nullptr, "null Rc<> dereference")
  inline Exposed* operator->() { NULLCHECK; return get(); }
  inline const T* operator->() const { NULLCHECK; return get(); }
  inline Exposed& operator*() { NULLCHECK; return *get(); }
  inline const T& operator*() const { NULLCHECK; return *get(); }
#undef NULLCHECK

  inline Exposed* get() { return impl.get(); }
  inline const T* get() const { return impl.get(); }

private:
  explicit Rc(Impl&& impl): impl(kj::mv(impl)) {}
  template <typename U>
  Rc(Refcounted* owner, U&& target): impl(owner, kj::fwd<U>(target)) {}
  // Adopt a claim on `owner`, exposing `target`: a T& for ordinary objects, or a pointer type to
  // copy inline.

  Impl impl;

  friend class Refcounted;

  template <typename U, typename... Params>
  friend Rc<U> rc(Params&&... params);

  template <typename>
  friend class Rc;

  template <typename>
  friend class WeakRc;
};

// MaybeTraits specialization for Rc<T>.
// This enables:
// 1. Niche optimization: Maybe<Rc<T>> uses a null Rc as "none", so it is the same size as
//    Rc<T> itself rather than carrying a separate flag.
// 2. Implicit conversion: If U is implicitly convertible to Rc<T>, then U is implicitly convertible
//    to Maybe<Rc<T>>. This allows: Maybe<Rc<Base>> m = rcDerived;
template <typename T>
struct MaybeTraits<Rc<T>> {
  // Niche optimization: a null Rc is the "none" state.
  static void initNone(Rc<T>* ptr) noexcept { kj::ctor(*ptr); }
  static bool isNone(const Rc<T>& rc) noexcept { return rc.get() == nullptr; }

  // Enable converting constructor: Maybe<Rc<T>>(U&&) accepts types U convertible to Rc<T>.
  static constexpr bool convertingConstructor = true;

  // Disable implicit conversion to the referent.
  static constexpr bool dereferencingConversion = false;

  // Rc's move ctor leaves the source null (the none state), and moving a null Rc is a no-op.
  static constexpr bool noneIsMoveSafe = true;
};

template <typename T, typename... Params>
inline Rc<T> rc(Params&&... params) {
  // Allocate a new refcounted instance of T, passing `params` to its constructor.
  // Returns smart pointer that can be used to manage references.

  static_assert(!isPointerType<T>(), "Allocate the backing owner, then project() to a pointer");
  if constexpr (canConvert<T*, Refcounted*>()) {
    T* object = new T(fwd<Params>(params)...);
    return Rc<T>(static_cast<Refcounted*>(object), *object);
  } else {
    auto wrapper = new _::RcWrapper<T>(fwd<Params>(params)...);
    return Rc<T>(wrapper, *wrapper->getWrappedPtr());
  }
}

template <typename T>
class WeakRc {
  // WeakRc<T> is a weak reference companion to kj::Rc<T>.
  //
  // A WeakRc<T> does not keep its referent alive: it expires once the last strong Rc<T> is
  // dropped. While the referent is still alive, a WeakRc<T> can be upgraded back to a strong
  // Rc<T>. This is useful for breaking reference cycles or for holding a non-owning reference that
  // can detect when the referent has gone away.
  //
  // Obtain a WeakRc<T> via Rc<T>::downgrade() (or its synonym Rc<T>::addWeakRef()). Common usage:
  // - KJ_IF_SOME on WeakRc<T> upgrades to Rc<T>
  // - assertLive() obtains T& and throws on expired WeakRc<T>
  // - tryGet() obtains Maybe<T&> directly
  // - upgrade() (or its synonym addStrongRef()) upgrades to Maybe<Rc<T>>
  //
  // WeakRc<T> is movable but, like kj::Rc<T>, not implicitly copyable; use clone() to make an
  // additional weak reference explicitly. Like kj::Rc<T> it is NOT threadsafe.
  //
  // Upgrading never resurrects a dead object: once the last strong Rc<T> is dropped the referent's
  // refcount reaches zero and is never incremented again. upgrade() only ever produces a strong
  // reference while the refcount is still non-zero. See WeakRc<T>::upgrade().
  //
  // The relationship between Rc<T> and WeakRc<T> is similar to that between kj::Pin<T>/kj::Ptr<T>
  // and kj::Weak<T>.
  static_assert(!isPointerType<T>(),
      "WeakRc does not support pointer types; downgrade the pointer's owner instead");

public:
  KJ_DISALLOW_COPY(WeakRc);
  inline WeakRc(decltype(nullptr)) noexcept {}

  inline ~WeakRc() noexcept(false) { dispose(); }

  WeakRc(WeakRc&& other) noexcept {
    kj::swp(cell, other.cell);
    kj::swp(ptr, other.ptr);
  }

  template <typename U, typename = EnableIf<canConvert<U*, T*>()>>
  WeakRc(WeakRc<U>&& other) noexcept: ptr(other.ptr) {
    kj::swp(cell, other.cell);
    other.ptr = nullptr;
  }

  inline WeakRc(Rc<T>& rc): WeakRc(rc.downgrade()) {}
  inline WeakRc(Rc<T>&& rc): WeakRc(rc.downgrade()) {}

  template <typename U, typename = EnableIf<canConvert<U*, T*>()>>
  inline WeakRc(Rc<U>& rc): WeakRc(rc.downgrade()) {}
  template <typename U, typename = EnableIf<canConvert<U*, T*>()>>
  inline WeakRc(Rc<U>&& rc): WeakRc(rc.downgrade()) {}

  WeakRc<T> clone() {
    // Make an additional weak reference to the same referent.
    return WeakRc<T>(cell, ptr);
  }

  WeakRc<T> addRef() { return clone(); }
  // Make an additional weak reference to the same referent.
  
  WeakRc& operator=(decltype(nullptr)) {
    dispose();
    return *this;
  }

  WeakRc& operator=(WeakRc&& other) {
    if (this == &other) return *this;
    kj::swp(cell, other.cell);
    kj::swp(ptr, other.ptr);
    other.dispose();
    return *this;
  }

  inline bool operator==(decltype(nullptr)) const { return get() == nullptr; }
  inline bool operator==(const WeakRc<T>& other) const { return get() == other.get(); }
  inline bool operator==(const Rc<T>& other) const { return get() == other.get(); }

  template <typename U>
  inline bool operator==(const WeakRc<U>& other) const { return get() == other.get(); }

  inline T& assertLive() const {
    // Obtain a `T&` reference, checking that the referent is still alive.
    T* p = get();
    KJ_IREQUIRE(p != nullptr, "null WeakRc<> dereference");
    return *p;
  }

  inline Maybe<T&> tryGet() const { return get(); }
  // Obtain a reference if the referent is still alive, otherwise return none.

  inline Maybe<Rc<T>> upgrade() const {
    // Obtain a strong Rc<T> if the referent is still alive, otherwise return none.
    if (get() == nullptr) {
      return kj::none;
    }
    // No resurrection: when the last strong Rc<T> is dropped, the refcount reaches zero and
    // Refcounted::disposeImpl() nulls `cell->refcounted` before the object is destroyed. Because
    // (non-atomic) Rc<T> is single-threaded, a non-null `cell->refcounted` therefore guarantees the
    // refcount is still non-zero here (confirmed by get() above), so we never increment a refcount
    // that has already reached zero.
    KJ_IREQUIRE(cell->refcounted->refcount > 0,
        "WeakRc<> must not revive an object whose refcount already reached zero.");
    ++cell->refcounted->refcount;
    return Rc<T>(cell->refcounted, *ptr);
  }

  inline Maybe<Rc<T>> addStrongRef() const { return upgrade(); }
  // Synonym for upgrade().

private:
  _::RcWeakCell* cell = nullptr;
  T* ptr = nullptr;

  inline WeakRc(_::RcWeakCell* cell, T* ptr): cell(cell), ptr(ptr) {
    if (cell != nullptr) {
      cell->addRef();
    }
  }

  inline void dispose() {
    if (cell != nullptr) {
      cell->decRef();
      cell = nullptr;
      ptr = nullptr;
    }
  }

  inline T* get() const {
    if (cell == nullptr || cell->refcounted == nullptr) {
      return nullptr;
    }
    return ptr;
  }

  template <typename>
  friend class Rc;
  template <typename>
  friend class WeakRc;
  friend class Refcounted;
};

template <typename T>
WeakRc<T> Refcounted::addWeakRefInternal(T* object) {
  static_assert(kj::canConvert<T&, Refcounted&>());
  Refcounted* refcounted = object;
  KJ_IREQUIRE(refcounted->refcount > 0,
      "Object not allocated with kj::refcounted() or kj::rc().");
  return WeakRc<T>(refcounted->getWeakCell(), object);
}

template <typename T>
WeakRc<T> Rc<T>::downgrade() {
  if (get() == nullptr) {
    return nullptr;
  }
  return WeakRc<T>(impl.getOwner()->getWeakCell(), get());
}

namespace _ {  // private

template <typename T>
inline NullableValue<Rc<T>> readMaybe(WeakRc<T>& weak) { return readMaybe(weak.upgrade()); }
template <typename T>
inline NullableValue<Rc<T>> readMaybe(const WeakRc<T>& weak) { return readMaybe(weak.upgrade()); }
template <typename T>
inline NullableValue<Rc<T>> readMaybe(WeakRc<T>&& weak) { return readMaybe(weak.upgrade()); }

}  // namespace _ (private)

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

template<typename T>
class Arc;

template<typename T>
class UniqueArc;

template <typename T, typename... Params>
Arc<T> arc(Params&&... params);

template <typename T, typename... Params>
UniqueArc<T> uniqueArc(Params&&... params);

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
    return kj::atomicLoad(&refcount, kj::AtomicMemoryOrder::ACQUIRE) > 1;
  }

protected:
  inline auto addRefToThis(this auto&& self) {
    return addRcRefInternal(&self);
  }

private:
  mutable volatile uint refcount = 0;

  bool addRefWeakInternal() const;

  inline bool hasRefs() const {
    return kj::atomicLoad(&refcount, kj::AtomicMemoryOrder::RELAXED) > 0;
  }

  inline void incRefcount() const {
    kj::atomicAddFetch(&refcount, 1, kj::AtomicMemoryOrder::RELAXED);
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
  template <typename T>
  friend class UniqueArc;
  template <typename T> friend class _::ArcWrapper;
  template <typename T> friend class _::ArcOwnWrapper;
  template <typename T, typename Owner, bool> friend class _::HandleImpl;
  template <typename T, typename... Params>
  friend kj::Arc<T> arc(Params&&... params);
  template <typename T, typename... Params>
  friend kj::UniqueArc<T> uniqueArc(Params&&... params);
};

template <typename T, typename... Params>
inline kj::Own<T> atomicRefcounted(Params&&... params) {
  return AtomicRefcounted::addRefInternal(new T(kj::fwd<Params>(params)...));
}

template <typename T>
kj::Own<T> atomicAddRef(T& object) {
  KJ_IREQUIRE(object.AtomicRefcounted::hasRefs(),
      "Object not allocated with kj::atomicRefcounted().");
  return AtomicRefcounted::addRefInternal(&object);
}

template <typename T>
kj::Own<const T> atomicAddRef(const T& object) {
  KJ_IREQUIRE(object.AtomicRefcounted::hasRefs(),
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
  return kj::Arc<T>(refcounted, *object);
}

namespace _ {  // private

template <typename T>
class ArcWrapper final: public AtomicRefcounted {
public:
  template <typename... Params>
  explicit ArcWrapper(Params&&... params): wrapped(kj::fwd<Params>(params)...) {
    incRefcount();
  }

  T* getWrappedPtr() { return &wrapped; }
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
  //
  // Pointer types are stored inline as with Rc, but must be read-only: project mutable
  // builders/ArrayPtrs to their reader/const-element types first. As with ordinary Arc, the
  // backing context and its destruction must obey their own threading contracts.
  static_assert(!isPointerType<T>() || PointerTraits<RemoveConst<T>>::isReadOnly,
      "Arc requires a read-only pointer type; project to its reader/const-element type");
  using Impl = _::ArcImpl<const T>;

public:
  KJ_DISALLOW_COPY(Arc);
  Arc() { }
  Arc(decltype(nullptr)) { }
  Arc(Arc&& other) noexcept = default;

  template <typename U, typename = EnableIf<_::canConvertRc<U, T>()>>
  inline Arc(Arc<U>&& other) noexcept: impl(kj::mv(other.impl)) {}

  template <typename U = T, typename = EnableIf<isSameType<U, T>() && !isPointerType<T>()>>
  inline Arc(U t) {
    static_assert(!canConvert<const T*, const AtomicRefcounted*>());
    auto wrapper = new _::ArcWrapper<U>(kj::mv(t));
    impl = Impl(wrapper, *wrapper->getWrappedPtr());
  }

  inline Arc(Own<const T> t) {
    static_assert(!canConvert<const T*, const AtomicRefcounted*>());
    if (t.get() == nullptr) return;
    auto wrapper = new _::ArcOwnWrapper<T>(kj::mv(t));
    impl = Impl(wrapper, *wrapper->getWrappedPtr());
  }

  template <typename U, typename = EnableIf<canConvert<U*, T*>()>>
  inline Arc(UniqueArc<U>&& other);
  // Give up uniqueness of a UniqueArc<U> and share it.

  kj::Own<const T> toOwn() {
    // Convert Arc<T> to Own<const T>.
    // Nullifies the original Arc<T>.
    static_assert(!isPointerType<T>(),
        "toOwn() is not available for pointer types, which live inline in the Arc");
    return impl.toOwn();
  }

  kj::Arc<T> addRef() const {
    return Arc(impl.clone());
  }

  kj::Arc<T> clone() const {
    return addRef();
  }

  template <typename Func>
  auto project(Func&& func) && {
    // Move this ownership claim to an object contained by the referent. Arc exposes
    // its referent as const, so the callback receives const T&. The returned Arc points at the
    // projection, but disposal still applies to the original object. For example, an Arc of a field
    // can be obtained with:
    //
    //     Arc<Person> person = kj::arc<Person>();
    //     Arc<const String> name = person.addRef().project(
    //         [](const Person& person) -> const String& { return person.name; });
    //
    // Like kj::attachRef(person->name, person.addRef()), but returns an Arc sharing the original
    // refcount without allocating an attachment bundle.
    //
    // This operation consumes the Arc. Use addRef().project() to retain the original Arc, or
    // kj::mv(arc).project() to transfer it explicitly. This Arc must not be null, and the callback
    // must return a reference or an Arc-compatible pointer type whose lifetime is covered by the
    // original ownership claim. Pointer types are copied inline, including when returned by
    // reference.
    // Returning the referent itself is supported; the result is then not a projection.
    using Result = decltype(kj::fwd<Func>(func)(*get()));
    static_assert(_::isProjectionResult<Result>(),
        "Arc::project() callback must return an lvalue reference or a pointer type; "
        "pointer results must be copyable/movable from the returned value without throwing");
    using U = _::ProjectionType<Result>;

    KJ_IREQUIRE(get() != nullptr, "null Arc<> projection");
    auto owner = kj::mv(*this);
    // Construct the result before releasing `owner`: it may copy from owner's inline value.
    Arc<U> result(owner.impl.getOwner(), kj::fwd<Func>(func)(*owner));
    owner.impl.release();
    return result;
  }

  // Surrenders ownership of the underlying object to the caller. Unlike Own<T>::disown(), there
  // is no need for the caller to prove they know how to dispose of the object, because the object
  // is its own Disposer.
  //
  // A projected Arc cannot be disowned because its pointer does not identify the object whose
  // refcount owns it. disown() throws if this Arc is a projection.
  const T* disown() {
    static_assert(canConvert<const T*, const AtomicRefcounted*>());
    KJ_IREQUIRE(impl.getOwner() == static_cast<const AtomicRefcounted*>(get()),
        "cannot disown a projected Arc");
    const T* result = get();
    impl.release();
    return result;
  }

  // Assume ownership of an object without incrementing its refcount. Opposite of disown().
  static Arc reown(const T* ptr) {
    static_assert(canConvert<const T*, const AtomicRefcounted*>());
    if (ptr == nullptr) return nullptr;
    return Arc(static_cast<const AtomicRefcounted*>(ptr), *ptr);
  }

  Arc& operator=(decltype(nullptr)) {
    impl.dispose();
    return *this;
  }

  Arc& operator=(Arc&& other) {
    if (this != &other) impl = kj::mv(other.impl);
    return *this;
  }

  template <typename U>
  Arc<U> downcast() {
    if (get() == nullptr) return nullptr;
    Arc<U> result(impl.getOwner(), kj::downcast<const U>(*get()));
    impl.release();
    return result;
  }

  inline bool operator==(const Arc<T>& other) const requires (!isPointerType<T>()) {
    return get() == other.get();
  }
  inline bool operator==(decltype(nullptr)) const { return get() == nullptr; }

#define NULLCHECK KJ_IREQUIRE(get() != nullptr, "null Arc<> dereference")
  inline const T* operator->() const { NULLCHECK; return get(); }
  inline const T& operator*() const { NULLCHECK; return *get(); }
#undef NULLCHECK
  inline const T* get() const { return impl.get(); }

private:
  explicit Arc(Impl&& impl): impl(kj::mv(impl)) {}
  template <typename U>
  Arc(const AtomicRefcounted* owner, U&& target): impl(owner, kj::fwd<U>(target)) {}
  // Adopt a claim on `owner`, exposing `target`: a const T& for ordinary objects, or a pointer
  // type to copy inline.

  Impl impl;

  friend class AtomicRefcounted;

  template <typename U, typename... Params>
  friend Arc<U> arc(Params&&... params);

  template <typename>
  friend class Arc;

  template <typename>
  friend class UniqueArc;
};

// MaybeTraits specialization for Arc<T>.
// This enables:
// 1. Niche optimization: Maybe<Arc<T>> uses a null Arc as "none", so it is the same size as
//    Arc<T> itself rather than carrying a separate flag.
// 2. Implicit conversion: If U is implicitly convertible to Arc<T>, then U is implicitly
//    convertible to Maybe<Arc<T>>. This allows: Maybe<Arc<Base>> m = arcDerived;
template <typename T>
struct MaybeTraits<Arc<T>> {
  // Niche optimization: a null Arc is the "none" state.
  static void initNone(Arc<T>* ptr) noexcept { kj::ctor(*ptr); }
  static bool isNone(const Arc<T>& arc) noexcept { return arc.get() == nullptr; }

  // Enable converting constructor: Maybe<Arc<T>>(U&&) accepts types U convertible to Arc<T>.
  static constexpr bool convertingConstructor = true;

  // Disable implicit conversion to the referent.
  static constexpr bool dereferencingConversion = false;

  // Arc's move ctor leaves the source null (the none state), and moving a null Arc is a no-op.
  static constexpr bool noneIsMoveSafe = true;
};

template <typename T, typename... Params>
inline Arc<T> arc(Params&&... params) {
  static_assert(!isPointerType<T>(), "Allocate the backing owner, then project() to a pointer");
  if constexpr (canConvert<T*, AtomicRefcounted*>()) {
    return AtomicRefcounted::addRcRefInternal(new T(kj::fwd<Params>(params)...));
  } else {
    auto wrapper = new _::ArcWrapper<T>(kj::fwd<Params>(params)...);
    return Arc<T>(wrapper, *wrapper->getWrappedPtr());
  }
}

template <typename T>
class UniqueArc {
  // Uniquely-owned Arc<T>.
  //
  // kj::Arc<T> exposes only `const T`, because in kj "const" means "thread-safe" and an Arc may be
  // shared across threads. UniqueArc<T> is the mutable precursor to an Arc<T>: its refcount is
  // known to be exactly 1, so no other thread can observe the object and non-const access is safe.
  //
  // The usual pattern is to allocate with kj::uniqueArc<T>(...), initialize the object through the
  // UniqueArc, then convert it into an Arc<T> to share it:
  //
  //     kj::Arc<Gadget> createGadget(Config& config) {
  //       auto gadget = kj::uniqueArc<Gadget>();
  //       gadget->setName(config.name);
  //       return kj::mv(gadget);                    // or kj::mv(gadget).toArc()
  //     }
  //
  // Conversion is O(1): UniqueArc<T> and Arc<T> share the same representation, and the refcount
  // is already 1. Once converted, the object is only reachable as `const T` again.
  //
  // Like kj::Own<T>, a UniqueArc<T> may be moved to another thread but must not be used from
  // several threads at once.
  //
  // While a UniqueArc<T> is alive, T MUST NOT hand out additional references to itself (e.g. via
  // addRefToThis() or kj::atomicAddRef(*this)). Doing so breaks the uniqueness invariant that makes
  // mutation safe. Uniqueness is asserted in toArc() and in every accessor.
  static_assert(!isPointerType<T>(), "Allocate the backing owner, then project() to a pointer");

public:
  KJ_DISALLOW_COPY(UniqueArc);
  UniqueArc() { }
  UniqueArc(decltype(nullptr)) { }
  inline UniqueArc(UniqueArc&& other) noexcept: refcounted(other.refcounted), ptr(other.ptr) {
    other.refcounted = nullptr;
    other.ptr = nullptr;
  }

  template <typename U, typename = EnableIf<canConvert<U*, T*>()>>
  inline UniqueArc(UniqueArc<U>&& other) noexcept: refcounted(other.refcounted), ptr(other.ptr) {
    other.refcounted = nullptr;
    other.ptr = nullptr;
  }

  template <typename U = T, typename = EnableIf<isSameType<U, T>()>>
  inline UniqueArc(U t) {
    // Wrap a value of a type which is not itself AtomicRefcounted.
    static_assert(!canConvert<const T*, const AtomicRefcounted*>());
    auto wrapper = new _::ArcWrapper<U>(kj::mv(t));
    refcounted = wrapper;
    ptr = wrapper->getWrappedPtr();
  }

  ~UniqueArc() noexcept(false) { dispose(); }

  Arc<T> toArc() && {
    // Give up uniqueness and obtain a shareable Arc<T>. Consumes this UniqueArc, which is null
    // afterwards. Requires this UniqueArc to be non-null.
    KJ_IREQUIRE(ptr != nullptr, "null UniqueArc<> conversion to Arc<>");
    checkUnique();
    Arc<T> result(refcounted, *ptr);
    refcounted = nullptr;
    ptr = nullptr;
    return result;
  }

  UniqueArc& operator=(decltype(nullptr)) {
    dispose();
    return *this;
  }

  UniqueArc& operator=(UniqueArc&& other) {
    if (this == &other) return *this;
    swp(refcounted, other.refcounted);
    swp(ptr, other.ptr);
    other.dispose();
    return *this;
  }

  template <typename U>
  UniqueArc<U> downcast() {
    UniqueArc<U> result;
    if (ptr != nullptr) {
      result = UniqueArc<U>(refcounted, &kj::downcast<U>(*ptr));
      refcounted = nullptr;
      ptr = nullptr;
    }
    return result;
  }

  inline bool operator==(const UniqueArc<T>& other) const { return ptr == other.ptr; }
  inline bool operator==(decltype(nullptr)) const { return ptr == nullptr; }

#define NULLCHECK KJ_IREQUIRE(ptr != nullptr, "null UniqueArc<> dereference")
  inline T* operator->() { NULLCHECK; checkUnique(); return ptr; }
  inline const T* operator->() const { NULLCHECK; checkUnique(); return ptr; }
  inline T& operator*() { NULLCHECK; checkUnique(); return *ptr; }
  inline const T& operator*() const { NULLCHECK; checkUnique(); return *ptr; }
#undef NULLCHECK
  inline T* get() { if (ptr != nullptr) checkUnique(); return ptr; }
  inline const T* get() const { if (ptr != nullptr) checkUnique(); return ptr; }

private:
  UniqueArc(const AtomicRefcounted* refcounted, T* ptr): refcounted(refcounted), ptr(ptr) {}

  inline void checkUnique() const {
    // Assert the uniqueness invariant. `ptr` must be non-null.
    KJ_IREQUIRE(!refcounted->isShared(),
        "UniqueArc<> is no longer unique; the object handed out references to itself.");
  }

  void dispose() {
    if (ptr == nullptr) return;
    const AtomicRefcounted* refcountedCopy = refcounted;
    refcounted = nullptr;
    ptr = nullptr;
    // AtomicRefcounted dispose ignores the pointer.
    refcountedCopy->dispose(static_cast<AtomicRefcounted*>(nullptr));
  }

  const AtomicRefcounted* refcounted = nullptr;
  T* ptr = nullptr;

  template <typename U, typename... Params>
  friend UniqueArc<U> uniqueArc(Params&&... params);

  template <typename>
  friend class UniqueArc;

  template <typename>
  friend class Arc;

  friend struct MaybeTraits<UniqueArc<T>>;
};

// MaybeTraits specialization for UniqueArc<T>.
// This enables:
// 1. Niche optimization: Maybe<UniqueArc<T>> uses ptr == nullptr as "none", so it is the same size
//    as UniqueArc<T> itself rather than carrying a separate flag.
// 2. Implicit conversion: If U is implicitly convertible to UniqueArc<T>, then U is implicitly
//    convertible to Maybe<UniqueArc<T>>. This allows: Maybe<UniqueArc<Base>> m = uniqueArcDerived;
template <typename T>
struct MaybeTraits<UniqueArc<T>> {
  // Niche optimization: a null UniqueArc is the "none" state.
  static void initNone(UniqueArc<T>* ptr) noexcept { kj::ctor(*ptr); }
  static bool isNone(const UniqueArc<T>& uniqueArc) noexcept {
    // Read `ptr` directly rather than via get(): get() asserts the uniqueness invariant, which is
    // irrelevant to (and must not throw from) a none-check.
    return uniqueArc.ptr == nullptr;
  }

  // Enable converting constructor: Maybe<UniqueArc<T>>(U&&) accepts types U convertible to
  // UniqueArc<T>.
  static constexpr bool convertingConstructor = true;

  // Disable implicit conversion to the referent.
  static constexpr bool dereferencingConversion = false;

  // UniqueArc's move ctor just copies the pointers and sets the source to nullptr (the none state).
  // Moving a null UniqueArc is safe.
  static constexpr bool noneIsMoveSafe = true;
};

template <typename T>
template <typename U, typename>
inline Arc<T>::Arc(UniqueArc<U>&& other): Arc(kj::mv(other).toArc()) {}

template <typename T, typename... Params>
inline UniqueArc<T> uniqueArc(Params&&... params) {
  // Allocate a new instance of T, passing `params` to its constructor, exactly like kj::arc<T>(),
  // but return a UniqueArc<T> granting mutable access until it is converted into an Arc<T>.
  if constexpr (canConvert<T*, AtomicRefcounted*>()) {
    T* object = new T(kj::fwd<Params>(params)...);
    AtomicRefcounted* refcounted = object;
    refcounted->incRefcount();
    return UniqueArc<T>(refcounted, object);
  } else {
    auto wrapper = new _::ArcWrapper<T>(kj::fwd<Params>(params)...);
    return UniqueArc<T>(wrapper, wrapper->getWrappedPtr());
  }
}


}  // namespace kj

KJ_END_HEADER
