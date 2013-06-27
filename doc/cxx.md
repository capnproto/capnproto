---
layout: page
title: C++ Runtime
---

# C++ Runtime

The Cap'n Proto C++ runtime implementation provides an easy-to-use interface for manipulating
messages backed by fast pointer arithmetic.

## Example Usage

For the Cap'n Proto definition:

{% highlight capnp %}
struct Person {
  id @0 :UInt32;
  name @1 :Text;
  email @2 :Text;
  phones @3 :List(PhoneNumber);

  struct PhoneNumber {
    number @0 :Text;
    type @1 :Type;

    enum Type {
      mobile @0;
      home @1;
      work @2;
    }
  }

  employment @4 union {
    unemployed @5 :Void;
    employer @6 :Text;
    school @7 :Text;
    selfEmployed @8 :Void;
    # We assume that a person is only one of these.
  }
}

struct AddressBook {
  people @0 :List(Person);
}
{% endhighlight %}

You might write code like:

{% highlight c++ %}
#include "addressbook.capnp.h"
#include <capnp/message.h>
#include <capnp/serialize-packed.h>
#include <iostream>

void writeAddressBook(int fd) {
  ::capnp::MallocMessageBuilder message;

  AddressBook::Builder addressBook = message.initRoot<AddressBook>();
  ::capnp::List<Person>::Builder people = addressBook.initPeople(2);

  Person::Builder alice = people[0];
  alice.setId(123);
  alice.setName("Alice");
  alice.setEmail("alice@example.com");
  // Type shown for explanation purposes; normally you'd use auto.
  ::capnp::List<Person::PhoneNumber>::Builder alicePhones =
      alice.initPhones(1);
  alicePhones[0].setNumber("555-1212");
  alicePhones[0].setType(Person::PhoneNumber::Type::MOBILE);
  alice.getEmployment().setSchool("MIT");

  Person::Builder bob = people[1];
  bob.setId(456);
  bob.setName("Bob");
  bob.setEmail("bob@example.com");
  auto bobPhones = bob.initPhones(2);
  bobPhones[0].setNumber("555-4567");
  bobPhones[0].setType(Person::PhoneNumber::Type::HOME);
  bobPhones[1].setNumber("555-7654");
  bobPhones[1].setType(Person::PhoneNumber::Type::WORK);
  bob.getEmployment().setUnemployed();

  writePackedMessageToFd(fd, message);
}

void printAddressBook(int fd) {
  ::capnp::PackedFdMessageReader message(fd);

  AddressBook::Reader addressBook = message.getRoot<AddressBook>();

  for (Person::Reader person : addressBook.getPeople()) {
    std::cout << person.getName().cStr() << ": "
              << person.getEmail().cStr() << std::endl;
    for (Person::PhoneNumber::Reader phone: person.getPhones()) {
      const char* typeName = "UNKNOWN";
      switch (phone.getType()) {
        case Person::PhoneNumber::Type::MOBILE: typeName = "mobile"; break;
        case Person::PhoneNumber::Type::HOME: typeName = "home"; break;
        case Person::PhoneNumber::Type::WORK: typeName = "work"; break;
      }
      std::cout << "  " << typeName << " phone: "
                << phone.getNumber().cStr() << std::endl;
    }
    Person::Employment::Reader employment = person.getEmployment();
    switch (employment.which()) {
      case Person::Employment::UNEMPLOYED:
        std::cout << "  unemployed" << std::endl;
        break;
      case Person::Employment::EMPLOYER:
        std::cout << "  employer: "
                  << employment.getEmployer().cStr() << std::endl;
        break;
      case Person::Employment::SCHOOL:
        std::cout << "  student at: "
                  << employment.getSchool().cStr() << std::endl;
        break;
      case Person::Employment::SELF_EMPLOYED:
        std::cout << "  self-employed" << std::endl;
        break;
    }
  }
}
{% endhighlight %}

## C++ Feature Usage:  C++11, Exceptions

This implementation makes use of C++11 features.  If you are using GCC, you will need at least
version 4.7 to compile Cap'n Proto.  If you are using Clang, you will need at least version 3.2.
These compilers required the flag `-std=c++11` to enable C++11 features -- your code which
`#include`s Cap'n Proto headers will need to be compiled with this flag.  Other compilers have not
been tested at this time.

