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

#include "common.h"
#include <kj/compat/gtest.h>
#include <kj/string.h>
#include <kj/debug.h>
#include <capnp/test.capnp.h>
#include <kj/refcount.h>
#if !CAPNP_LITE
#include "dynamic.h"
#endif

#include <type_traits>

#if HAVE_CONFIG_H
#include "config.h"
#endif

namespace capnp {
namespace {

KJ_ASSERT_CAN_MEMCPY(capnp::word);
  
TEST(Common, Version) {
#ifdef VERSION
  auto expectedVersion =
      kj::str(CAPNP_VERSION_MAJOR, '.', CAPNP_VERSION_MINOR, '.', CAPNP_VERSION_MICRO);
  auto devVersion =
      kj::str(CAPNP_VERSION_MAJOR, '.', CAPNP_VERSION_MINOR, "-dev");
  kj::StringPtr actualVersion = VERSION;
  KJ_ASSERT(actualVersion == expectedVersion ||
            actualVersion.startsWith(kj::str(expectedVersion, '-')) ||
            actualVersion.startsWith(kj::str(expectedVersion, '.')) ||
            (actualVersion == devVersion && CAPNP_VERSION_MICRO == 0),
            expectedVersion, actualVersion);
#endif
}

struct ExampleStruct {
  struct _capnpPrivate {
    struct IsStruct;
  };
};
struct ExampleInterface {
  struct _capnpPrivate {
    struct IsInterface;
  };
};

static_assert(_::Kind_<ExampleStruct>::kind == Kind::STRUCT, "Kind SFINAE failed.");
static_assert(_::Kind_<ExampleInterface>::kind == Kind::INTERFACE, "Kind SFINAE failed.");

// Test FromAnay<>
template <typename T, typename U>
struct EqualTypes_ { static constexpr bool value = false; };

template <typename T>
struct EqualTypes_<T, T> { static constexpr bool value = true; };

template <typename T, typename U>
inline constexpr bool equalTypes() { return EqualTypes_<T, U>::value; }

using capnproto_test::capnp::test::TestAllTypes;
using capnproto_test::capnp::test::TestInterface;

namespace test = capnproto_test::capnp::test;
static_assert(kj::isPointerType<TestAllTypes::Reader>());
static_assert(kj::isPointerType<const TestAllTypes::Reader>());
static_assert(kj::isPointerType<test::TestGenerics<Text, TestAllTypes>::Reader>());
static_assert(kj::isPointerType<test::TestGroups::Groups::Foo::Reader>());
static_assert(kj::isPointerType<TestAllTypes::Builder>());
static_assert(!kj::PointerTraits<TestAllTypes::Builder>::isReadOnly);
static_assert(kj::isPointerType<List<uint32_t>::Reader>());
static_assert(kj::isPointerType<List<test::TestEnum>::Reader>());
static_assert(kj::isPointerType<List<TestAllTypes>::Reader>());
static_assert(kj::isPointerType<List<List<Text>>::Reader>());
static_assert(kj::isPointerType<List<Text>::Reader>());
static_assert(kj::isPointerType<List<Data>::Reader>());
static_assert(kj::isPointerType<List<AnyPointer>::Reader>());
static_assert(kj::isPointerType<List<AnyStruct>::Reader>());
static_assert(kj::isPointerType<Text::Reader>());
static_assert(kj::isPointerType<Data::Reader>());
static_assert(kj::isPointerType<Text::Builder>());
static_assert(kj::isPointerType<Data::Builder>());
static_assert(kj::isPointerType<AnyPointer::Reader>());
static_assert(kj::isPointerType<AnyStruct::Reader>());
static_assert(kj::isPointerType<AnyList::Reader>());
static_assert(kj::isPointerType<AnyPointer::Builder>());
static_assert(kj::isPointerType<AnyStruct::Builder>());
static_assert(kj::isPointerType<AnyList::Builder>());
static_assert(!kj::isPointerType<ReaderFor<uint32_t>>());
static_assert(!kj::isPointerType<test::TestEnum>());
static_assert(!kj::isPointerType<Void>());
static_assert(!kj::isPointerType<kj::Own<TestAllTypes::Reader>>());
static_assert(!kj::isPointerType<kj::Rc<TestAllTypes::Reader>>());
static_assert(sizeof(kj::Rc<TestAllTypes::Reader>) == sizeof(TestAllTypes::Reader) + sizeof(void*));
static_assert(sizeof(kj::Arc<TestAllTypes::Reader>) ==
    sizeof(TestAllTypes::Reader) + sizeof(void*));
// Readers are exposed as const, so *rc cannot be re-pointed; builders keep their non-const setters.
static_assert(kj::isSameType<decltype(*kj::instance<kj::Rc<TestAllTypes::Reader>&>()),
                             const TestAllTypes::Reader&>());
static_assert(kj::isSameType<decltype(*kj::instance<kj::Rc<TestAllTypes::Builder>&>()),
                             TestAllTypes::Builder&>());

#if !CAPNP_LITE
static_assert(kj::isPointerType<List<TestInterface>::Reader>());
static_assert(kj::isPointerType<DynamicStruct::Reader>());
static_assert(kj::isPointerType<DynamicList::Reader>());
static_assert(kj::isPointerType<DynamicStruct::Builder>());
static_assert(kj::isPointerType<DynamicList::Builder>());
static_assert(!kj::isPointerType<DynamicValue::Reader>());
static_assert(!kj::isPointerType<DynamicValue::Builder>());
static_assert(!kj::isPointerType<Capability::Client>());
static_assert(!kj::isPointerType<TestInterface::Client>());
static_assert(!kj::isPointerType<DynamicCapability::Client>());
static_assert(!kj::isPointerType<Response<TestAllTypes>>());
static_assert(!kj::isPointerType<Request<TestAllTypes, TestAllTypes>>());
static_assert(!kj::isPointerType<StreamingRequest<TestAllTypes>>());
static_assert(!kj::isPointerType<TestAllTypes::Pipeline>());
#endif

static_assert(equalTypes<FromAny<int>, int>(), "");
static_assert(equalTypes<FromAny<TestAllTypes::Reader>, TestAllTypes>(), "");
static_assert(equalTypes<FromAny<TestAllTypes::Builder>, TestAllTypes>(), "");
#if !CAPNP_LITE
static_assert(equalTypes<FromAny<TestAllTypes::Pipeline>, TestAllTypes>(), "");
static_assert(equalTypes<FromAny<TestInterface::Client>, TestInterface>(), "");
static_assert(equalTypes<FromAny<kj::Own<TestInterface::Server>>, TestInterface>(), "");
#endif

}  // namespace
}  // namespace capnp
