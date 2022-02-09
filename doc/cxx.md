---
layout: page
title: C++ Serialization
---

# C++ Serialization

The Cap'n Proto C++ runtime implementation provides an easy-to-use interface for manipulating
messages backed by fast pointer arithmetic.  This page discusses the serialization layer of
the runtime; see [C++ RPC](cxxrpc.html) for information about the RPC layer.

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

  employment :union {
    unemployed @4 :Void;
    employer @5 :Text;
    school @6 :Text;
    selfEmployed @7 :Void;
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
See the header `kj/exception.h` for details on how to register an exception callback.

## KJ Library

Cap'n Proto is built on top of a basic utility library called KJ.  The two were actually developed
together -- KJ is simply the stuff which is not specific to Cap'n Proto serialization, and may be
useful to others independently of Cap'n Proto.  For now, the two are distributed together.  The
name "KJ" has no particular meaning; it was chosen to be short and easy-to-type.

As of v0.3, KJ is distributed with Cap'n Proto but built as a separate library.  You may need
to explicitly link against libraries:  `-lcapnp -lkj`

## Generating Code

To generate C++ code from your `.capnp` [interface definition](language.html), run:

    capnp compile -oc++ myproto.capnp

This will create `myproto.capnp.h` and `myproto.capnp.c++` in the same directory as `myproto.capnp`.

To use this code in your app, you must link against both `libcapnp` and `libkj`.  If you use
`pkg-config`, Cap'n Proto provides the `capnp` module to simplify discovery of compiler and linker
flags.

If you use [RPC](cxxrpc.html) (i.e., your schema defines [interfaces](language.html#interfaces)),
then you will additionally need to link against `libcapnp-rpc` and `libkj-async`, or use the
`capnp-rpc` `pkg-config` module.

### Setting a Namespace

You probably want your generated types to live in a C++ namespace.  You will need to import
`/capnp/c++.capnp` and use the `namespace` annotation it defines:

{% highlight capnp %}
using Cxx = import "/capnp/c++.capnp";
$Cxx.namespace("foo::bar::baz");
{% endhighlight %}

Note that `capnp/c++.capnp` is installed in `$PREFIX/include` (`/usr/local/include` by default)
when you install the C++ runtime.  The `capnp` tool automatically searches `/usr/include` and
`/usr/local/include` for imports that start with a `/`, so it should "just work".  If you installed
somewhere else, you may need to add it to the search path with the `-I` flag to `capnp compile`,
which works much like the compiler flag of the same name.

## Types

### Primitive Types

Primitive types map to the obvious C++ types:

* `Bool` -> `bool`
* `IntNN` -> `intNN_t`
* `UIntNN` -> `uintNN_t`
* `Float32` -> `float`
* `Float64` -> `double`
* `Void` -> `::capnp::Void` (An empty struct; its only value is `::capnp::VOID`)

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

`Foo::Builder`, meanwhile, has several methods for each field `bar`:

* `getBar()`:  For primitives, returns the value.  For composites, returns a Builder for the
  composite.  If a composite field has not been initialized (i.e. this is the first time it has
  been accessed), it will be initialized to a copy of the field's default value before returning.
* `setBar(x)`:  For primitives, sets the value to x.  For composites, sets the value to a deep copy
  of x, which must be a Reader for the type.
* `initBar(n)`:  Only for lists and blobs.  Sets the field to a newly-allocated list or blob
  of size n and returns a Builder for it.  The elements of the list are initialized to their empty
  state (zero for numbers, default values for structs).
* `initBar()`:  Only for structs.  Sets the field to a newly-allocated struct and returns a
  Builder for it.  Note that the newly-allocated struct is initialized to the default value for
  the struct's _type_ (i.e., all-zero) rather than the default value for the field `bar` (if it
  has one).
* `hasBar()`:  Only for pointer fields (e.g. structs, lists, blobs).  Returns true if the pointer
  has been initialized (non-null).  (This method is also available on readers.)
* `adoptBar(x)`:  Only for pointer fields.  Adopts the orphaned object x, linking it into the field
  `bar` without copying.  See the section on orphans.
* `disownBar()`:  Disowns the value pointed to by `bar`, setting the pointer to null and returning
  its previous value as an orphan.  See the section on orphans.

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

### Groups

Groups look a lot like a combination of a nested type and a field of that type, except that you
cannot set, adopt, or disown a group -- you can only get and init it.

### Unions

A named union (as opposed to an unnamed one) works just like a group, except with some additions:

* For each field `foo`, the union reader and builder have a method `isFoo()` which returns true
  if `foo` is the currently-set field in the union.
* The union reader and builder also have a method `which()` that returns an enum value indicating
  which field is currently set.
* Calling the set, init, or adopt accessors for a field makes it the currently-set field.
* Calling the get or disown accessors on a field that isn't currently set will throw an
  exception in debug mode or return garbage when `NDEBUG` is defined.

Unnamed unions differ from named unions only in that the accessor methods from the union's members
are added directly to the containing type's reader and builder, rather than generating a nested
type.

See the [example](#example-usage) at the top of the page for an example of unions.

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
(whereas in the schema language you'd write them in camelCase).

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

As a special convenience, if you are using GCC 4.8+ or Clang, `Text::Reader` (and its underlying
type, `kj::StringPtr`) can be implicitly converted to and from `std::string` format.  This is
accomplished without actually `#include`ing `<string>`, since some clients do not want to rely
on this rather-bulky header.  In fact, any class which defines a `.c_str()` method will be
implicitly convertible in this way.  Unfortunately, this trick doesn't work on GCC 4.7.

### Interfaces

[Interfaces (RPC) have their own page.](cxxrpc.html)

### Generics

[Generic types](language.html#generic-types) become templates in C++. The outer type (the one whose
name matches the schema declaration's name) is templatized; the inner `Reader` and `Builder` types
are not, because they inherit the parameters from the outer type. Similarly, template parameters
should refer to outer types, not `Reader` or `Builder` types.

For example, given:

{% highlight capnp %}
struct Map(Key, Value) {
  entries @0 :List(Entry);
  struct Entry {
    key @0 :Key;
    value @1 :Value;
  }
}

struct People {
  byName @0 :Map(Text, Person);
  # Maps names to Person instances.
}
{% endhighlight %}

You might write code like:

{% highlight c++ %}
void processPeople(People::Reader people) {
  Map<Text, Person>::Reader reader = people.getByName();
  capnp::List<Map<Text, Person>::Entry>::Reader entries =
      reader.getEntries()
  for (auto entry: entries) {
    processPerson(entry);
  }
}
{% endhighlight %}

Note that all template parameters will be specified with a default value of `AnyPointer`.
Therefore, the type `Map<>` is equivalent to `Map<capnp::AnyPointer, capnp::AnyPointer>`.

### Constants

Constants are exposed with their names converted to UPPERCASE_WITH_UNDERSCORES naming style
(whereas in the schema language you’d write them in camelCase).  Primitive constants are just
`constexpr` values.  Pointer-type constants (e.g. structs, lists, and blobs) are represented
using a proxy object that can be converted to the relevant `Reader` type, either implicitly or
using the unary `*` or `->` operators.

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

There is an [example](#example-usage) of all this at the beginning of this page.

### Using mmap

Cap'n Proto can be used together with `mmap()` (or Win32's `MapViewOfFile()`) for extremely fast
reads, especially when you only need to use a subset of the data in the file.  Currently,
Cap'n Proto is not well-suited for _writing_ via `mmap()`, only reading, but this is only because
we have not yet invented a mutable segment framing format -- the underlying design should
eventually work for both.

To take advantage of `mmap()` at read time, write your file in regular serialized (but NOT packed)
format -- that is, use `writeMessageToFd()`, _not_ `writePackedMessageToFd()`.  Now, `mmap()` in
the entire file, and then pass the mapped memory to the constructor of
`capnp::FlatArrayMessageReader` (defined in `capnp/serialize.h`).  That's it.  You can use the
reader just like a normal `StreamFdMessageReader`.  The operating system will automatically page
in data from disk as you read it.

`mmap()` works best when reading from flash media, or when the file is already hot in cache.
It works less well with slow rotating disks.  Here, disk seeks make random access relatively
expensive.  Also, if I/O throughput is your bottleneck, then the fact that mmaped data cannot
be packed or compressed may hurt you.  However, it all depends on what fraction of the file you're
actually reading -- if you only pull one field out of one deeply-nested struct in a huge tree, it
may still be a win.  The only way to know for sure is to do benchmarks!  (But be careful to make
sure your benchmark is actually interacting with disk and not cache.)

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
  alice.get("employment").as<DynamicStruct>()
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
  bob.get("employment").as<DynamicStruct>()
     .set("unemployed", ::capnp::VOID);

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
      for (auto field: structValue.getSchema().getFields()) {
        if (!structValue.has(field)) continue;
        if (first) {
          first = false;
        } else {
          std::cout << ", ";
        }
        std::cout << field.getProto().getName().cStr()
                  << " = ";
        dynamicPrintValue(structValue.get(field));
      }
      std::cout << ")";
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

* While `SchemaLoader` loads binary schemas, you can also parse directly from text using
  `SchemaParser` (`capnp/schema-parser.h`).  However, this requires linking against `libcapnpc`
  (in addition to `libcapnp` and `libkj`) -- this code is bulky and not terribly efficient.  If
  you can arrange to use only binary schemas at runtime, you'll be better off.

* Unlike with Protobufs, there is no "global registry" of compiled-in types.  To get the schema
  for a compiled-in type, use `capnp::Schema::from<MyType>()`.

* Unlike with Protobufs, the overhead of supporting reflection is small.  Generated `.capnp.c++`
  files contain only some embedded const data structures describing the schema, no code at all,
  and the runtime library support code is relatively small.  Moreover, if you do not use the
  dynamic API or the schema API, you do not even need to link their implementations into your
  executable.

* The dynamic API performs type checks at runtime.  In case of error, it will throw an exception.
  If you compile with `-fno-exceptions`, it will crash instead.  Correct usage of the API should
  never throw, but bugs happen.  Enabling and catching exceptions will make your code more robust.

* Loading user-provided schemas has security implications: it greatly increases the attack
  surface of the Cap'n Proto library.  In particular, it is easy for an attacker to trigger
  exceptions.  To protect yourself, you are strongly advised to enable exceptions and catch them.

## Orphans

An "orphan" is a Cap'n Proto object that is disconnected from the message structure.  That is,
it is not the root of a message, and there is no other Cap'n Proto object holding a pointer to it.
Thus, it has no parents.  Orphans are an advanced feature that can help avoid copies and make it
easier to use Cap'n Proto objects as part of your application's internal state.  Typical
applications probably won't use orphans.

The class `capnp::Orphan<T>` (defined in `<capnp/orphan.h>`) represents a pointer to an orphaned
object of type `T`.  `T` can be any struct type, `List<T>`, `Text`, or `Data`.  E.g.
`capnp::Orphan<Person>` would be an orphaned `Person` structure.  `Orphan<T>` is a move-only class,
similar to `std::unique_ptr<T>`.  This prevents two different objects from adopting the same
orphan, which would result in an invalid message.

An orphan can be "adopted" by another object to link it into the message structure.  Conversely,
an object can "disown" one of its pointers, causing the pointed-to object to become an orphan.
Every pointer-typed field `foo` provides builder methods `adoptFoo()` and `disownFoo()` for these
purposes.  Again, these methods use C++11 move semantics.  To use them, you will need to be
familiar with `std::move()` (or the equivalent but shorter-named `kj::mv()`).

Even though an orphan is unlinked from the message tree, it still resides inside memory allocated
for a particular message (i.e. a particular `MessageBuilder`).  An orphan can only be adopted by
objects that live in the same message.  To move objects between messages, you must perform a copy.
If the message is serialized while an `Orphan<T>` living within it still exists, the orphan's
content will be part of the serialized message, but the only way the receiver could find it is by
investigating the raw message; the Cap'n Proto API provides no way to detect or read it.

To construct an orphan from scratch (without having some other object disown it), you need an
`Orphanage`, which is essentially an orphan factory associated with some message.  You can get one
by calling the `MessageBuilder`'s `getOrphanage()` method, or by calling the static method
`Orphanage::getForMessageContaining(builder)` and passing it any struct or list builder.

Note that when an `Orphan<T>` goes out-of-scope without being adopted, the underlying memory that
it occupied is overwritten with zeros.  If you use packed serialization, these zeros will take very
little bandwidth on the wire, but will still waste memory on the sending and receiving ends.
Generally, you should avoid allocating message objects that won't be used, or if you cannot avoid
it, arrange to copy the entire message over to a new `MessageBuilder` before serializing, since
only the reachable objects will be copied.

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

## Tips and Best Practices

Here are some tips for using the C++ Cap'n Proto runtime most effectively:

* Accessor methods for primitive (non-pointer) fields are fast and inline.  They should be just
  as fast as accessing a struct field through a pointer.

* Accessor methods for pointer fields, on the other hand, are not inline, as they need to validate
  the pointer.  If you intend to access the same pointer multiple times, it is a good idea to
  save the value to a local variable to avoid repeating this work.  This is generally not a
  problem given C++11's `auto`.

  Example:

      // BAD
      frob(foo.getBar().getBaz(),
           foo.getBar().getQux(),
           foo.getBar().getCorge());

      // GOOD
      auto bar = foo.getBar();
      frob(bar.getBaz(), bar.getQux(), bar.getCorge());

  It is especially important to use this style when reading messages, for another reason:  as
  described under the "security tips" section, below, every time you `get` a pointer, Cap'n Proto
  increments a counter by the size of the target object.  If that counter hits a pre-defined limit,
  an exception is thrown (or a default value is returned, if exceptions are disabled), to prevent
  a malicious client from sending your server into an infinite loop with a specially-crafted
  message.  If you repeatedly `get` the same object, you are repeatedly counting the same bytes,
  and so you may hit the limit prematurely.  (Since Cap'n Proto readers are backed directly by
  the underlying message buffer and do not have anywhere else to store per-object information, it
  is impossible to remember whether you've seen a particular object already.)

* Internally, all pointer fields start out "null", even if they have default values.  When you have
  a pointer field `foo` and you call `getFoo()` on the containing struct's `Reader`, if the field
  is "null", you will receive a reader for that field's default value.  This reader is backed by
  read-only memory; nothing is allocated.  However, when you call `get` on a _builder_, and the
  field is null, then the implementation must make a _copy_ of the default value to return to you.
  Thus, you've caused the field to become non-null, just by "reading" it.  On the other hand, if
  you call `init` on that field, you are explicitly replacing whatever value is already there
  (null or not) with a newly-allocated instance, and that newly-allocated instance is _not_ a
  copy of the field's default value, but just a completely-uninitialized instance of the
  appropriate type.

* It is possible to receive a struct value constructed from a newer version of the protocol than
  the one your binary was built with, and that struct might have extra fields that you don't know
  about.  The Cap'n Proto implementation tries to avoid discarding this extra data.  If you copy
  the struct from one message to another (e.g. by calling a set() method on a parent object), the
  extra fields will be preserved.  This makes it possible to build proxies that receive messages
  and forward them on without having to rebuild the proxy every time a new field is added.  You
  must be careful, however:  in some cases, it's not possible to retain the extra fields, because
  they need to be copied into a space that is allocated before the expected content is known.
  In particular, lists of structs are represented as a flat array, not as an array of pointers.
  Therefore, all memory for all structs in the list must be allocated upfront.  Hence, copying
  a struct value from another message into an element of a list will truncate the value.  Because
  of this, the setter method for struct lists is called `setWithCaveats()` rather than just `set()`.

* Messages are built in "arena" or "region" style:  each object is allocated sequentially in
  memory, until there is no more room in the segment, in which case a new segment is allocated,
  and objects continue to be allocated sequentially in that segment.  This design is what makes
  Cap'n Proto possible at all, and it is very fast compared to other allocation strategies.
  However, it has the disadvantage that if you allocate an object and then discard it, that memory
  is lost.  In fact, the empty space will still become part of the serialized message, even though
  it is unreachable.  The implementation will try to zero it out, so at least it should pack well,
  but it's still better to avoid this situation.  Some ways that this can happen include:
  * If you `init` a field that is already initialized, the previous value is discarded.
  * If you create an orphan that is never adopted into the message tree.
  * If you use `adoptWithCaveats` to adopt an orphaned struct into a struct list, then a shallow
    copy is necessary, since the struct list requires that its elements are sequential in memory.
    The previous copy of the struct is discarded (although child objects are transferred properly).
  * If you copy a struct value from another message using a `set` method, the copy will have the
    same size as the original.  However, the original could have been built with an older version
    of the protocol which lacked some fields compared to the version your program was built with.
    If you subsequently `get` that struct, the implementation will be forced to allocate a new
    (shallow) copy which is large enough to hold all known fields, and the old copy will be
    discarded.  Child objects will be transferred over without being copied -- though they might
    suffer from the same problem if you `get` them later on.
  Sometimes, avoiding these problems is too inconvenient.  Fortunately, it's also possible to
  clean up the mess after-the-fact:  if you copy the whole message tree into a fresh
  `MessageBuilder`, only the reachable objects will be copied, leaving out all of the unreachable
  dead space.

  In the future, Cap'n Proto may be improved such that it can re-use dead space in a message.
  However, this will only improve things, not fix them entirely: fragmentation could still leave
  dead space.

### Build Tips

* If you are worried about the binary footprint of the Cap'n Proto library, consider statically
  linking with the `--gc-sections` linker flag.  This will allow the linker to drop pieces of the
  library that you do not actually use.  For example, many users do not use the dynamic schema and
  reflection APIs, which contribute a large fraction of the Cap'n Proto library's overall
  footprint.  Keep in mind that if you ever stringify a Cap'n Proto type, the stringification code
  depends on the dynamic API; consider only using stringification in debug builds.

  If you are dynamically linking against the system's shared copy of `libcapnp`, don't worry about
  its binary size.  Remember that only the code which you actually use will be paged into RAM, and
  those pages are shared with other applications on the system.

  Also remember to strip your binary.  In particular, `libcapnpc` (the schema parser) has
  excessively large symbol names caused by its use of template-based parser combinators.  Stripping
  the binary greatly reduces its size.

* The Cap'n Proto library has lots of debug-only asserts that are removed if you `#define NDEBUG`,
  including in headers.  If you care at all about performance, you should compile your production
  binaries with the `-DNDEBUG` compiler flag.  In fact, if Cap'n Proto detects that you have
  optimization enabled but have not defined `NDEBUG`, it will define it for you (with a warning),
  unless you define `DEBUG` or `KJ_DEBUG` to explicitly request debugging.

### Security Tips

Cap'n Proto has not yet undergone security review.  It most likely has some vulnerabilities.  You
should not attempt to decode Cap'n Proto messages from sources you don't trust at this time.

However, assuming the Cap'n Proto implementation hardens up eventually, then the following security
tips will apply.

* It is highly recommended that you enable exceptions.  When compiled with `-fno-exceptions`,
  Cap'n Proto categorizes exceptions into "fatal" and "recoverable" varieties.  Fatal exceptions
  cause the server to crash, while recoverable exceptions are handled by logging an error and
  returning a "safe" garbage value.  Fatal is preferred in cases where it's unclear what kind of
  garbage value would constitute "safe".  The more of the library you use, the higher the chance
  that you will leave yourself open to the possibility that an attacker could trigger a fatal
  exception somewhere.  If you enable exceptions, then you can catch the exception instead of
  crashing, and return an error just to the attacker rather than to everyone using your server.

  Basic parsing of Cap'n Proto messages shouldn't ever trigger fatal exceptions (assuming the
  implementation is not buggy).  However, the dynamic API -- especially if you are loading schemas
  controlled by the attacker -- is much more exception-happy.  If you cannot use exceptions, then
  you are advised to avoid the dynamic API when dealing with untrusted data.

* If you need to process schemas from untrusted sources, take them in binary format, not text.
  The text parser is a much larger attack surface and not designed to be secure.  For instance,
  as of this writing, it is trivial to deadlock the parser by simply writing a constant whose value
  depends on itself.

* Cap'n Proto automatically applies two artificial limits on messages for security reasons:
  a limit on nesting dept, and a limit on total bytes traversed.

  * The nesting depth limit is designed to prevent stack overflow when handling a deeply-nested
    recursive type, and defaults to 64.  If your types aren't recursive, it is highly unlikely
    that you would ever hit this limit, and even if they are recursive, it's still unlikely.

  * The traversal limit is designed to defend against maliciously-crafted messages which use
    pointer cycles or overlapping objects to make a message appear much larger than it looks off
    the wire.  While cycles and overlapping objects are illegal, they are hard to detect reliably.
    Instead, Cap'n Proto places a limit on how many bytes worth of objects you can _dereference_
    before it throws an exception.  This limit is assessed every time you follow a pointer.  By
    default, the limit is 64MiB (this may change in the future).  `StreamFdMessageReader` will
    actually reject upfront any message which is larger than the traversal limit, even before you
    start reading it.

    If you need to write your code in such a way that you might frequently re-read the same
    pointers, instead of increasing the traversal limit to the point where it is no longer useful,
    consider simply copying the message into a new `MallocMessageBuilder` before starting.  Then,
    the traversal limit will be enforced only during the copy.  There is no traversal limit on
    objects once they live in a `MessageBuilder`, even if you use `.asReader()` to convert a
    particular object's builder to the corresponding reader type.

  Both limits may be increased using `capnp::ReaderOptions`, defined in `capnp/message.h`.

* Remember that enums on the wire may have a numeric value that does not match any value defined
  in the schema.  Your `switch()` statements must always have a safe default case.

## Lessons Learned from Protocol Buffers

The author of Cap'n Proto's C++ implementation also wrote (in the past) version 2 of Google's
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
  As a result, the Cap'n Proto runtime library is smaller, and code that uses it compiles quickly.

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
