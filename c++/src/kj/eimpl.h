// Copyright (c) 2013 AMA Capital Management LLC
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

#ifndef KJ_EIMPL_H_
#define KJ_EIMPL_H_

#include "common.h"
#include <type_traits>  // for aligned_storage

namespace kj {

template<typename Impl, size_t size,
  size_t alignment = alignof(typename std::aligned_storage<size>::type)>
struct EImpl
{
  // A wrapper around a type that doesn't require that type to be complete.
  //
  // An EImpl contains an Impl.  It constructs it and destructs it for you.  You can
  // access the embedded impl as though EImpl is a pointer.  But you can have an EImpl in
  // a struct and put that struct on the stack even if the Impl type is undefined.  (Of
  // course, you can't construct the EImpl directly.)
  //
  // EImpl inherits the movability and copyability of Impl.  Unfortunately, even if Impl
  // is trivial, trivially copyable, and/or trivially destructible, EImpl will be none of the
  // above.  (This is unfixable.)
  //
  // Using any of the members that require Impl to be a complete type will assert that
  // size and alignment are sufficient.
  //
  // EImpl does not, and cannot, propagate noexcept specifications from Impl -- the method
  // signatures cannot require Impl to be a complete type.  In general, EImpl will be a member
  // of some class, and that class can use explicit noexcept operators if it wants.
  //
  // TODO(someday): Add a variant (or extra template argument) that supports noexcept move.

public:
  Impl &operator *() noexcept { return *(Impl*)&storage_; }
  Impl const &operator *() const noexcept { return *(Impl const*)&storage_; }
  Impl *operator->() noexcept { return &**this; }
  Impl const *operator->() const noexcept { return &**this; }

  EImpl() { check(); ctor(**this); }
  // Default-constructs the wrapped object.

  template<typename... Args>
  explicit EImpl(Args&&... args) { check(); ctor(**this, fwd<Args>(args)...); }
  // Construct the wrapped object with arbitrary arguments.  (Despite the fact that it accepts
  // zero arguments, C++ does not consuder it to be a default constructor.)

  ~EImpl() noexcept(false) { check(); (**this).~Impl(); }
  // Destroy the wrapped object.

  // The remaining methods are the standard copy- and move- constructors and assignment operators.

  EImpl(const EImpl &rhs) { check(); ctor(**this, *rhs); }
  EImpl(EImpl &&rhs) { check(); ctor(**this, mv(*rhs)); }
  EImpl &operator = (const EImpl &rhs) { check(); **this = *rhs; return *this; }
  EImpl &operator = (EImpl &&rhs) { check(); **this = mv(*rhs); return *this; }

private:
  typename std::aligned_storage<size, alignment>::type storage_;
  // Storage for the wrapped object.

  template<size_t required_size>
  inline constexpr void checkSize() const
  {
    // This is a small hack: if the assertion fails, the compiler is likely to show the required
    // size as well as the literal error mesage.
    static_assert(required_size <= size, "eimpl is too small");
  }

  template<size_t required_alignment>
  inline constexpr void checkAlignment() const
  {
    // This is a small hack: if the assertion fails, the compiler is likely to show the required
    // alignment as well as the literal error mesage.
    static_assert(required_alignment <= alignment, "eimpl is misaligned");
  }

  inline constexpr void check() const
  {
    checkSize<sizeof(Impl)>();
    checkAlignment<alignof(Impl)>();
  }
};

}  // namespace kj

#endif  // KJ_EIMPL_H_