This implementation prefers to handle errors using exceptions.  Exceptions are only used in
circumstances that should never occur in normal operation.  For example, exceptions are thrown
on assertion failures (indicating bugs in the code), network failures, and invalid input.
Exceptions thrown by Cap'n Proto are never part of the interface and never need to be caught in
correct usage.  The purpose of throwing exceptions is to allow higher-level code a chance to
recover from unexpected circumstances without disrupting other work happening in the same process.
For example, a server that handles requests from multiple clients should, on exception, return an
error to the client that caused the exception and close that connection, but should continue
handling other connections normally.

When Cap'n Proto code might throw an exception from a destructor, it first checks
`std::uncaught_exception()` to ensure that this is safe.  If another exception is already active,
the new exception is assumed to be a side-effect of the main exception, and is either silently
swallowed or reported on a side channel.

In recognition of the fact that some teams prefer not to use exceptions, and that even enabling
exceptions in the compiler introduces overhead, Cap'n Proto allows you to disable them entirely
by registering your own exception callback.  The callback will be called in place of throwing an
exception.  The callback may abort the process, and is required to do so in certain circumstances
(when a fatal bug is detected).  If the callback returns normally, Cap'n Proto will attempt
to continue by inventing "safe" values.  This will lead to garbage output, but at least the program
will not crash.  Your exception callback should set some sort of a flag indicating that an error
occurred, and somewhere up the stack you should check for that flag and cancel the operation.
See the header `capnp/exception.h` for details on how to register an exception callback.

## KJ Library

Cap'n Proto is built on top of a basic utility library called KJ.  The two were actually developed
together -- KJ is simply the stuff which is not specific to Cap'n Proto serialization, and may be
useful to others independently of Cap'n Proto.  For now, the the two are distributed together.  The
name "KJ" has no particular meaning; it was chosen to be short and easy-to-type.

## Generating Code

To generate C++ code from your `.capnp` [interface definition](language.html), run:

    capnpc -oc++ myproto.capnp

This will create `myproto.capnp.h` and `myproto.capnp.c++` in the same directory as `myproto.capnp`.

### Setting a Namespace

You probably want your generated types to live in a C++ namespace.  You will need to import
`/capnp/c++.capnp` and use the `namespace` annotation it defines:

{% highlight capnp %}
using Cxx = import "/capnp/c++.capnp";
$Cxx.namespace("foo::bar::baz");
{% endhighlight %}

Note that `capnp/c++.capnp` is installed in `$PREFIX/include` (`/usr/local/include` by default)
when you install the C++ runtime.  `capnpc` automatically searches `/usr/include` and
`/usr/local/include` for imports that start with a `/`, so it should "just work".  If you installed
somewhere else, you may need to add it to the search path with the `-I` flag to `capnpc`, which
works much like the compiler flag of the same name.

## Types

### Primitive Types

Primitive types map to the obvious C++ types:

* `Bool` -> `bool`
* `IntNN` -> `intNN_t`
* `UIntNN` -> `uintNN_t`
* `Float32` -> `float`
* `Float64` -> `double`
* `Void` -> `::capnp::Void` (An enum with one value: `::capnp::Void::VOID`)

### Structs

For each struct `Foo` in your interface, a C++ type named `Foo` generated.  This type itself is
really just a namespace; it contains two important inner classes:  `Reader` and `Builder`.

`Reader` represents a read-only instance of `Foo` while `Builder` represents a writable instance
(usually, one that you are building).  Both classes behave like pointers, in that you can pass them
by value and they do not own the underlying data that they operate on.  In other words,
`Foo::Builder` is like a pointer to a `Foo` while `Foo::Reader` is like a const pointer to a `Foo`.

For every field `bar` defined in `Foo`, `Foo::Reader` has a method `getBar()`.  For primitive types,
`get` just returns the type, but for structs, lists, and blobs, it returns a `Reader` for the
type.

{% highlight c++ %}
// Example Reader methods:

