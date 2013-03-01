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

namespace capnproto {
namespace internal {

struct ListDescriptor;
struct StructDescriptor;
struct FieldDescriptor;

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
  BIT,
  BYTE,
  TWO_BYTES,
  FOUR_BYTES,
  EIGHT_BYTES,

  REFERENCE,       // Indicates that the field lives in the reference segment, not the data segment.
  KEY_REFERENCE,   // A 64-bit key, 64-bit reference pair.

  STRUCT           // An arbitrary-sized inlined struct.  Used only for list elements, not struct
                   // fields, since a struct cannot embed another struct inline.
};

inline BitCount sizeInBits(FieldSize s) {
  static constexpr BitCount table[] = {1*BITS, 8*BITS, 16*BITS, 32*BITS, 64*BITS,
                                       64*BITS, 128*BITS, 0*BITS};
  return table[static_cast<int>(s)];
}

inline ByteCount byteOffsetForFieldZero(FieldSize s) {
  // For the given field size, get the offset, in bytes, between a struct pointer and the location
  // of the struct's first field, if the struct's first field is of the given type.  We use this
  // to adjust pointers when non-struct lists are converted to struct lists or vice versa.

  static constexpr ByteCount table[] = {1*BYTES, 1*BYTES, 2*BYTES, 4*BYTES, 8*BYTES,
                                        0*BYTES, 8*BYTES, 0*BYTES};
  return table[static_cast<int>(s)];
}

struct ListDescriptor {
  // Describes a list.

  Descriptor base;

  FieldSize elementSize;
  // Size of each element of the list.  Also determines whether it is a reference list or a data
  // list.

  const Descriptor* elementDescriptor;
  // For a reference list, this is a descriptor of an element.  Otherwise, NULL.

  uint32_t defaultCount;
  // Number of elements in the default value.

  const void* defaultData;
  // For a data list, points to an array of elements representing the default contents of the list.
  // Note that unlike data segments of structs, this pointer points to the first byte of the data.
  // For a reference list, points to an array of descriptor pointers -- use defaultReferences()
  // for type-safety.

  const Descriptor* const* defaultReferences() const {
    // Convenience accessor for reference lists.
    return reinterpret_cast<const Descriptor* const*>(defaultData);
  }
};
static_assert(__builtin_offsetof(ListDescriptor, base) == 0,
      "'base' must be the first member of ListDescriptor to allow reinterpret_cast from "
      "Descriptor to ListDescriptor.");

struct StructDescriptor {
  // Describes a struct.

  Descriptor base;

  uint8_t fieldCount;
  // Number of fields in this type -- that we were aware of at compile time, of course.

  WordCount8 dataSize;
  // Size of the data segment, in 64-bit words.
  // TODO:  Can we use WordCount here and still get static init?

  uint8_t referenceCount;
  // Number of references in the reference segment.

  const FieldDescriptor* fields;
  // Array of FieldDescriptors.

  const void* defaultData;
  // Default data.  The pointer actually points to the byte immediately after the end of the data.

  const Descriptor* const* defaultReferences;
  // Array of descriptors describing the references.

  inline WordCount wordSize() const {
    // Size of the struct in words.
    // TODO:  Somehow use WORDS_PER_REFERENCE here.
    return WordCount(dataSize) + referenceCount * WORDS;
  }
};
static_assert(__builtin_offsetof(StructDescriptor, base) == 0,
      "'base' must be the first member of StructDescriptor to allow reinterpret_cast from "
      "Descriptor to StructDescriptor.");

struct FieldDescriptor {
  // Describes one field of a struct.

  WordCount8 requiredDataSize;
  // The minimum size of the data segment of any object which includes this field.  This is always
  // offset * size / 64 bits, rounded up.  This value is useful for validating object references
  // received on the wire -- if dataSize is insufficient to support fieldCount, don't trust it!

  uint8_t requiredReferenceCount;
  // The minimum size of the reference segment of any object which includes this field.  Same deal
  // as with requiredDataSize.

  uint16_t offset;
  // If the field is a data field (size != REFERENCE), then this is the offset within the object's
  // data segment at which the field is positioned, measured in multiples of the field's size.  This
  // offset is intended to be *subtracted* from the object pointer, since the object pointer points
  // to the beginning of the reference segment, which is located immediately after the data segment.
  // Therefore, this offset can never be zero.
  //
  // For size == BIT, the meaning is slightly different:  bits are numbered from zero, starting with
  // the eight bits in the last byte of the data segment, followed by the eight bits in the byte
  // before that, and so on.  Within each byte, bits are numbered from least-significant to
  // most-significant -- i.e. *not* backwards.  This awkward numbering is necessary to allow a
  // list of booleans to be upgraded to a list of structs where field number zero is a boolean --
  // we need the first boolean in either a list or a struct to be located at the same end of its
  // byte.
  //
  // If the field is a reference field (size == REFERENCE), then this is the index within the
  // reference array at which the field is located.

  FieldSize size;
  // Size of this field.

  uint8_t hole32Offset;
  uint16_t hole16Offset;
  uint16_t hole8Offset;
  // In the case that this field is the last one in the object, and thus the object's data segment
  // size is equal to requiredDataSize, then the following offsets indicate locations of "holes" in
  // the data segment which are not occupied by any field.  The packing algorithm guarantees that
  // there can be at most one hole of each size.  An offset of zero indicates that no hole is
  // present.  Each offset is measured in multiples two times the hole size.  E.g. hole32Offset is
  // measured in 64-bit words.  (The packing algorithm guarantees that hole offsets will always be
  // an even multiple of the hole size.)

  uint16_t bitholeOffset;
  // If the object contains boolean fields and the number of booleans is not divisible by 8, then
  // there will also be a hole of 1-7 bits somewhere.  bitholeOffset is the offset, in bits, of the
  // first (most-significant) such missing bit.  All subsequent (less-significant) bits within the
  // same byte are also missing.
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
