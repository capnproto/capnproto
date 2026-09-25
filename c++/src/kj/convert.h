// Copyright (c) 2026 Cloudflare, Inc. and contributors
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
// Generic conversions between owning types and their pointer types, invoked via `.as<Tag>()`.
//
// Many KJ types provide `template <typename U> auto as()`, which calls `asImpl((U*)nullptr, *this)`
// found by argument-dependent lookup. This header defines two tags for this mechanism:
//
// - `x.as<kj::View>()` returns the pointer type (see kj::PointerTraits) that views `x`:
//
//       String                    -> StringPtr
//       ConstString               -> StringPtr
//       Array<T>                  -> ArrayPtr<T>         (ArrayPtr<const T> if const)
//       ArrayBuilder<T>           -> ArrayPtr<T>         (ArrayPtr<const T> if const)
//       FixedArray<T, n>          -> ArrayPtr<T>         (ArrayPtr<const T> if const)
//       CappedArray<T, n>         -> ArrayPtr<T>         (ArrayPtr<const T> if const)
//       Vector<T>                 -> ArrayPtr<T>         (ArrayPtr<const T> if const)
//       const ArrayPtr<T>         -> ArrayPtr<const T>
//       Path                      -> PathPtr             (kj/filesystem.h)
//       const Foo::Builder        -> Foo::Reader         (capnp/common.h)
//       const Text::Builder       -> Text::Reader        (capnp/blob.h)
//       capnp::Response<Foo>      -> Foo::Reader         (capnp/capability.h)
//       Rc<T>                     -> Rc<View of T>       sharing the refcount, e.g.
//                                                        Rc<String> -> Rc<StringPtr>
//       Arc<T>                    -> Arc<View of T>      sharing the refcount, e.g.
//                                                        Arc<Array<T>> -> Arc<ArrayPtr<const T>>
//       Maybe<T>, Maybe<T&>       -> Maybe<View of T>    e.g. Maybe<String> -> Maybe<StringPtr>
//
//   Pointer types (StringPtr, ArrayPtr, PathPtr, Cap'n Proto readers and builders) view as
//   themselves. Foo stands for any Cap'n Proto struct or list type, and also covers Data,
//   AnyPointer, AnyStruct, AnyList, DynamicStruct and DynamicList.
//
// - `x.as<kj::Copy>()` goes the other way, returning a new owning deep copy of `x`:
//
//       StringPtr                 -> String
//       ArrayPtr<T>               -> Array<RemoveConst<T>>
//       PathPtr                   -> Path                (kj/filesystem.h)
//       Text::Reader              -> String              (via StringPtr)
//       Data::Reader              -> Array<byte>         (via ArrayPtr<const byte>)
//       Rc<T>                     -> Rc<Copy of T>       newly allocated, e.g.
//                                                        Rc<StringPtr> -> Rc<String>
//       Arc<T>                    -> Arc<Copy of T>      newly allocated, e.g.
//                                                        Arc<StringPtr> -> Arc<String>
//       Maybe<T>, Maybe<T&>       -> Maybe<Copy of T>    e.g. Maybe<StringPtr> -> Maybe<String>
//
//   Owning types (String, ConstString, Array, Path) copy as their clone().
//
// Conversions compose: e.g. Maybe<Rc<String>> views as Maybe<Rc<StringPtr>>, and
// `rc.as<kj::View>().as<kj::Copy>()` makes an independent Rc.
//
// A plain view borrows from its source, which must outlive it. In particular, do not view a
// temporary owning object: `kj::str("foo").as<kj::View>()` dangles. Views of Rc/Arc carry their
// own reference to the owner and have no such restriction.
//
// Calling `.as<>()` on an rvalue pointer type or Rc/Arc consumes it, as with other rvalue
// operations on these types: `kj::mv(rc).as<kj::View>()` transfers the reference, while
// `rc.as<kj::View>()` adds a new one.
//
// Extending: define `asImpl(kj::View*, const MyOwner&)` returning a registered pointer type, and
// `asImpl(kj::Copy*, const MyPtr&)` returning the owning type. Rc, Arc and Maybe conversions are
// derived from these automatically.

