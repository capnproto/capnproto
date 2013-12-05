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

@0xa93fc509624c72d9;
$Cxx.namespace("capnp::schema");

using Id = UInt64;
# The globally-unique ID of a file, type, or annotation.

struct Node {
  id @0 :Id;

  displayName @1 :Text;
  # Name to present to humans to identify this Node.  You should not attempt to parse this.  Its
  # format could change.  It is not guaranteed to be unique.
  #
  # (On Zooko's triangle, this is the node's nickname.)

  displayNamePrefixLength @2 :UInt32;
  # If you want a shorter version of `displayName` (just naming this node, without its surrounding
  # scope), chop off this many characters from the beginning of `displayName`.

  scopeId @3 :Id;
  # ID of the lexical parent node.  Typically, the scope node will have a NestedNode pointing back
  # at this node, but robust code should avoid relying on this (and, in fact, group nodes are not
  # listed in the outer struct's nestedNodes, since they are listed in the fields).  `scopeId` is
  # zero if the node has no parent, which is normally only the case with files, but should be
  # allowed for any kind of node (in order to make runtime type generation easier).

  nestedNodes @4 :List(NestedNode);
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

  annotations @5 :List(Annotation);
  # Annotations applied to this node.

  union {
    # Info specific to each kind of node.

    file @6 :Void;

    struct :group {
      dataWordCount @7 :UInt16;
      # Size of the data section, in words.

      pointerCount @8 :UInt16;
      # Size of the pointer section, in pointers (which are one word each).

      preferredListEncoding @9 :ElementSize;
      # The preferred element size to use when encoding a list of this struct.  If this is anything
      # other than `inlineComposite` then the struct is one word or less in size and is a candidate
      # for list packing optimization.

      isGroup @10 :Bool;
      # If true, then this "struct" node is actually not an independent node, but merely represents
      # some named union or group within a particular parent struct.  This node's scopeId refers
      # to the parent struct, which may itself be a union/group in yet another struct.
      #
      # All group nodes share the same dataWordCount and pointerCount as the top-level
      # struct, and their fields live in the same ordinal and offset spaces as all other fields in
      # the struct.
      #
      # Note that a named union is considered a special kind of group -- in fact, a named union
      # is exactly equivalent to a group that contains nothing but an unnamed union.

      discriminantCount @11 :UInt16;
      # Number of fields in this struct which are members of an anonymous union, and thus may
      # overlap.  If this is non-zero, then a 16-bit discriminant is present indicating which
      # of the overlapping fields is active.  This can never be 1 -- if it is non-zero, it must be
      # two or more.
      #
      # Note that the fields of an unnamed union are considered fields of the scope containing the
      # union -- an unnamed union is not its own group.  So, a top-level struct may contain a
      # non-zero discriminant count.  Named unions, on the other hand, are equivalent to groups
      # containing unnamed unions.  So, a named union has its own independent schema node, with
      # `isGroup` = true.

      discriminantOffset @12 :UInt32;
      # If `discriminantCount` is non-zero, this is the offset of the union discriminant, in
      # multiples of 16 bits.

      fields @13 :List(Field);
      # Fields defined within this scope (either the struct's top-level fields, or the fields of
      # a particular group; see `isGroup`).
      #
      # The fields are sorted by ordinal number, but note that because groups share the same
      # ordinal space, the field's index in this list is not necessarily exactly its ordinal.
      # On the other hand, the field's position in this list does remain the same even as the
      # protocol evolves, since it is not possible to insert or remove an earlier ordinal.
      # Therefore, for most use cases, if you want to identify a field by number, it may make the
      # most sense to use the field's index in this list rather than its ordinal.
    }

    enum :group {
      enumerants@14 :List(Enumerant);
      # Enumerants ordered by numeric value (ordinal).
    }

    interface :group {
      methods @15 :List(Method);
      # Methods ordered by ordinal.

      extends @31 :List(Id);
      # Superclasses of this interface.
    }

    const :group {
      type @16 :Type;
      value @17 :Value;
    }

    annotation :group {
      type @18 :Type;

      targetsFile @19 :Bool;
      targetsConst @20 :Bool;
      targetsEnum @21 :Bool;
      targetsEnumerant @22 :Bool;
      targetsStruct @23 :Bool;
      targetsField @24 :Bool;
      targetsUnion @25 :Bool;
      targetsGroup @26 :Bool;
      targetsInterface @27 :Bool;
      targetsMethod @28 :Bool;
      targetsParam @29 :Bool;
      targetsAnnotation @30 :Bool;
    }
  }
}

struct Field {
  # Schema for a field of a struct.

  name @0 :Text;

  codeOrder @1 :UInt16;
  # Indicates where this member appeared in the code, relative to other members.
  # Code ordering may have semantic relevance -- programmers tend to place related fields
  # together.  So, using code ordering makes sense in human-readable formats where ordering is
  # otherwise irrelevant, like JSON.  The values of codeOrder are tightly-packed, so the maximum
  # value is count(members) - 1.  Fields that are members of a union are only ordered relative to
  # the other members of that union, so the maximum value there is count(union.members).

  annotations @2 :List(Annotation);

  const noDiscriminant :UInt16 = 0xffff;

