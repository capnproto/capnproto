# Copyright (c) 2013-2014 Sandstorm Development Group, Inc. and contributors
# Licensed under the MIT License:
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.

@0xc56be168dcbbc3c6;
# The structures in this file correspond to the AST of the Cap'n Proto schema language.
#
# This file is intended to be used internally by capnpc.  Mostly, it is useful because it is more
# convenient that defining data classes in C++, particularly where variant types (unions) are
# needed.  Over time, this file may change in backwards-incompatible ways.

using Cxx = import "/capnp/c++.capnp";

$Cxx.namespace("capnp::compiler");

# TODO(someday):  Here's a case where parameterized types might be nice, but note that it would
#   need to support primitive parameters...
struct LocatedText {
  value @0 :Text;
  startByte @1 :UInt32;
  endByte @2 :UInt32;
}

struct LocatedInteger {
  value @0 :UInt64;
  startByte @1 :UInt32;
  endByte @2 :UInt32;
}

struct LocatedFloat {
  value @0 :Float64;
  startByte @1 :UInt32;
  endByte @2 :UInt32;
}

struct DeclName {
  # An expressing naming a thing declared elsewhere.  Examples:
  # * `MyType`
  # * `foo.bar.Baz`
  # * `.absolute.path.to.SomeType`
  # * `import "foo.capnp"`

  base :union {
    # The first element of the name.

    absoluteName @0 :LocatedText;   # A symbol at the global scope.
    relativeName @1 :LocatedText;   # A symbol that should be looked up lexically.
    importName @2 :LocatedText;     # A file name to import.
  }

  memberPath @3 :List(LocatedText);
  # List of `.member` suffixes.

  startByte @4 :UInt32;
  endByte @5 :UInt32;
}

struct TypeExpression {
  # An expression evaluating to a type.

  name @0 :DeclName;
  # Name of the type declaration.

  params @1 :List(TypeExpression);
  # Type parameters, if any.  E.g. `List(Foo)` has one type parameter `Foo`.
  #
  # If a param failed to parse, its `name` may be null, and it should be ignored.

  startByte @2 :UInt32;
  endByte @3 :UInt32;
}

struct ValueExpression {
  # An expression evaluating to a value.

  union {
    unknown @0 :Void;  # e.g. parse error; downstream should ignore
    positiveInt @1 :UInt64;
    negativeInt @2 :UInt64;
    float @3 :Float64;
    string @4 :Text;
    binary @10 :Data;
    name @5 :DeclName;
    list @6 :List(ValueExpression);
    struct @7 :List(FieldAssignment);
  }

  struct FieldAssignment {
    fieldName @0 :LocatedText;
    value @1 :ValueExpression;
  }

  startByte @8 :UInt32;
  endByte @9 :UInt32;
}

struct Declaration {
  # A declaration statement.

  name @0 :LocatedText;

  id :union {
    unspecified @1 :Void;
    uid @2 :LocatedInteger;
    ordinal @3 :LocatedInteger;  # limited to 16 bits
  }

  nestedDecls @4 :List(Declaration);

  annotations @5 :List(AnnotationApplication);
  struct AnnotationApplication {
    name @0 :DeclName;

    value :union {
      none @1 :Void;   # None specified; implies void value.
      expression @2 :ValueExpression;
    }
  }

  startByte @6 :UInt32;
  endByte @7 :UInt32;

  docComment @8 :Text;

  union {
    file @9 :Void;

    using :group {
      target @10 :DeclName;
    }

    const :group {
      type @11 :TypeExpression;
      value @12 :ValueExpression;
    }

    enum @13 :Void;
    enumerant @14 :Void;

    struct @15 :Void;
    field :group {
      type @16 :TypeExpression;
      defaultValue :union {
        none @17 :Void;
        value @18 :ValueExpression;
      }
    }
    union @19 :Void;
    group @20 :Void;

    interface :group {
      extends @21 :List(DeclName);
    }
    method :group {
      params @22 :ParamList;
      results :union {
        none @23 :Void;
        explicit @24 :ParamList;
      }
    }

    annotation :group {
      type @25 :TypeExpression;

      targetsFile @26 :Bool;
      targetsConst @27 :Bool;
      targetsEnum @28 :Bool;
      targetsEnumerant @29 :Bool;
      targetsStruct @30 :Bool;
      targetsField @31 :Bool;
      targetsUnion @32 :Bool;
      targetsGroup @33 :Bool;
      targetsInterface @34 :Bool;
      targetsMethod @35 :Bool;
      targetsParam @36 :Bool;
      targetsAnnotation @37 :Bool;
    }

    nakedId @38 :LocatedInteger;
    nakedAnnotation @39 :AnnotationApplication;
    # A floating UID or annotation (allowed at the file top level).

    # The following declaration types are not produced by the parser, but are declared here
    # so that the compiler can handle symbol name lookups more uniformly.
    #
    # New union members added here will magically become visible in the global scope.
    # E.g. "builtinFoo" becomes visible as "Foo".
    builtinVoid @40 :Void;
    builtinBool @41 :Void;
    builtinInt8 @42 :Void;
    builtinInt16 @43 :Void;
    builtinInt32 @44 :Void;
    builtinInt64 @45 :Void;
    builtinUInt8 @46 :Void;
    builtinUInt16 @47 :Void;
    builtinUInt32 @48 :Void;
    builtinUInt64 @49 :Void;
    builtinFloat32 @50 :Void;
    builtinFloat64 @51 :Void;
    builtinText @52 :Void;
    builtinData @53 :Void;
    builtinList @54 :Void;
    builtinObject @55 :Void;  # only for "renamed to AnyPointer" error message
    builtinAnyPointer @56 :Void;
  }

  struct ParamList {
    # A list of method parameters or method returns.

    union {
      namedList @0 :List(Param);

      type @1 :DeclName;
      # Specified some other struct type instead of a named list.
    }

    startByte @2 :UInt32;
    endByte @3 :UInt32;
  }
  struct Param {
    name @0 :LocatedText;  # If null, param failed to parse.
    type @1 :TypeExpression;
    annotations @2 :List(AnnotationApplication);
    defaultValue :union {
      none @3 :Void;
      value @4 :ValueExpression;
    }

    startByte @5 :UInt32;
    endByte @6 :UInt32;
  }
}

struct ParsedFile {
  root @0 :Declaration;
}
