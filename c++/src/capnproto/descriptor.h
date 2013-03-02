// Copyright (c) 2013, Kenton Varda <temporal@gmail.com>
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

// This file is NOT intended for use by clients, except in generated code.
//
// The structures declared here provide basic information about user-defined capnproto data types
// needed in order to read, write, and traverse the raw message data.  Basically, descriptors
// serve two purposes:
// - Define the layout of struct types so that they can be allocated and so that incoming structs
//   can be validated.
// - Define the default values used when fields are absent on the wire.
//
// We use raw structs with no inheritance in order to allow for static initialization via the data
// segment with no need to execute any code at startup.

#ifndef CAPNPROTO_DESCRIPTOR_H_
#define CAPNPROTO_DESCRIPTOR_H_

#include <inttypes.h>
#include "macros.h"
#include "type-safety.h"

namespace capnproto {
namespace internal {

struct ListDescriptor;
struct StructDescriptor;
struct FieldDescriptor;
typedef Id<uint8_t, FieldDescriptor> FieldNumber;

template <int wordCount>
union AlignedData {
  // Useful for declaring static constant data blobs as an array of bytes, but forcing those
  // bytes to be word-aligned.

  uint8_t bytes[wordCount * sizeof(word)];
  word words[wordCount];
};

struct Descriptor {
  // This is the "base type" for descriptors that describe the target of a reference.
  // StructDescriptor and ListDescriptor should be treated as if they subclass this type.  However,
  // because subclassing breaks the ability to initialize structs using an initializer list -- which
  // we need for static initialization purposes -- we don't actually use inheritance.  Instead,
  // each of the "subclasses" has a field of type Descriptor as its first field.

  enum class Kind: uint8_t {
    LIST,     // This is a ListDescriptor
    STRUCT    // This is a StructDescriptor
  };

  Kind kind;

  inline const ListDescriptor* asList() const;
  inline const StructDescriptor* asStruct() const;
  // Poor-man's downcast.
};

enum class FieldSize: uint8_t {
  // TODO:  Rename to FieldLayout or maybe ValueLayout.

  BIT = 0,
  BYTE = 1,
  TWO_BYTES = 2,
  FOUR_BYTES = 3,
  EIGHT_BYTES = 4,

  REFERENCE = 5,  // Indicates that the field lives in the reference segment, not the data segment.
  KEY_REFERENCE = 6,  // A 64-bit key, 64-bit reference pair.  Valid only in lists.

  STRUCT = 7   // An arbitrary-sized inlined struct.  Used only for list elements, not struct
               // fields, since a struct cannot embed another struct inline.
};

typedef decltype(BITS / ELEMENTS) BitsPerElement;

namespace internal {
  static constexpr BitsPerElement BITS_PER_ELEMENT_TABLE[] = {
      1 * BITS / ELEMENTS,
      8 * BITS / ELEMENTS,
      16 * BITS / ELEMENTS,
      32 * BITS / ELEMENTS,
      64 * BITS / ELEMENTS,
      64 * BITS / ELEMENTS,
      128 * BITS / ELEMENTS,
      0 * BITS / ELEMENTS
  };
}

inline constexpr BitsPerElement bitsPerElement(FieldSize size) {
  return internal::BITS_PER_ELEMENT_TABLE[static_cast<int>(size)];
}

struct ListDescriptor {
  // Describes a list.

  Descriptor base;

  FieldSize elementSize;
  // Size of each element of the list.  Also determines whether it is a reference list or a data
  // list.

  ElementCount32 defaultCount;
  // Number of elements in the default value.

  const word* defaultValue;
  // Default content, parseable as a raw (trusted) message.

  const Descriptor* elementDescriptor;
  // For a reference list, this is a descriptor of an element.  Otherwise, NULL.
};
static_assert(CAPNPROTO_OFFSETOF(ListDescriptor, base) == 0,
      "'base' must be the first member of ListDescriptor to allow reinterpret_cast from "
      "Descriptor to ListDescriptor.");

struct StructDescriptor {
  // Describes a struct.

  Descriptor base;

  FieldNumber fieldCount;
  // Number of fields in this type -- that we were aware of at compile time, of course.

  WordCount8 dataSize;
  // Size of the data segment, in 64-bit words.

  WireReferenceCount8 referenceCount;
  // Number of references in the reference segment.

  const FieldDescriptor* fields;
  // Descriptors for fields, ordered by field number.