// myPrimitiveField @0 :Int32;
int32_t getMyPrimitiveField();

// myTextField @1 :Text;
::capnp::Text::Reader getMyTextField();
// (Note that Text::Reader may be implicitly cast to const char* and
// std::string.)

// myStructField @2 :MyStruct;
MyStruct::Reader getMyStructField();

// myListField @3 :List(Float64);
::capnp::List<double> getMyListField();
{% endhighlight %}

`Foo::Builder`, meanwhile, has two or three methods for each field `bar`:

* `getBar()`:  For primitives, returns the value.  For composites, returns a Builder for the
  composite.  If a composite field has not been initialized (i.e. this is the first time it has
  been accessed), it will be initialized to a copy of the field's default value before returning.
* `setBar(x)`:  For primitives, sets the value to X.  For composites, sets the value to a copy of
  x, which must be a Reader for the type.
* `initBar(n)`:  Only for lists and blobs.  Sets the field to a newly-allocated list or blob
  of size n and returns a Builder for it.  The elements of the list are initialized to their empty
  state (zero for numbers, default values for structs).
* `initBar()`:  Only for structs.  Sets the field to a newly-allocated struct and returns a
  Builder for it.  Note that the newly-allocated struct is initialized to the default value for
  the struct's _type_ (i.e., all-zero) rather than the default value for the field `bar` (if it
  has one).

{% highlight c++ %}
// Example Builder methods:

// myPrimitiveField @0 :Int32;
int32_t getMyPrimitiveField();
void setMyPrimitiveField(int32_t value);

// myTextField @1 :Text;
::capnp::Text::Builder getMyTextField();
void setMyTextField(::capnp::Text::Reader value);
::capnp::Text::Builder initMyTextField(size_t size);
// (Note that Text::Reader is implicitly constructable from const char*
// and std::string, and Text::Builder can be implicitly cast to
// these types.)

// myStructField @2 :MyStruct;
MyStruct::Builder getMyStructField();
void setMyStructField(MyStruct::Reader value);
MyStruct::Builder initMyStructField();

// myListField @3 :List(Float64);
::capnp::List<double>::Builder getMyListField();
void setMyListField(::capnp::List<double>::Reader value);
::capnp::List<double>::Builder initMyListField(size_t size);
{% endhighlight %}

### Unions

For each union `foo` declared in the struct, the struct's reader and builder have a method
`getFoo()` which returns a reader/builder for the union.  The union reader/builder has accessors
for each field exactly like a struct's accessors.  It also has an accessor `which()` which returns
an enum indicating which member of the union is currently set.  Setting any member of the union
updates the value returned by `which()`.  Getting a member other than the currently-set member
crashes in debug mode or returns garbage when `NDEBUG` is defined.

