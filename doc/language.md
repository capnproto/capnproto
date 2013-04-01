---
layout: page
---

# Defining Types

Like Protocol Buffers and Thrift (but unlike JSON or MessagePack), Cap'n Proto messages are
strongly-typed and not self-describing. You must define your message structure in a special
language, then invoke the Cap'n Proto compiler (`capnpc`) to generate source code to manipulate
that message type in your desired language.

For example:

{% highlight capnp %}
struct Person {
  name @0 :Text;
  birthdate @3 :Date;

  email @1 :Text;
  phones @2 :List(PhoneNumber);

  struct PhoneNumber {
    number @0 :Text;
    type @1 :Type;

    enum Type {
      mobile @0;
      home @1;
      work @2;
    }
  }
}

struct Date {
  year @0 :Int16;
  month @1 :UInt8;
  day @2 :UInt8;
}
{% endhighlight %}

Some notes:

* Types come after names. This makes sense because the name is usually the thing you are looking
  for when you read the code, so it should be up front where it is easiest to see, not hidden later
  on the line. Sorry, C got it wrong.
* The `@N` annotations show how the protocol evolved over time, so that the system can make sure
  to maintain compatibility with older versions. Fields (and enum values, and interface methods)
  must be numbered consecutively starting from zero in the order in which they were added. In this
  example, it looks like the `birthdate` field was added to the `Person` structure recently -- its
  number is higher than the `email` and `phones` fields. Unlike Protobufs, you cannot skip numbers
  when defining fields -- but there was never any reason to do so anyway.

## Language Reference

### Comments

Comments are indicated by hash signs and extend to the end of the line:

{% highlight capnp %}
# This is a comment.
{% endhighlight %}

Comments meant as documentation should appear _after_ the declaration, either on the same line, or
on a subsequent line. Doc comments for aggregate definitions should appear on the line after the
opening brace.

{% highlight capnp %}
struct Date {
  # A standard Gregorian calendar date.

  year @0 :Int16;
  # The year.  Must include the century.
  # Negative value indicates BC.

  month @1 :UInt8;   # Month number, 1-12.
  day @2 :UInt8;     # Day number, 1-30.
}
{% endhighlight %}

Placing the comment _after_ the declaration rather than before makes the code more readable,
especially when doc comments grow long. You almost always need to see the declaration before you
can start reading the comment.

### Built-in Types

The following types are automatically defined:

* **Void:** `Void`
* **Boolean:** `Bool`
* **Integers:** `Int8`, `Int16`, `Int32`, `Int64`
* **Unsigned integers:** `UInt8`, `UInt16`, `UInt32`, `UInt64`
* **Floating-point:** `Float32`, `Float64`
* **Blobs:** `Text`, `Data`
* **Lists:** `List(T)`

Notes:

* The `Void` type has exactly one possible value, and thus can be encoded in zero bytes. It is
  rarely used, but can be useful as a union member.
* `Text` is always UTF-8 encoded and NUL-terminated.
* `Data` is a completely arbitrary sequence of bytes.
* `List` is a parameterized type, where the parameter is the element type. For example,
  `List(Int32)`, `List(Person)`, and `List(List(Text))` are all valid.

### Structs

A struct has a set of named, typed fields, numbered consecutively starting from zero.

{% highlight capnp %}
struct Person {
  name @0 :Text;
  email @1 :Text;
}
{% endhighlight %}

Fields can have default values:


{% highlight capnp %}
foo @0 :Int32 = 123;
bar @1 :Text = "blah";
baz @2 :List(Bool) = [ true, false, false, true ];
qux @3 :Person = (name = "Bob", email = "bob@example.com");
corge @4 :Void = void;
{% endhighlight %}


### Unions

A union is two or more fields of a struct which are stored in the same location. Only one of
these fields can be set at a time, and a separate tag is maintained to track which one is
currently set. Unlike in C, unions are not types, they are simply properties of fields, therefore
union declarations do not look like types.

{% highlight capnp %}
struct Person {
  # ...

  union employment @4;

  unemployed @5 in employment :Void;
  employer @6 in employment :Company;
  school @7 in employment :School;
  selfEmployed @8 in employment :Void;
}
{% endhighlight %}

Notes:

* Unions are numbered in the same number space as other fields. Remember that the purpose of the
  numbers is to indicate the evolution order of the struct. The system needs to know when the union
  was declared relative to the fields in it. Also note that at most one element of the union is
  allowed to have a number less than the union's number, as unionizing two or more existing fields
  would change their layout.

* Notice that we used the "useless" `Void` type here. We don't have any extra information to store
  for the `unemployed` or `selfEmployed` cases, but we still want the union to have tags for them.

### Enums

