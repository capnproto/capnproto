# Copyright (c) 2013, Kenton Varda <temporal@gmail.com>
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice, this
#    list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
# ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

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

  base @0 union {
    # The first element of the name.

    absoluteName @1 :LocatedText;   # A symbol at the global scope.
    relativeName @2 :LocatedText;   # A symbol that should be looked up lexically.
    importName @3 :LocatedText;     # A file name to import.
  }

  memberPath @4 :List(LocatedText);
  # List of `.member` suffixes.

  startByte @5 :UInt32;
  endByte @6 :UInt32;
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

  body @0 union {
    unknown @1 :Void;  # e.g. parse error; downstream should ignore
    positiveInt @2 :UInt64;
    negativeInt @3 :UInt64;
    float @4 :Float64;
    string @5 :Text;
    name @6 :DeclName;
    list @7 :List(ValueExpression);
    structValue @8 :List(FieldAssignment);
    unionValue @9 :FieldAssignment;
  }

  struct FieldAssignment {
    fieldName @0 :LocatedText;
    value @1 :ValueExpression;
  }

  startByte @10 :UInt32;
  endByte @11 :UInt32;
}

struct Declaration {
  # A declaration statement.

  name @0 :LocatedText;

  id @1 union {
    unspecified @2 :Void;
    uid @3 :LocatedInteger;
    ordinal @4 :LocatedInteger;  # limited to 16 bits
  }

  nestedDecls @17 :List(Declaration);

  annotations @5 :List(AnnotationApplication);
  struct AnnotationApplication {
    name @0 :DeclName;

    value @1 union {
      none @2 :Void;   # None specified; implies void value.
      expression @3 :ValueExpression;
    }
  }

  startByte @18 :UInt32;
  endByte @19 :UInt32;

  docComment @20 :Text;

  body @6 union {
    fileDecl @24 :File;
    usingDecl @7 :Using;
    constDecl @8 :Const;
    enumDecl @9 :Enum;
    enumerantDecl @10 :Enumerant;
    structDecl @11 :Struct;
    fieldDecl @12 :Field;
    unionDecl @13 :Union;
    groupDecl @23 :Group;
    interfaceDecl @14 :Interface;
    methodDecl @15 :Method;
    annotationDecl @16 :Annotation;

    nakedId @21 :LocatedInteger;
    nakedAnnotation @22 :AnnotationApplication;
    # A floating UID or annotation (allowed at the file top level).

    # The following declaration types are not produced by the parser, but are declared here
    # so that the compiler can handle symbol name lookups more uniformly.
    #
    # New union members added here will magically become visible in the global scope.
    # E.g. "builtinFoo" becomes visible as "Foo".
    builtinVoid @25 :Void;
    builtinBool @26 :Void;
    builtinInt8 @27 :Void;
    builtinInt16 @28 :Void;
    builtinInt32 @29 :Void;
    builtinInt64 @30 :Void;
    builtinUInt8 @31 :Void;
    builtinUInt16 @32 :Void;
    builtinUInt32 @33 :Void;
    builtinUInt64 @34 :Void;
    builtinFloat32 @35 :Void;
    builtinFloat64 @36 :Void;
    builtinText @37 :Void;
    builtinData @38 :Void;
    builtinList @39 :Void;
    builtinObject @40 :Void;
  }

  struct File {}

  struct Using {
    target @0 :DeclName;
  }

  struct Const {
    type @0 :TypeExpression;
    value @1 :ValueExpression;
  }

  struct Enum {}

  struct Enumerant {}

  struct Struct {}

  struct Field {
    type @0 :TypeExpression;
    defaultValue @1 union {
      none @2 :Void;
      value @3 :ValueExpression;
    }
  }

  struct Union {}

  struct Group {}

  struct Interface {}

  struct Method {
    params @0 :List(Param);
    struct Param {
      name @0 :LocatedText;  # If null, param failed to parse.
      type @1 :TypeExpression;
      annotations @2 :List(AnnotationApplication);
      defaultValue @3 union {
        none @4 :Void;
        value @5 :ValueExpression;
      }
    }

    returnType @1 union {
      none @2 :Void;   # No return type specified; implied Void.
      expression @3 :TypeExpression;
    }
  }

  struct Annotation {
    type @0 :TypeExpression;

    targetsFile @1 :Bool;
    targetsConst @2 :Bool;
    targetsEnum @3 :Bool;
    targetsEnumerant @4 :Bool;
    targetsStruct @5 :Bool;
    targetsField @6 :Bool;
    targetsUnion @7 :Bool;
    targetsInterface @8 :Bool;
    targetsMethod @9 :Bool;
    targetsParam @10 :Bool;
    targetsAnnotation @11 :Bool;
  }
}

struct ParsedFile {
  root @0 :Declaration;
}
