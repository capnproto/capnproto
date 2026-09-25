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

#include "convert.h"
#include "filesystem.h"
#include "vector.h"
#include "test.h"

namespace kj {
namespace {

template <typename Expected, typename Actual>
void expectType(Actual&&) {
  static_assert(isSameType<Expected, Decay<Actual>>());
}

// A user-defined owner that opts into kj::View / kj::Copy.
struct Message final: public Refcounted {
  explicit Message(StringPtr text): text(kj::str(text)) {}
  String text;
};

StringPtr asImpl(View*, const Message& message) { return message.text; }

struct AtomicMessage final: public AtomicRefcounted {
  explicit AtomicMessage(StringPtr text): text(kj::str(text)) {}
  String text;
};

StringPtr asImpl(View*, const AtomicMessage& message) { return message.text; }

KJ_TEST("as<View>: strings") {
  String str = kj::str("foo");
  auto ptr = str.as<View>();
  expectType<StringPtr>(ptr);
  KJ_EXPECT(ptr == "foo");
  KJ_EXPECT(ptr.begin() == str.begin());

  const String& constStr = str;
  expectType<StringPtr>(constStr.as<View>());

  ConstString constString = "bar"_kjc;
  auto constPtr = constString.as<View>();
  expectType<StringPtr>(constPtr);
  KJ_EXPECT(constPtr == "bar");
  KJ_EXPECT(constPtr.begin() == constString.begin());

  // Pointer types view themselves.
  auto again = ptr.as<View>();
  expectType<StringPtr>(again);
  KJ_EXPECT(again.begin() == str.begin());

  // Viewing a rvalue pointer moves from it.
  StringPtr source = str;
  auto moved = kj::mv(source).as<View>();
  KJ_EXPECT(moved.begin() == str.begin());
  KJ_EXPECT(source == nullptr);
}

KJ_TEST("as<View>: arrays") {
  Array<int> array = kj::heapArray<int>({1, 2, 3});
  auto ptr = array.as<View>();
  expectType<ArrayPtr<int>>(ptr);
  KJ_EXPECT(ptr.begin() == array.begin());
  ptr[0] = 10;
  KJ_EXPECT(array[0] == 10);

  const Array<int>& constArray = array;
  expectType<ArrayPtr<const int>>(constArray.as<View>());

  expectType<ArrayPtr<int>>(ptr.as<View>());
  const ArrayPtr<int>& constPtr = ptr;
  // A const ArrayPtr<int> can't be copied, but can be viewed read-only.
  auto readonly = constPtr.as<View>();
  expectType<ArrayPtr<const int>>(readonly);
  KJ_EXPECT(readonly.begin() == array.begin());

  Vector<int> vector;
  vector.add(1);
  vector.add(2);
  expectType<ArrayPtr<int>>(vector.as<View>());
  KJ_EXPECT(vector.as<View>().begin() == vector.begin());
  const Vector<int>& constVector = vector;
  expectType<ArrayPtr<const int>>(constVector.as<View>());

  FixedArray<int, 3> fixed;
  expectType<ArrayPtr<int>>(fixed.as<View>());
  KJ_EXPECT(fixed.as<View>().size() == 3);

  CappedArray<int, 4> capped(2);
  expectType<ArrayPtr<int>>(capped.as<View>());
  KJ_EXPECT(capped.as<View>().size() == 2);

  auto builder = kj::heapArrayBuilder<int>(3);
  builder.add(5);
  expectType<ArrayPtr<int>>(builder.as<View>());
  KJ_EXPECT(builder.as<View>().size() == 1);

  auto staticPtr = "abc"_kjb;
  expectType<ArrayPtr<const byte>>(staticPtr.as<View>());
}

KJ_TEST("as<View>: paths") {
  Path path = Path::parse("foo/bar");
  auto ptr = path.as<View>();
  expectType<PathPtr>(ptr);
  KJ_EXPECT(ptr == path);
  KJ_EXPECT(ptr.begin() == path.begin());
  expectType<PathPtr>(ptr.as<View>());
}

KJ_TEST("as<View>: Rc") {
  Rc<String> str = kj::rc<String>(kj::str("foo"));

  Rc<StringPtr> ptr = str.as<View>();
  KJ_EXPECT(str != nullptr);
  KJ_EXPECT(*ptr == "foo");
  KJ_EXPECT(ptr->begin() == str->begin());

  // The view keeps the original owner alive.
  const char* chars = str->begin();
  str = nullptr;
  KJ_EXPECT(*ptr == "foo");
  KJ_EXPECT(ptr->begin() == chars);

  // Viewing a view shares the same owner.
  Rc<StringPtr> again = ptr.as<View>();
  KJ_EXPECT(again->begin() == chars);

  // rvalue Rc is consumed.
  Rc<StringPtr> moved = kj::mv(ptr).as<View>();
  KJ_EXPECT(ptr == nullptr);
  KJ_EXPECT(moved->begin() == chars);

  // Null stays null.
  Rc<String> null;
  KJ_EXPECT(null.as<View>() == nullptr);
  KJ_EXPECT(kj::mv(null).as<View>() == nullptr);
}

KJ_TEST("as<View>: Rc shares refcount") {
  auto message = kj::rc<Message>("hello");
  KJ_EXPECT(!message->isShared());
  {
    Rc<StringPtr> ptr = message.as<View>();
    KJ_EXPECT(message->isShared());
    KJ_EXPECT(*ptr == "hello");
    KJ_EXPECT(ptr->begin() == message->text.begin());
  }
  KJ_EXPECT(!message->isShared());

  Rc<StringPtr> moved = message.addRef().as<View>();
  KJ_EXPECT(message->isShared());
  moved = nullptr;
  KJ_EXPECT(!message->isShared());
}

KJ_TEST("as<View>: Rc of arrays, vectors and paths") {
  auto array = kj::rc<Array<int>>(kj::heapArray<int>({1, 2, 3}));
  Rc<ArrayPtr<int>> ptr = array.as<View>();
  (*ptr)[1] = 20;
  KJ_EXPECT((*array)[1] == 20);

  auto constArray = kj::rc<const Array<int>>(kj::heapArray<int>({1, 2, 3}));
  Rc<ArrayPtr<const int>> constPtr = constArray.as<View>();
  KJ_EXPECT(constPtr->size() == 3);

  Rc<Vector<int>> vector = kj::rc<Vector<int>>();
  vector->add(7);
  Rc<ArrayPtr<int>> vectorPtr = vector.as<View>();
  KJ_EXPECT(vectorPtr->size() == 1);
  KJ_EXPECT((*vectorPtr)[0] == 7);

  auto path = kj::rc<Path>(Path::parse("foo/bar"));
  Rc<PathPtr> pathPtr = path.as<View>();
  KJ_EXPECT(*pathPtr == Path::parse("foo/bar"));
}

KJ_TEST("as<View>: Arc") {
  Arc<String> str = kj::arc<String>(kj::str("foo"));

  Arc<StringPtr> ptr = str.as<View>();
  KJ_EXPECT(*ptr == "foo");
  KJ_EXPECT(ptr->begin() == str->begin());

  // Arc::addRef() is const, so const Arcs can be viewed too.
  const Arc<String>& constStr = str;
  Arc<StringPtr> fromConst = constStr.as<View>();
  KJ_EXPECT(fromConst->begin() == str->begin());

  Arc<StringPtr> moved = kj::mv(str).as<View>();
  KJ_EXPECT(str == nullptr);
  KJ_EXPECT(*moved == "foo");

  // Arc exposes its referent as const, yielding read-only array views.
  auto array = kj::arc<Array<int>>(kj::heapArray<int>({1, 2, 3}));
  Arc<ArrayPtr<const int>> arrayPtr = array.as<View>();
  KJ_EXPECT(arrayPtr->size() == 3);

  auto message = kj::arc<AtomicMessage>("hello");
  {
    Arc<StringPtr> messagePtr = message.as<View>();
    KJ_EXPECT(message->isShared());
    KJ_EXPECT(*messagePtr == "hello");
  }
  KJ_EXPECT(!message->isShared());

  Arc<String> null;
  KJ_EXPECT(null.as<View>() == nullptr);
}

KJ_TEST("as<Copy>: pointer types") {
  String str = kj::str("foo");
  StringPtr ptr = str;
  auto copy = ptr.as<Copy>();
  expectType<String>(copy);
  KJ_EXPECT(copy == "foo");
  KJ_EXPECT(copy.begin() != str.begin());

  // Copying a rvalue pointer clears it.
  auto moved = kj::mv(ptr).as<Copy>();
  KJ_EXPECT(moved == "foo");
  KJ_EXPECT(ptr == nullptr);

  Array<int> array = kj::heapArray<int>({1, 2, 3});
  ArrayPtr<int> arrayPtr = array;
  auto arrayCopy = arrayPtr.as<Copy>();
  expectType<Array<int>>(arrayCopy);
  KJ_EXPECT(arrayCopy == array);
  KJ_EXPECT(arrayCopy.begin() != array.begin());

  ArrayPtr<const int> constPtr = array;
  expectType<Array<int>>(constPtr.as<Copy>());
  const ArrayPtr<int>& constRef = arrayPtr;
  expectType<Array<int>>(constRef.as<Copy>());

  Path path = Path::parse("foo/bar");
  PathPtr pathPtr = path;
  auto pathCopy = pathPtr.as<Copy>();
  expectType<Path>(pathCopy);
  KJ_EXPECT(pathCopy == path);
  KJ_EXPECT(pathCopy.begin() != path.begin());
}

KJ_TEST("as<Copy>: owning types") {
  String str = kj::str("foo");
  auto copy = str.as<Copy>();
  expectType<String>(copy);
  KJ_EXPECT(copy == "foo");
  KJ_EXPECT(copy.begin() != str.begin());

  ConstString constString = "bar"_kjc;
  expectType<ConstString>(constString.as<Copy>());

  Array<int> array = kj::heapArray<int>({1, 2, 3});
  auto arrayCopy = array.as<Copy>();
  expectType<Array<int>>(arrayCopy);
  KJ_EXPECT(arrayCopy == array);

  Path path = Path::parse("foo/bar");
  expectType<Path>(path.as<Copy>());
  KJ_EXPECT(path.as<Copy>() == path);
}

KJ_TEST("as<Copy>: Rc and Arc") {
  auto owner = kj::rc<Message>("hello");
  Rc<StringPtr> ptr = owner.as<View>();
  KJ_EXPECT(owner->isShared());

  Rc<String> copy = ptr.as<Copy>();
  KJ_EXPECT(*copy == "hello");
  KJ_EXPECT(copy->begin() != owner->text.begin());
  KJ_EXPECT(ptr != nullptr);

  const Rc<StringPtr>& constPtr = ptr;
  Rc<String> constCopy = constPtr.as<Copy>();
  KJ_EXPECT(*constCopy == "hello");

  // Copying a rvalue Rc releases it once the copy is made.
  Rc<String> moved = kj::mv(ptr).as<Copy>();
  KJ_EXPECT(ptr == nullptr);
  KJ_EXPECT(!owner->isShared());
  KJ_EXPECT(*moved == "hello");

  // Copying an owning Rc deep-copies its referent into a new Rc.
  Rc<String> copyOfCopy = copy.as<Copy>();
  KJ_EXPECT(*copyOfCopy == "hello");
  KJ_EXPECT(copyOfCopy->begin() != copy->begin());

  // View and back.
  auto array = kj::rc<Array<int>>(kj::heapArray<int>({1, 2, 3}));
  Rc<Array<int>> roundTrip = array.as<View>().as<Copy>();
  KJ_EXPECT(*roundTrip == *array);
  KJ_EXPECT(roundTrip->begin() != array->begin());

  Arc<String> arcStr = kj::arc<String>(kj::str("foo"));
  Arc<StringPtr> arcPtr = arcStr.as<View>();
  Arc<String> arcCopy = arcPtr.as<Copy>();
  KJ_EXPECT(*arcCopy == "foo");
  KJ_EXPECT(arcCopy->begin() != arcStr->begin());

  Rc<StringPtr> null;
  KJ_EXPECT(null.as<Copy>() == nullptr);
  Arc<StringPtr> arcNull;
  KJ_EXPECT(arcNull.as<Copy>() == nullptr);
}

KJ_TEST("as<View> and as<Copy>: Maybe") {
  Maybe<String> str = kj::str("foo");
  Maybe<StringPtr> ptr = str.as<View>();
  KJ_EXPECT(KJ_ASSERT_NONNULL(ptr).begin() == KJ_ASSERT_NONNULL(str).begin());

  Maybe<String> copy = ptr.as<Copy>();
  KJ_EXPECT(KJ_ASSERT_NONNULL(copy) == "foo");
  KJ_EXPECT(KJ_ASSERT_NONNULL(copy).begin() != KJ_ASSERT_NONNULL(str).begin());
  expectType<Maybe<String>>(str.as<Copy>());

  Maybe<String> none;
  KJ_EXPECT(none.as<View>() == kj::none);
  KJ_EXPECT(none.as<Copy>() == kj::none);

  const Maybe<Array<int>> array = kj::heapArray<int>({1, 2, 3});
  expectType<Maybe<ArrayPtr<const int>>>(array.as<View>());

  // Maybe<T&> converts the referenced value.
  String target = kj::str("bar");
  Maybe<String&> ref = target;
  Maybe<StringPtr> refView = ref.as<View>();
  KJ_EXPECT(KJ_ASSERT_NONNULL(refView).begin() == target.begin());
  expectType<Maybe<String>>(ref.as<Copy>());

  // Conversions compose with Rc.
  Maybe<Rc<String>> rc = kj::rc<String>(kj::str("baz"));
  Maybe<Rc<StringPtr>> rcView = rc.as<View>();
  KJ_EXPECT(*KJ_ASSERT_NONNULL(rcView) == "baz");
  Maybe<Rc<String>> rcCopy = kj::mv(rcView).as<Copy>();
  KJ_EXPECT(*KJ_ASSERT_NONNULL(rcCopy) == "baz");
  KJ_EXPECT(rcView == kj::none);
}

}  // namespace
}  // namespace kj