  discriminantValue @3 :UInt16 = Field.noDiscriminant;
  # If the field is in a union, this is the value which the union's discriminant should take when
  # the field is active.  If the field is not in a union, this is 0xffff.

  union {
    slot :group {
      # A regular, non-group, non-fixed-list field.

      offset @4 :UInt32;
      # Offset, in units of the field's size, from the beginning of the section in which the field
      # resides.  E.g. for a UInt32 field, multiply this by 4 to get the byte offset from the
      # beginning of the data section.

      type @5 :Type;
      defaultValue @6 :Value;

      hadExplicitDefault @10 :Bool;
      # Whether the default value was specified explicitly.  Non-explicit default values are always
      # zero or empty values.  Usually, whether the default value was explicit shouldn't matter.
      # The main use case for this flag is for structs representing method parameters:
      # explicitly-defaulted parameters may be allowed to be omitted when calling the method.
    }

    group :group {
      # A group.

      typeId @7 :Id;
      # The ID of the group's node.
    }
  }

  ordinal :union {
    implicit @8 :Void;
    explicit @9 :UInt16;
    # The original ordinal number given to the field.  You probably should NOT use this; if you need
    # a numeric identifier for a field, use its position within the field array for its scope.
    # The ordinal is given here mainly just so that the original schema text can be reproduced given
    # the compiled version -- i.e. so that `capnp compile -ocapnp` can do its job.
  }
}

struct Enumerant {
  # Schema for member of an enum.

  name @0 :Text;

  codeOrder @1 :UInt16;
  # Specifies order in which the enumerants were declared in the code.
  # Like Struct.Field.codeOrder.

  annotations @2 :List(Annotation);
}

struct Method {
  # Schema for method of an interface.

  name @0 :Text;

  codeOrder @1 :UInt16;
  # Specifies order in which the methods were declared in the code.
  # Like Struct.Field.codeOrder.

  paramStructType @2 :Id;
  # ID of the parameter struct type.  If a named parameter list was specified in the method
  # declaration (rather than a single struct parameter type) then a corresponding struct type is
  # auto-generated.  Such an auto-generated type will not be listed in the interface's
  # `nestedNodes` and its `scopeId` will be zero -- it is completely detached from the namespace.

  resultStructType @3 :Id;
  # ID of the return struct type; similar to `paramStructType`.

  annotations @4 :List(Annotation);
}

struct Type {
  # Represents a type expression.

  union {
    # The ordinals intentionally match those of Value.

    void @0 :Void;
    bool @1 :Void;
    int8 @2 :Void;
    int16 @3 :Void;
    int32 @4 :Void;
    int64 @5 :Void;
    uint8 @6 :Void;
    uint16 @7 :Void;
    uint32 @8 :Void;
    uint64 @9 :Void;
    float32 @10 :Void;
    float64 @11 :Void;
    text @12 :Void;
    data @13 :Void;

    list :group {
      elementType @14 :Type;
    }

    enum :group {
      typeId @15 :Id;
    }
    struct :group {
      typeId @16 :Id;
    }
    interface :group {
      typeId @17 :Id;
    }

    anyPointer @18 :Void;
  }
}

struct Value {
  # Represents a value, e.g. a field default value, constant value, or annotation value.

  union {
    # The ordinals intentionally match those of Type.

    void @0 :Void;
    bool @1 :Bool;
    int8 @2 :Int8;
    int16 @3 :Int16;
    int32 @4 :Int32;
    int64 @5 :Int64;
    uint8 @6 :UInt8;
    uint16 @7 :UInt16;
    uint32 @8 :UInt32;
    uint64 @9 :UInt64;
    float32 @10 :Float32;
    float64 @11 :Float64;
    text @12 :Text;
    data @13 :Data;

    list @14 :AnyPointer;

    enum @15 :UInt16;
    struct @16 :AnyPointer;

    interface @17 :Void;
    # The only interface value that can be represented statically is "null", whose methods always
    # throw exceptions.

    anyPointer @18 :AnyPointer;
  }
}

struct Annotation {
  # Describes an annotation applied to a declaration.  Note AnnotationNode describes the
  # annotation's declaration, while this describes a use of the annotation.

  id @0 :Id;
  # ID of the annotation node.

  value @1 :Value;
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

struct CodeGeneratorRequest {
  nodes @0 :List(Node);
  # All nodes parsed by the compiler, including for the files on the command line and their
  # imports.

  requestedFiles @1 :List(RequestedFile);
  # Files which were listed on the command line.

  struct RequestedFile {
    id @0 :Id;
    # ID of the file.

    filename @1 :Text;
    # Name of the file as it appeared on the command-line (minus the src-prefix).  You may use
    # this to decide where to write the output.

    imports @2 :List(Import);
    # List of all imported paths seen in this file.

    struct Import {
      id @0 :Id;
      # ID of the imported file.

      name @1 :Text;
      # Name which *this* file used to refer to the foreign file.  This may be a relative name.
      # This information is provided because it might be useful for code generation, e.g. to
      # generate #include directives in C++.  We don't put this in Node.file because this
      # information is only meaningful at compile time anyway.
      #
      # (On Zooko's triangle, this is the import's petname according to the importing file.)
    }
  }
}