#include "string.h"
#include "vector.h"
#include "refcount.h"

KJ_BEGIN_HEADER

namespace kj {

struct View {};
// Conversion tag: `x.as<View>()` returns a non-owning pointer type referring to `x`.

struct Copy {};
// Conversion tag: `x.as<Copy>()` returns an owning deep copy of `x`.

// TODO(cleanup): Move the type-specific asImpl() overloads below next to their types, as done for
//   Path and PathPtr in kj/filesystem.h.

// =======================================================================================
// View

inline StringPtr asImpl(View*, const StringPtr& ptr) { return ptr; }
inline StringPtr asImpl(View*, StringPtr&& ptr) { return kj::mv(ptr); }

template <typename T>
inline ArrayPtr<T> asImpl(View*, ArrayPtr<T>& ptr) { return ptr; }
template <typename T>
inline ArrayPtr<const T> asImpl(View*, const ArrayPtr<T>& ptr) { return ptr; }
template <typename T>
inline ArrayPtr<T> asImpl(View*, ArrayPtr<T>&& ptr) { return kj::mv(ptr); }
template <typename T>
inline ArrayPtr<const T> asImpl(View*, const ArrayPtr<T>&& ptr) { return ptr; }
// A const ArrayPtr<T> of mutable T cannot be copied, but can be viewed as ArrayPtr<const T>.

inline StringPtr asImpl(View*, const String& str) { return str; }
inline StringPtr asImpl(View*, const ConstString& str) { return str; }

template <typename T>
inline ArrayPtr<T> asImpl(View*, Array<T>& array) { return array.asPtr(); }
template <typename T>
inline ArrayPtr<const T> asImpl(View*, const Array<T>& array) { return array.asPtr(); }

template <typename T>
inline ArrayPtr<T> asImpl(View*, ArrayBuilder<T>& builder) { return builder.asPtr(); }
template <typename T>
inline ArrayPtr<const T> asImpl(View*, const ArrayBuilder<T>& builder) { return builder.asPtr(); }

template <typename T, size_t size>
inline ArrayPtr<T> asImpl(View*, FixedArray<T, size>& array) { return array.asPtr(); }
template <typename T, size_t size>
inline ArrayPtr<const T> asImpl(View*, const FixedArray<T, size>& array) { return array.asPtr(); }

template <typename T, size_t size>
inline ArrayPtr<T> asImpl(View*, CappedArray<T, size>& array) { return array.asPtr(); }
template <typename T, size_t size>
inline ArrayPtr<const T> asImpl(View*, const CappedArray<T, size>& array) {
  return array.asPtr();
}

template <typename T>
inline ArrayPtr<T> asImpl(View*, Vector<T>& vector) { return vector.asPtr(); }
template <typename T>
inline ArrayPtr<const T> asImpl(View*, const Vector<T>& vector) { return vector.asPtr(); }

namespace _ {  // private

template <typename Tag>
struct AsImpl {
  // Functor applying `asImpl((Tag*)nullptr, value)`, used to lift conversions into Rc/Arc/Maybe.
  template <typename T>
  auto operator()(T&& value) const -> decltype(asImpl((Tag*)nullptr, kj::fwd<T>(value))) {
    // The trailing return type keeps this SFINAE-friendly: Maybe::map() computes its result type
    // for every overload, including const ones for which no conversion may exist.
    return asImpl((Tag*)nullptr, kj::fwd<T>(value));
  }
};

}  // namespace _ (private)

template <typename T>
inline auto asImpl(View*, Rc<T>&& rc) {
  // Rc<Owner> -> Rc<View of Owner>, consuming the Rc and sharing its refcount without allocating.
  // A null Rc yields null.
  using Result = decltype(kj::mv(rc).project(_::AsImpl<View>()));
  if (rc == nullptr) return Result(nullptr);
  return kj::mv(rc).project(_::AsImpl<View>());
}

template <typename T>
inline auto asImpl(View*, Rc<T>& rc) {
  // Like above, but adds a new reference rather than consuming the Rc.
  return asImpl((View*)nullptr, rc.addRef());
}

template <typename T>
inline auto asImpl(View*, Arc<T>&& arc) {
  // Arc<Owner> -> Arc<View of Owner>, consuming the Arc and sharing its refcount without
  // allocating. Arc exposes its referent as const, so views are read-only. A null Arc yields null.
  using Result = decltype(kj::mv(arc).project(_::AsImpl<View>()));
  if (arc == nullptr) return Result(nullptr);
  return kj::mv(arc).project(_::AsImpl<View>());
}

template <typename T>
inline auto asImpl(View*, const Arc<T>& arc) {
  // Like above, but adds a new reference rather than consuming the Arc.
  return asImpl((View*)nullptr, arc.addRef());
}

// =======================================================================================
// Copy

inline String asImpl(Copy*, const StringPtr& ptr) { return ptr.clone(); }
inline String asImpl(Copy*, StringPtr&& ptr) { return kj::mv(ptr).clone(); }
// The rvalue overload clears the source pointer.

template <typename T>
inline auto asImpl(Copy*, ArrayPtr<T>& ptr) { return ptr.clone(); }
template <typename T>
inline auto asImpl(Copy*, const ArrayPtr<T>& ptr) { return ptr.clone(); }
template <typename T>
inline auto asImpl(Copy*, ArrayPtr<T>&& ptr) { return kj::mv(ptr).clone(); }
// The rvalue overload clears the source pointer.

inline String asImpl(Copy*, const String& str) { return str.clone(); }
inline ConstString asImpl(Copy*, const ConstString& str) { return str.clone(); }

template <typename T>
inline auto asImpl(Copy*, Array<T>& array) { return array.clone(); }
template <typename T>
inline auto asImpl(Copy*, const Array<T>& array) { return array.clone(); }

template <typename T>
inline auto asImpl(Copy*, const Rc<T>& rc) {
  // Rc<T> -> Rc<Copy of T>: allocates a new, independent Rc holding a copy of the referent.
  // A null Rc yields null.
  using Value = decltype(asImpl((Copy*)nullptr, *rc));
  if (rc == nullptr) return Rc<Value>();
  return kj::rc<Value>(asImpl((Copy*)nullptr, *rc));
}

template <typename T>
inline auto asImpl(Copy*, Rc<T>&& rc) {
  // Like above, but releases the source Rc once the copy is made.
  auto result = asImpl((Copy*)nullptr, rc);
  rc = nullptr;
  return result;
}

template <typename T>
inline auto asImpl(Copy*, const Arc<T>& arc) {
  // Arc<T> -> Arc<Copy of T>: allocates a new, independent Arc holding a copy of the referent.
  // A null Arc yields null.
  using Value = decltype(asImpl((Copy*)nullptr, *arc));
  if (arc == nullptr) return Arc<Value>();
  return kj::arc<Value>(asImpl((Copy*)nullptr, *arc));
}

template <typename T>
inline auto asImpl(Copy*, Arc<T>&& arc) {
  // Like above, but releases the source Arc once the copy is made.
  auto result = asImpl((Copy*)nullptr, arc);
  arc = nullptr;
  return result;
}

// =======================================================================================
// Maybe

// Maybe<T> converts its value, if any: e.g. Maybe<String> -> Maybe<StringPtr> with View, and
// Maybe<StringPtr> -> Maybe<String> with Copy. Maybe<T&> converts the referenced value.

template <typename T>
inline auto asImpl(View*, Maybe<T>& maybe) { return maybe.map(_::AsImpl<View>()); }
template <typename T>
inline auto asImpl(View*, const Maybe<T>& maybe) { return maybe.map(_::AsImpl<View>()); }
template <typename T>
inline auto asImpl(View*, Maybe<T>&& maybe) { return kj::mv(maybe).map(_::AsImpl<View>()); }

template <typename T>
inline auto asImpl(Copy*, Maybe<T>& maybe) { return maybe.map(_::AsImpl<Copy>()); }
template <typename T>
inline auto asImpl(Copy*, const Maybe<T>& maybe) { return maybe.map(_::AsImpl<Copy>()); }
template <typename T>
inline auto asImpl(Copy*, Maybe<T>&& maybe) { return kj::mv(maybe).map(_::AsImpl<Copy>()); }

}  // namespace kj

KJ_END_HEADER