  const word* defaultValue;
  // Default content, parseable as a raw (trusted) message.  This pointer actually points at a
  // struct reference which in turn points at the struct content.

  WordCount wordSize() const {
    return WordCount(dataSize) + referenceCount * WORDS_PER_REFERENCE;
  }
};
static_assert(CAPNPROTO_OFFSETOF(StructDescriptor, base) == 0,
      "'base' must be the first member of StructDescriptor to allow reinterpret_cast from "
      "Descriptor to StructDescriptor.");

static constexpr uint16_t INVALID_FIELD_OFFSET = 0xffff;
static constexpr ByteCount16 FIELD_ITSELF_IS_UNION_TAG = 0xfffe * BYTES;

struct FieldDescriptor {
  // Describes one field of a struct.

  WordCount8 requiredDataSize;
  // The minimum size of the data segment of any object which includes this field.  This is always
  // offset * size / 64 bits, rounded up.  This value is useful for validating object references
  // received on the wire -- if dataSize is insufficient to support fieldCount, don't trust it!

  WireReferenceCount8 requiredReferenceCount;
  // The minimum size of the reference segment of any object which includes this field.  Same deal
  // as with requiredDataSize.

  FieldSize size;
  // Size of this field.

  ElementCount16 offset;
  // If the field is a data field (size != REFERENCE), then this is the offset within the object's
  // data segment at which the field is positioned, measured in multiples of the field's size.  Note
  // that for size == BIT, bits are considered to be in little-endian order.  Therefore, an offset
  // of zero refers to the least-significant bit of the first byte, 15 refers to the
  // most-significant bit of the second byte, etc.
  //
  // If the field is a reference field (size == REFERENCE), then this is the index within the
  // reference array at which the field is located.
  //
  // A value of INVALID_FIELD_OFFSET means that this is a void field.

  ByteCount16 unionTagOffset;
  // Offset within the data segment at which a union tag exists deciding whether this field is
  // valid.  If the tag byte does not contain this field's number, then this field is considered
  // to be unset.  An offset of INVALID_FIELD_OFFSET means the field is not a member of a union.
  // An offset of FIELD_ITSELF_IS_UNION_TAG means that this field itself is actually a union tag.

  uint16_t hole32Offset;
  uint16_t hole16Offset;
  ByteCount16 hole8Offset;
  // In the case that this field is the last one in the object, and thus the object's data segment
  // size is equal to requiredDataSize, then the following offsets indicate locations of "holes" in
  // the data segment which are not occupied by any field.  The packing algorithm guarantees that
  // there can be at most one hole of each size, and such holes will always be located at an offset
  // that is an odd multiple of the hole size.  The hole offsets here are given in multiples of the
  // hole size, with INVALID_FIELD_OFFSET meaning there is no hole.
  //
  // TODO:  Measurement types for hole16 and hole32?

  BitCount16 bitholeOffset;
  // If the object contains boolean fields and the number of booleans is not divisible by 8, then
  // there will also be a hole of 1-7 bits somewhere.  bitholeOffset is the offset, in bits, of the
  // first (least-significant) such missing bit.  All subsequent (more-significant) bits within the
  // same byte are also missing.  A value of INVALID_FIELD_OFFSET indicates that there is no hole.

  const Descriptor* descriptor;
  // If the field has a composite type, this is its descriptor.

  const void* defaultValue;
  // Pointer to the field's default value.  For reference fields, this should actually be a pointer
  // to a reference, which may be interpreted as a raw (trusted) message.  This pointer must be
  // aligned correctly for the value it points at.
  //
  // You might wonder why we can't just pull the default field value from the struct's defalut
  // value.  The problem is with unions -- at best, the struct default data only encodes one member
  // of each union.
};

inline const ListDescriptor* Descriptor::asList() const {
  CAPNPROTO_DEBUG_ASSERT(kind == Kind::LIST, "asList() called on Descriptor that isn't a list.");
  return reinterpret_cast<const ListDescriptor*>(this);
}
inline const StructDescriptor* Descriptor::asStruct() const {
  CAPNPROTO_DEBUG_ASSERT(
      kind == Kind::STRUCT, "asStruct() called on Descriptor that isn't a struct.");
  return reinterpret_cast<const StructDescriptor*>(this);
}

}  // namespace internal
}  // namespace capnproto

#endif  // CAPNPROTO_DESCRIPTOR_H_