See the [example](#example_usage) at the top of the page for an example of unions.

### Lists

Lists are represented by the type `capnp::List<T>`, where `T` is any of the primitive types,
any Cap'n Proto user-defined type, `capnp::Text`, `capnp::Data`, or `capnp::List<U>`
(to form a list of lists).

The type `List<T>` itself is not instantiatable, but has two inner classes: `Reader` and `Builder`.
As with structs, these types behave like pointers to read-only and read-write data, respectively.

Both `Reader` and `Builder` implement `size()`, `operator[]`, `begin()`, and `end()`, as good C++
containers should.  Note, though, that `operator[]` is read-only -- you cannot use it to assign
the element, because that would require returning a reference, which is impossible because the
underlying data may not be in your CPU's native format (e.g., wrong byte order).  Instead, to
assign an element of a list, you must use `builder.set(index, value)`.

For `List<Foo>` where `Foo` is a non-primitive type, the type returned by `operator[]` and
`iterator::operator*()` is `Foo::Reader` (for `List<Foo>::Reader`) or `Foo::Builder`
(for `List<Foo>::Builder`).  The builder's `set` method takes a `Foo::Reader` as its second
parameter.

For lists of lists or lists of blobs, the builder also has a method `init(index, size)` which sets
the element at the given index to a newly-allocated value with the given size and returns a builder
for it.  Struct lists do not have an `init` method because all elements are initialized to empty
values when the list is created.

### Enums

Cap'n Proto enums become C++11 "enum classes".  That means they behave like any other enum, but
the enum's values are scoped within the type.  E.g. for an enum `Foo` with value `bar`, you must
refer to the value as `Foo::BAR`.

To match prevaling C++ style, an enum's value names are converted to UPPERCASE_WITH_UNDERSCORES
(whereas in the definition language you'd write them in camelCase).

Keep in mind when writing `switch` blocks that an enum read off the wire may have a numeric
value that is not listed in its definition.  This may be the case if the sender is using a newer
version of the protocol, or if the message is corrupt or malicious.  In C++11, enums are allowed
to have any value that is within the range of their base type, which for Cap'n Proto enums is
`uint16_t`.

### Blobs (Text and Data)

Blobs are manipulated using the classes `capnp::Text` and `capnp::Data`.  These classes are,
again, just containers for inner classes `Reader` and `Builder`.  These classes are iterable and
implement `size()` and `operator[]` methods.  `Builder::operator[]` even returns a reference
(unlike with `List<T>`).  `Text::Reader` additionally has a method `cStr()` which returns a
NUL-terminated `const char*`.

### Interfaces

Interfaces (RPC) are not yet implemented at this time.

### Constants

Constants are not yet implemented at this time.  (They are not hard, but they are low-priority.)

## Messages and I/O

To create a new message, you must start by creating a `capnp::MessageBuilder`
(`capnp/message.h`).  This is an abstract type which you can implement yourself, but most users
will want to use `capnp::MallocMessageBuilder`.  Once your message is constructed, write it to
a file descriptor with `capnp::writeMessageToFd(fd, builder)` (`capnp/serialize.h`) or
`capnp::writePackedMessageToFd(fd, builder)` (`capnp/serialize-packed.h`).

To read a message, you must create a `capnp::MessageReader`, which is another abstract type.
Implementations are specific to the data source.  You can use `capnp::StreamFdMessageReader`
(`capnp/serialize.h`) or `capnp::PackedFdMessageReader` (`capnp/serialize-packed.h`)
to read from file descriptors; both take the file descriptor as a constructor argument.

Note that if your stream contains additional data after the message, `PackedFdMessageReader` may
accidentally read some of that data, since it does buffered I/O.  To make this work correctly, you
will need to set up a multi-use buffered stream.  Buffered I/O may also be a good idea with
`StreamFdMessageReader` and also when writing, for performance reasons.  See `capnp/io.h` for
details.

There is an [example](#example_usage) of all this at the beginning of this page.

## Dynamic Reflection

Sometimes you want to write generic code that operates on arbitrary types, iterating over the
fields or looking them up by name.  For example, you might want to write code that encodes
arbitrary Cap'n Proto types in JSON format.  This requires something like "reflection", but C++
does not offer reflection.  Also, you might even want to operate on types that aren't compiled
into the binary at all, but only discovered at runtime.

The C++ API supports inspecting schemas at runtime via the interface defined in
`capnp/schema.h`, and dynamically reading and writing instances of arbitrary types via
`capnp/dynamic.h`.  Here's the example from the beginning of this file rewritten in terms
of the dynamic API:

{% highlight c++ %}
#include "addressbook.capnp.h"
#include <capnp/message.h>
#include <capnp/serialize-packed.h>
#include <iostream>
#include <capnp/schema.h>
#include <capnp/dynamic.h>

using ::capnp::DynamicValue;
using ::capnp::DynamicStruct;
using ::capnp::DynamicEnum;
using ::capnp::DynamicList;
using ::capnp::DynamicUnion;
using ::capnp::List;
using ::capnp::Schema;
using ::capnp::StructSchema;
using ::capnp::EnumSchema;

using ::capnp::Void;
using ::capnp::Text;
using ::capnp::MallocMessageBuilder;
using ::capnp::PackedFdMessageReader;

void dynamicWriteAddressBook(int fd, StructSchema schema) {
  // Write a message using the dynamic API to set each
  // field by text name.  This isn't something you'd
  // normally want to do; it's just for illustration.

  MallocMessageBuilder message;

  // Types shown for explanation purposes; normally you'd
  // use auto.
  DynamicStruct::Builder addressBook =
      message.initRoot<DynamicStruct>(schema);

  DynamicList::Builder people =
      addressBook.init("people", 2).as<DynamicList>();

  DynamicStruct::Builder alice =
      people[0].as<DynamicStruct>();
  alice.set("id", 123);
  alice.set("name", "Alice");
  alice.set("email", "alice@example.com");
  auto alicePhones = alice.init("phones", 1).as<DynamicList>();
  auto phone0 = alicePhones[0].as<DynamicStruct>();
  phone0.set("number", "555-1212");
  phone0.set("type", "mobile");
  alice.get("employment").as<DynamicUnion>()
       .set("school", "MIT");

  auto bob = people[1].as<DynamicStruct>();
  bob.set("id", 456);
  bob.set("name", "Bob");
  bob.set("email", "bob@example.com");

  // Some magic:  We can convert a dynamic sub-value back to
  // the native type with as<T>()!
  List<Person::PhoneNumber>::Builder bobPhones =
      bob.init("phones", 2).as<List<Person::PhoneNumber>>();
  bobPhones[0].setNumber("555-4567");
  bobPhones[0].setType(Person::PhoneNumber::Type::HOME);
  bobPhones[1].setNumber("555-7654");
  bobPhones[1].setType(Person::PhoneNumber::Type::WORK);
  bob.get("employment").as<DynamicUnion>()
     .set("unemployed", Void::VOID);

  writePackedMessageToFd(fd, message);
}

void dynamicPrintValue(DynamicValue::Reader value) {
  // Print an arbitrary message via the dynamic API by
  // iterating over the schema.  Look at the handling
  // of STRUCT in particular.

  switch (value.getType()) {
    case DynamicValue::VOID:
      std::cout << "";
      break;
    case DynamicValue::BOOL:
      std::cout << (value.as<bool>() ? "true" : "false");
      break;
    case DynamicValue::INT:
      std::cout << value.as<int64_t>();
      break;
    case DynamicValue::UINT:
      std::cout << value.as<uint64_t>();
      break;
    case DynamicValue::FLOAT:
      std::cout << value.as<double>();
      break;
    case DynamicValue::TEXT:
      std::cout << '\"' << value.as<Text>().cStr() << '\"';
      break;
    case DynamicValue::LIST: {
      std::cout << "[";
      bool first = true;
      for (auto element: value.as<DynamicList>()) {
        if (first) {
          first = false;
        } else {
          std::cout << ", ";
        }
        dynamicPrintValue(element);
      }
      std::cout << "]";
      break;
    }
    case DynamicValue::ENUM: {
      auto enumValue = value.as<DynamicEnum>();
      KJ_IF_MAYBE(enumerant, enumValue.getEnumerant()) {
        std::cout <<
            enumerant->getProto().getName().cStr();
      } else {
        // Unknown enum value; output raw number.
        std::cout << enumValue.getRaw();
      }
      break;
    }
    case DynamicValue::STRUCT: {
      std::cout << "(";
      auto structValue = value.as<DynamicStruct>();
      bool first = true;
      for (auto member:
           structValue.getSchema().getMembers()) {
        if (first) {
          first = false;
        } else {
          std::cout << ", ";
        }
        std::cout << member.getProto().getName().cStr()
                  << " = ";
        dynamicPrintValue(structValue.get(member));
      }
      std::cout << ")";
      break;
    }
    case DynamicValue::UNION: {
      auto unionValue = value.as<DynamicUnion>();
      KJ_IF_MAYBE(tag, unionValue.which()) {
        std::cout << tag->getProto().getName().cStr() << "(";
        dynamicPrintValue(unionValue.get());
        std::cout << ")";
      } else {
        // Unknown union member; must have come from newer
        // version of the protocol.
        std::cout << "?";
      }
      break;
    }
    default:
      // There are other types, we aren't handling them.
      std::cout << "?";
      break;
  }
}

void dynamicPrintMessage(int fd, StructSchema schema) {
  PackedFdMessageReader message(fd);
  dynamicPrintValue(message.getRoot<DynamicStruct>(schema));
  std::cout << std::endl;
}
{% endhighlight %}

Notes about the dynamic API:

* You can implicitly cast any compiled Cap'n Proto struct reader/builder type directly to
  `DynamicStruct::Reader`/`DynamicStruct::Builder`.  Similarly with `List<T>` and `DynamicList`,
  and even enum types and `DynamicEnum`.  Finally, all valid Cap'n Proto field types may be
  implicitly converted to `DynamicValue`.

* You can load schemas dynamically at runtime using `SchemaLoader` (`capnp/schema-loader.h`) and
  use the Dynamic API to manipulate objects of these types.  `MessageBuilder` and `MessageReader`
  have methods for accessing the message root using a dynamic schema.

* Unlike with Protobufs, there is no "global registry" of compiled-in types.  To get the schema
  for a compiled-in type, use `capnp::Schema::from<MyType>()`.

* Unlike with Protobufs, the overhead of supporting reflection is small.  Generated `.capnp.c++`
  files contain only some embedded const data structures describing the schema, no code at all,
  and the runtime library support code is relatively small.  Moreover, if you do not use the
  dynamic API or the schema API, you do not even need to link their implementations into your
  executable.

## Reference

The runtime library contains lots of useful features not described on this page.  For now, the
best reference is the header files.  See:

    capnp/list.h
    capnp/blob.h
    capnp/message.h
    capnp/serialize.h
    capnp/serialize-packed.h
    capnp/schema.h
    capnp/schema-loader.h
    capnp/dynamic.h

## Lessons Learned from Protocol Buffers

The author of Cap'n Proto's C++ implementation also wrote (in the past) verison 2 of Google's
Protocol Buffers.  As a result, Cap'n Proto's implementation benefits from a number of lessons
learned the hard way:

* Protobuf generated code is enormous due to the parsing and serializing code generated for every
  class.  This actually poses a significant problem in practice -- there exist server binaries
  containing literally hundreds of megabytes of compiled protobuf code.  Cap'n Proto generated code,
  on the other hand, is almost entirely inlined accessors.  The only things that go into `.capnp.o`
  files are default values for pointer fields (if needed, which is rare) and the encoded schema
  (just the raw bytes of a Cap'n-Proto-encoded schema structure).  The latter could even be removed
  if you don't use dynamic reflection.

* The C++ Protobuf implementation used lots of dynamic initialization code (that runs before
  `main()`) to do things like register types in global tables.  This proved problematic for
  programs which linked in lots of protocols but needed to start up quickly.  Cap'n Proto does not
  use any dynamic initializers anywhere, period.

* The C++ Protobuf implementation makes heavy use of STL in its interface and implementation.
  The proliferation of template instantiations gives the Protobuf runtime library a large footprint,
  and using STL in the interface can lead to weird ABI problems and slow compiles.  Cap'n Proto
  does not use any STL containers in its interface and makes sparing use in its implementation.
  As a result, the Cap'n Proto runtime library is very small, and code that uses it compiles
  quickly.

* The in-memory representation of messages in Protobuf-C++ involves many heap objects.  Each
  message (struct) is an object, each non-primitive repeated field allocates an array of pointers
  to more objects, and each string may actually add two heap objects.  Cap'n Proto by its nature
  uses arena allocation, so the entire message is allocated in a few contiguous segments.  This
  means Cap'n Proto spends very little time allocating memory, stores messages more compactly, and
  avoids memory fragmentation.

* Related to the last point, Protobuf-C++ relies heavily on object reuse for performance.
  Building or parsing into a newly-allocated Protobuf object is significantly slower than using
  an existing one.  However, the memory usage of a Protobuf object will tend to grow the more times
  it is reused, particularly if it is used to parse messages of many different "shapes", so the
  objects need to be deleted and re-allocated from time to time.  All this makes tuning Protobufs
  fairly tedious.  In contrast, enabling memory reuse with Cap'n Proto is as simple as providing
  a byte buffer to use as scratch space when you build or read in a message.  Provide enough scratch
  space to hold the entire message and Cap'n Proto won't allocate any memory.  Or don't -- since
  Cap'n Proto doesn't do much allocation in the first place, the benefits of scratch space are
  small.
