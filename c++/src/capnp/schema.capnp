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

using Cxx = import "c++.capnp";

@0xb471df2f45ca32c7;
$Cxx.namespace("capnp::schema");

# WARNING:  This protocol is still subject to backwards-incompatible change.

using Id = UInt64;
# The globally-unique ID of a file, type, or annotation.

struct Node {
  id @0 :Id;

  displayName @1 :Text;
  # Name to present to humans to identify this Node.  You should not attempt to parse this.  Its
  # format could change.  It is not guaranteed to be unique.
  #
  # (On Zooko's triangle, this is the node's nickname.)

  scopeId @2 :Id = 0;
  # ID of the lexical parent node.  Typically, the scope node will have a NestedNode pointing back
  # at this node, but robust code should avoid relying on this.  `scopeId` is zero if the node has
  # no parent, which is normally only the case with files, but should be allowed for any kind of
  # node (in order to make runtime type generation easier).

  nestedNodes @3 :List(NestedNode);
  # List of nodes nested within this node, along with the names under which they were declared.

  struct NestedNode {
    name @0 :Text;
    # Unqualified symbol name.  Unlike Node.name, this *can* be used programmatically.
    #
    # (On Zooko's triangle, this is the node's petname according to its parent scope.)

    id @1 :Id;
    # ID of the nested node.  Typically, the target node's scopeId points back to this node, but
    # robust code should avoid relying on this.
  }

  annotations @4 :List(Annotation);
  # Annotations applied to this node.

  body @5 union {
    # Info specific to each kind of node.

    fileNode @6 :FileNode;
    structNode @7 :StructNode;
    enumNode @8 :EnumNode;
    interfaceNode @9 :InterfaceNode;
    constNode @10 :ConstNode;
    annotationNode @11 :AnnotationNode;
  }
}

struct Type {
  # Represents a type expression.

  body @0 union {
    voidType @1 :Void;
    boolType @2 :Void;
    int8Type @3 :Void;
    int16Type @4 :Void;
    int32Type @5 :Void;
    int64Type @6 :Void;
    uint8Type @7 :Void;
    uint16Type @8 :Void;
    uint32Type @9 :Void;
    uint64Type @10 :Void;
    float32Type @11 :Void;
    float64Type @12 :Void;
    textType @13 :Void;
    dataType @14 :Void;

    listType @15 :Type;  # Value = the element type.

    enumType @16 :Id;
    structType @17 :Id;
    interfaceType @18 :Id;

    objectType @19 :Void;
  }
}

struct Value {
  # Represents a value, e.g. a field default value, constant value, or annotation value.

  body @0 union {
    # Note ordinals 1 and 10 are intentionally swapped to improve union layout.
    voidValue @10 :Void;
    boolValue @2 :Bool;
    int8Value @3 :Int8;
    int16Value @4 :Int16;
    int32Value @5 :Int32;
    int64Value @6 :Int64;
    uint8Value @7 :UInt8;
    uint16Value @8 :UInt16;
    uint32Value @9 :UInt32;
    uint64Value @1 :UInt64;
    float32Value @11 :Float32;
    float64Value @12 :Float64;
    textValue @13 :Text;
    dataValue @14 :Data;

    listValue @15 :Object;

    enumValue @16 :UInt16;
    structValue @17 :Object;

    interfaceValue @18 :Void;
    # The only interface value that can be represented statically is "null", whose methods always
    # throw exceptions.

    objectValue @19 :Object;
  }
}

struct Annotation {
  # Describes an annotation applied to a declaration.  Note AnnotationNode describes the
  # annotation's declaration, while this describes a use of the annotation.

  id @0 :Id;
  # ID of the annotation node.

  value @1 :Value;
}

struct FileNode {
  imports @0 :List(Import);
  struct Import {
    id @0 :Id;
    # DEPRECATED:  ID of the imported file.  This is no longer filled in because it is hostile to
    # lazy importing:  since this import list appears in the FileNode, and since the FileNode must
    # necessarily be cosntructed if any schemas in the file are used, the implication of listing
    # import IDs here is that if a schema file is used at all, all of its imports must be parsed,
    # just to get their IDs.  We'd much rather delay parsing a file until something inside it is
    # actually used.
    #
    # In any case, this import list's main reason for existing is to make it easy to generate
    # the appropriate #include statements in C++.  The IDs of files aren't needed for that.
    #
    # TODO(someday):  Perhaps provide an alternative way to identify the remote file.

    name @1 :Text;
    # Name which *this* file used to refer to the foreign file.  This may be a relative name.
    # This information is provided because it might be useful for code generation, e.g. to generate
    # #include directives in C++.
    #
    # (On Zooko's triangle, this is the import's petname according to the importing file.)
  }
}