An enum is a type with a small finite set of symbolic values.

{% highlight capnp %}
enum Rfc3092Variable {
  foo @0;
  bar @1;
  baz @2;
  qux @3;
  # ...
}
{% endhighlight %}

Like fields, enum values must be numbered sequentially starting from zero. In languages where
enums have numeric values, these numbers will be used, but in general Cap'n Proto enums should not
be considered numeric.

### Interfaces

An interface has a collection of methods, each of which takes some parameters and returns a
result. Like struct fields, methods are numbered.

{% highlight capnp %}
interface Directory {
  list @0 () :List(FileInfo);
  create @1 (name :Text) :FileInfo;
  open @2 (name :Text) :FileInfo;
  delete @3 (name :Text) :Void;
  link @4 (name :Text, file :File) :Void;
}

struct FileInfo {
  name @0 :Text;
  size @1 :UInt64;
  file @2 :File;   # A pointer to a File.
}

interface File {
  read @0 (startAt :UInt64 = 0, amount :UInt64 = 0xffffffffffffffff) :Data;
  # Default params = read entire file.

  write @1 (startAt :UInt64, data :Data) :Void;
  truncate @2 (size :UInt64) :Void;
}
{% endhighlight %}

Notice something interesting here: `FileInfo` is a struct, but it contains a `File`, which is an
interface. Structs (and primitive types) are passed by value, but interfaces are passed by
reference. So when `Directory.open` is called remotely, the content of a `FileInfo` (including
values for `name` and `size`) is transmitted back, but for the `file` field, only the address of
some remote `File` object is sent.

When an address of an object is transmitted, the RPC system automatically manages making sure that
the recipient gets permission to call the addressed object -- because if the recipient wasn't
meant to have access, the sender shouldn't have sent the reference in the first place. This makes
it very easy to develop secure protocols with Cap'n Proto -- you almost don't need to think about
access control at all. This feature is what makes Cap'n Proto a "capability-based" RPC system -- a
reference to an object inherently represents a "capability" to access it.

### Constants

You can define constants in Cap'n Proto. Constants can have any value.

{% highlight capnp %}
const pi :Float32 = 3.14159;
const bob :Person = (name = "Bob", email = "bob@example.com");
{% endhighlight %}

### Nesting, Scope, and Aliases

You can nest constant, alias, and type definitions inside structs and interfaces (but not enums).
This has no effect on any definition involved except to define the scope of its name. So in Java
terms, inner classes are always "static". To name a nested type from another scope, separate the
path with `.`s.

{% highlight capnp %}
struct Foo {
  struct Bar {
    #...
  }
  bar@0 :Bar;
}

struct Baz {
  bar@0 :Foo.Bar;
}
{% endhighlight %}

If typing long scopes becomes cumbersome, you can use `using` to declare an alias.

{% highlight capnp %}
struct Qux {
  using Foo.Bar;
  bar@0 :Bar;
}

struct Corge {
  using T = Foo.Bar;
  bar@0 :T;
}
{% endhighlight %}

### Imports

An `import` expression names the scope of some other file:

{% highlight capnp %}
struct Foo {
  # Use type "Baz" defined in bar.capnp.
  baz@0 :import "bar.capnp".Baz;
}
{% endhighlight %}

Of course, typically it's more readable to define an alias:

{% highlight capnp %}
using Bar = import "bar.capnp";

struct Foo {
  # Use type "Baz" defined in bar.capnp.
  baz@0 :Bar.Baz;
}
{% endhighlight %}

Or even:

{% highlight capnp %}
using import "bar.capnp".Baz;

struct Foo {
  baz @0 :Baz;
}
{% endhighlight %}

## Evolving Your Protocol

A protocol can be changed in the following ways without breaking backwards-compatibility:

* New types, constants, and aliases can be added anywhere, since they obviously don't affect the
  encoding of any existing type.
* New fields, values, and methods may be added to structs, enums, and interfaces, respectively,
  with the numbering rules described earlier.
* New parameters may be added to a method.  The new parameters must be added to the end of the
  parameter list and must have default values.
* Any symbolic name can be changed, as long as the ordinal numbers stay the same.
* A field of type `List(T)`, where `T` is NOT a struct type, may be changed to type `List(U)`,
  where `U` is a struct type whose field number 0 is of type `T`.  This rule is useful when you
  realize too late that you need to attach some extra data to each element of your list.  Without
  this rule, you would be stuck defining parallel lists, which are ugly.

Any other change should be assumed NOT to be safe.  Also, these rules only apply to the Cap'n Proto
native encoding.  It is sometimes useful to transcode Cap'n Proto types to other formats, like
JSON, which may have different rules (e.g., field names cannot change in JSON).