enum ElementSize {
  # Possible element sizes for encoded lists.  These correspond exactly to the possible values of
  # the 3-bit element size component of a list pointer.

  empty @0;    # aka "void", but that's a keyword.
  bit @1;
  byte @2;
  twoBytes @3;
  fourBytes @4;
  eightBytes @5;
  pointer @6;
  inlineComposite @7;
}

struct StructNode {
  dataSectionWordSize @0 :UInt16;
  pointerSectionSize @1 :UInt16;

  preferredListEncoding @2 :ElementSize;
  # The preferred element size to use when encoding a list of this struct.  If this is anything
  # other than `inlineComposite` then the struct is one word or less in size and is a candidate for
  # list packing optimization.

  members @3 :List(Member);
  # Top-level fields and unions of the struct, ordered by ordinal number, except that members of
  # unions are not included in this list (because they are nested inside the union declaration).
  # Note that this ordering is stable as the protocol evolves -- new members can only be added to
  # the end.  So, when encoding a struct as tag/value pairs with numeric tags, it actually may make
  # sense to use the field's position in this list rather than the original ordinal number to
  # identify fields.

  struct Member {
    name @0 :Text;

    ordinal @1 :UInt16;
    # For fields, the ordinal number.  For unions, if an explicit ordinal was given, that number.
    # Otherwise, for unions and groups, this is the ordinal of the lowest-numbered field in the
    # union/group.
    #
    # TODO(someday):  When revamping the meta-schema, move this into Field.

    codeOrder @2 :UInt16;
    # Indicates where this member appeared in the code, relative to other members.
    # Code ordering may have semantic relevance -- programmers tend to place related fields
    # together.  So, using code ordering makes sense in human-readable formats where ordering is
    # otherwise irrelevant, like JSON.  The values of codeOrder are tightly-packed, so the maximum
    # value is count(members) - 1.  Fields that are members of a union are only ordered relative to
    # the other members of that union, so the maximum value there is count(union.members).

    annotations @3 :List(Annotation);

    body @4 union {
      # More member types could be added over time.  Consumers should skip those that they
      # don't understand.

      fieldMember @5 :Field;
      unionMember @6 :Union;
      groupMember @7 :Group;
    }
  }

  struct Field {
    offset @0 :UInt32;
    # Offset, in units of the field's size, from the beginning of the section in which the field
    # resides.  E.g. for a UInt32 field, multiply this by 4 to get the byte offset from the
    # beginning of the data section.

    type @1 :Type;
    defaultValue @2 :Value;
  }

  struct Union {
    discriminantOffset @0 :UInt32;
    # Offset of the union's 16-bit discriminant within the struct's data section, in 16-bit units.

    members @1 :List(Member);
    # Fields of this union, ordered by ordinal.  Currently all members are fields, but
    # consumers should skip member types that they don't understand.  The first member in this list
    # gets discriminant value zero, the next gets one, and so on.
  }

  struct Group {
    members @0 :List(Member);
  }
}

struct EnumNode {
  enumerants @0 :List(Enumerant);
  # Enumerants, in order by ordinal.

  struct Enumerant {
    name @0 :Text;

    codeOrder @1 :UInt16;
    # Specifies order in which the enumerants were declared in the code.
    # Like Struct.Field.codeOrder.

    annotations @2 :List(Annotation);
  }
}

struct InterfaceNode {
  methods @0 :List(Method);
  # Methods, in order by ordinal.

  struct Method {
    name @0 :Text;

    codeOrder @1 :UInt16;
    # Specifies order in which the methods were declared in the code.
    # Like Struct.Field.codeOrder.

    params @2 :List(Param);
    struct Param {
      name @0 :Text;
      type @1 :Type;
      defaultValue @2 :Value;
      annotations @3 :List(Annotation);
    }

    requiredParamCount @3 :UInt16;
    # One plus the index of the last parameter that has no default value.  In languages where
    # method calls look like function calls, this is the minimum number of parameters that must
    # always be specified, while subsequent parameters are optional.

    returnType @4 :Type;

    annotations @5 :List(Annotation);
  }
}

struct ConstNode {
  type @0 :Type;
  value @1 :Value;
}

struct AnnotationNode {
  type @0 :Type;

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

struct CodeGeneratorRequest {
  nodes @0 :List(Node);
  # All nodes parsed by the compiler, including for the files on the command line and their
  # imports.

  requestedFiles @1 :List(Id);
  # IDs of files which were listed on the command line.
}
