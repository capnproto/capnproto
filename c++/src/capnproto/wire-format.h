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

#ifndef CAPNPROTO_WIRE_FORMAT_H_
#define CAPNPROTO_WIRE_FORMAT_H_

#include <inttypes.h>
#include "macros.h"

namespace capnproto {

class Arena;
class Segment;
class StructPtr;
class StructReadPtr;
class ListPtr;
class ListReadPtr;
class Capability;
struct WireReference;
struct WireHelpers;

namespace internal {
  class Descriptor;
  class StructDescriptor;
  class ListDescriptor;

  namespace debug {
    // These functions are only called inside debug asserts.  They are defined out-of-line so that
    // we don't have to #include descriptor.h from here.

    bool fieldIsStruct(const StructDescriptor* descriptor, int fieldNumber, int refIndex);
    bool fieldIsList(const StructDescriptor* descriptor, int fieldNumber, int refIndex);
    bool fieldIsData(const StructDescriptor* descriptor, int fieldNumber, int dataOffset,
                     int bitSize);
    bool dataFieldInRange(const StructDescriptor* descriptor, uint32_t dataOffset, uint32_t size);
    bool bitFieldInRange(const StructDescriptor* descriptor, uint32_t offset);
    bool refFieldIsStruct(const StructDescriptor* descriptor, int refIndex);
    bool refFieldIsList(const StructDescriptor* descriptor, int refIndex);
    bool elementsAreStructs(const ListDescriptor* descriptor);
    bool elementsAreStructs(const ListDescriptor* descriptor, uint32_t wordSize);
    bool elementsAreLists(const ListDescriptor* descriptor);
    bool elementsAreData(const ListDescriptor* descriptor, int bitSize);
  }  // namespace debug
}  // namespace internal

// -------------------------------------------------------------------

class StructPtr {
public:
  template <typename T>
  inline T getDataField(unsigned int offset) const;
  // Get the data field value of the given type at the given offset.  The offset is measured in
  // multiples of the field size, determined by the type.

  template <typename T>
  inline void setDataField(unsigned int offset, T value) const;
  // Set the data field value at the given offset.  Be careful to use the correct type.

  CAPNPROTO_ALWAYS_INLINE(StructPtr getStructField(int refIndex) const);
  // Get the struct field at the given index in the reference segment.  Allocates space for the
  // struct if necessary.

  CAPNPROTO_ALWAYS_INLINE(ListPtr initListField(int refIndex, uint32_t size) const);
  // Allocate a new list of the given size for the field at the given index in the reference
  // segment, and return a pointer to it.

  CAPNPROTO_ALWAYS_INLINE(ListPtr getListField(int refIndex) const);
  // Get the already-allocated list field for the given reference index.  Returns an empty list --
  // NOT necessarily the default value -- if the field is not initialized.

  StructReadPtr asReadPtr() const;
  // Get a StructReadPtr pointing at the same memory.

private:
  const internal::StructDescriptor* descriptor;  // Descriptor for the struct.
  Segment* segment;  // Memory segment in which the struct resides.
  void* ptr;  // Pointer to the location between the struct's data and reference segments.

  inline StructPtr(const internal::StructDescriptor* descriptor, Segment* segment, void* ptr)
      : descriptor(descriptor), segment(segment), ptr(ptr) {}

  StructPtr getStructFieldInternal(int refIndex) const;
  ListPtr initListFieldInternal(int refIndex, uint32_t size) const;
  ListPtr getListFieldInternal(int refIndex) const;
  // The public methods are inlined and simply wrap these "Internal" methods after doing debug
  // asserts.  This way, debugging is enabled by the caller's compiler flags rather than
  // libcapnproto's debug flags.

  friend class ListPtr;
  friend struct WireHelpers;
};

class StructReadPtr {
public:
  template <typename T>
  inline T getDataField(int fieldNumber, unsigned int offset) const;
  // Get the data field value of the given type at the given offset.  The offset is measured in
  // multiples of the field size, determined by the type.

  CAPNPROTO_ALWAYS_INLINE(
      StructReadPtr getStructField(int fieldNumber, unsigned int refIndex) const);
  // Get the struct field at the given index in the reference segment, or the default value if not
  // initialized.

  CAPNPROTO_ALWAYS_INLINE(ListReadPtr getListField(int fieldNumber, unsigned int refIndex) const);
  // Get the list field at the given index in the reference segment, or the default value if not
  // initialized.

private:
  const internal::StructDescriptor* descriptor;  // Descriptor for the struct.
  const Segment* segment;  // Memory segment in which the struct resides.

  const void* ptr[2];
  // ptr[0] points to the location between the struct's data and reference segments.
  // ptr[1] points to the end of the *default* data segment.
  // We put these in an array so we can choose between them without a branch.

  int fieldCount;  // Number of fields the struct is reported to have.

  int bit0Offset;
  // A special hack:  When accessing a boolean with field number zero, pretend its offset is this
  // instead of the usual zero.  This is needed to allow a boolean list to be upgraded to a list
  // of structs.

  int recursionLimit;
  // Limits the depth of message structures to guard against stack-overflow-based DoS attacks.
  // Once this reaches zero, further pointers will be pruned.

  inline StructReadPtr(const internal::StructDescriptor* descriptor, const Segment* segment,
                       const void* ptr, const void* defaultData, int fieldCount, int bit0Offset,
                       int recursionLimit)
      : descriptor(descriptor), segment(segment), ptr{ptr, defaultData}, fieldCount(fieldCount),
        bit0Offset(bit0Offset), recursionLimit(recursionLimit) {}

  StructReadPtr getStructFieldInternal(int fieldNumber, unsigned int refIndex) const;
  ListReadPtr getListFieldInternal(int fieldNumber, unsigned int refIndex) const;
  // The public methods are inlined and simply wrap these "Internal" methods after doing debug
  // asserts.  This way, debugging is enabled by the caller's compiler flags rather than
  // libcapnproto's debug flags.

  friend class ListReadPtr;
  friend class StructPtr;
  friend struct WireHelpers;
};

// -------------------------------------------------------------------

class ListPtr {
public:
  inline uint32_t size();
  // The number of elements in the list.

  template <typename T>
  CAPNPROTO_ALWAYS_INLINE(T getDataElement(unsigned int index) const);
  // Get the element of the given type at the given index.

  template <typename T>
  CAPNPROTO_ALWAYS_INLINE(void setDataElement(unsigned int index, T value) const);
  // Set the element at the given index.  Be careful to use the correct type.

  CAPNPROTO_ALWAYS_INLINE(
      StructPtr getStructElement(unsigned int index, uint32_t elementWordSize) const);
  // Get the struct element at the given index.  elementWordSize is the size, in 64-bit words, of
  // each element.

  CAPNPROTO_ALWAYS_INLINE(ListPtr initListElement(unsigned int index, uint32_t size) const);
  // Create a new list element of the given size at the given index.

  CAPNPROTO_ALWAYS_INLINE(ListPtr getListElement(unsigned int index) const);
  // Get the existing list element at the given index.

  ListReadPtr asReadPtr() const;
  // Get a ListReadPtr pointing at the same memory.

private:
  const internal::ListDescriptor* descriptor;  // Descriptor for the list.
  Segment* segment;  // Memory segment in which the list resides.
  void* ptr;  // Pointer to the beginning of the list.
  uint32_t elementCount;  // Number of elements in the list.

  inline ListPtr(const internal::ListDescriptor* descriptor, Segment* segment,
                 void* ptr, uint32_t size)
      : descriptor(descriptor), segment(segment), ptr(ptr), elementCount(size) {}

  StructPtr getStructElementInternal(unsigned int index, uint32_t elementWordSize) const;
  ListPtr initListElementInternal(unsigned int index, uint32_t size) const;
  ListPtr getListElementInternal(unsigned int index) const;
  // The public methods are inlined and simply wrap these "Internal" methods after doing debug
  // asserts.  This way, debugging is enabled by the caller's compiler flags rather than
  // libcapnproto's debug flags.

  friend class StructPtr;
  friend struct WireHelpers;
};

class ListReadPtr {
public:
  inline uint32_t size();
  // The number of elements in the list.

  template <typename T>
  CAPNPROTO_ALWAYS_INLINE(T getDataElement(unsigned int index) const);
  // Get the element of the given type at the given index.

  CAPNPROTO_ALWAYS_INLINE(StructReadPtr getStructElement(unsigned int index) const);
  // Get the struct element at the given index.

  CAPNPROTO_ALWAYS_INLINE(ListReadPtr getListElement(unsigned int index, uint32_t size) const);
  // Get the list element at the given index.

private:
  const internal::ListDescriptor* descriptor;  // Descriptor for the list.
  const Segment* segment;  // Memory segment in which the list resides.

  const void* ptr;
  // Pointer to the data.  If NULL, use defaultReferences.  (Never NULL for data lists.)

  uint32_t elementCount;  // Number of elements in the list.

  unsigned int stepBits;
  // The distance between elements, in bits.  This is usually the element size, but can be larger
  // if the sender upgraded a data list to a struct list.  It will always be aligned properly for
  // the type.  Unsigned so that division by a constant power of 2 is efficient.

  int structFieldCount;
  // If the elements are structs, the number of fields in each struct.

  int recursionLimit;
  // Limits the depth of message structures to guard against stack-overflow-based DoS attacks.
  // Once this reaches zero, further pointers will be pruned.

  inline ListReadPtr(const internal::ListDescriptor* descriptor, const Segment* segment,
                     const void* ptr, uint32_t size, int stepBits, int structFieldCount,
                     int recursionLimit)
      : descriptor(descriptor), segment(segment), ptr(ptr), elementCount(size), stepBits(stepBits),
        structFieldCount(structFieldCount), recursionLimit(recursionLimit) {}

  StructReadPtr getStructElementInternal(unsigned int index) const;
  ListReadPtr getListElementInternal(unsigned int index, uint32_t size) const;
  // The public methods are inlined and simply wrap these "Internal" methods after doing debug
  // asserts.  This way, debugging is enabled by the caller's compiler flags rather than
  // libcapnproto's debug flags.

  friend class StructReadPtr;
  friend class ListPtr;
  friend struct WireHelpers;
};

// =======================================================================================
// Internal implementation details...

template <typename T>
class WireValue {
  // Wraps a primitive value as it appears on the wire.  Namely, values are little-endian on the
  // wire, because little-endian is the most common endianness in modern CPUs.
  //
  // TODO:  On big-endian systems, inject byte-swapping here.  Most big-endian CPUs implement
  //   dedicated instructions for this, so use those rather than writing a bunch of shifts and
  //   masks.  Note that GCC has e.g. __builtin__bswap32() for this.
  //
  // Note:  In general, code that depends cares about byte ordering is bad.  See:
  //     http://commandcenter.blogspot.com/2012/04/byte-order-fallacy.html
  //   Cap'n Proto is special because it is essentially doing compiler-like things, fussing over
  //   allocation and layout of memory, in order to squeeze out every last drop of performance.

public:
  CAPNPROTO_ALWAYS_INLINE(WireValue()) {}
  CAPNPROTO_ALWAYS_INLINE(WireValue(T value)): value(value) {}

  CAPNPROTO_ALWAYS_INLINE(T get() const) { return value; }
  CAPNPROTO_ALWAYS_INLINE(void set(T newValue)) { value = newValue; }

private:
  T value;
};

// -------------------------------------------------------------------

template <typename T>
inline T StructPtr::getDataField(unsigned int offset) const {
  CAPNPROTO_DEBUG_ASSERT(
      internal::debug::dataFieldInRange(descriptor, offset, sizeof(T)),
      "StructPtr::getDataField() type mismatch.");

  return reinterpret_cast<WireValue<T>*>(ptr)[-offset].get();
}

template <>
inline bool StructPtr::getDataField<bool>(unsigned int offset) const {
  CAPNPROTO_DEBUG_ASSERT(
      internal::debug::bitFieldInRange(descriptor, offset),
      "StructPtr::getDataField<bool>() type mismatch.");
  uint8_t byte = *(reinterpret_cast<uint8_t*>(ptr) - (offset / 8) - 1);
  return (byte & (1 << (offset % 8))) != 0;
}

template <typename T>
inline void StructPtr::setDataField(unsigned int offset, T value) const {
  CAPNPROTO_DEBUG_ASSERT(
      internal::debug::dataFieldInRange(descriptor, offset, sizeof(T)),
      "StructPtr::setDataField() type mismatch.");
  reinterpret_cast<WireValue<T>*>(ptr)[-offset].set(value);
}

template <>
inline void StructPtr::setDataField<bool>(unsigned int offset, bool value) const {
  CAPNPROTO_DEBUG_ASSERT(
      internal::debug::bitFieldInRange(descriptor, offset),
      "StructPtr::setDataField<bool>() type mismatch.");
  uint8_t* byte = reinterpret_cast<uint8_t*>(ptr) - (offset / 8) - 1;
  *byte = (*byte & ~(1 << (offset % 8)))
        | (static_cast<uint8_t>(value) << (offset % 8));
}

inline StructPtr StructPtr::getStructField(int refIndex) const {
  CAPNPROTO_DEBUG_ASSERT(internal::debug::refFieldIsStruct(descriptor, refIndex),
      "StructPtr::getStructField() type mismatch.");
  return getStructFieldInternal(refIndex);
}

inline ListPtr StructPtr::initListField(int refIndex, uint32_t elementCount) const {
  CAPNPROTO_DEBUG_ASSERT(internal::debug::refFieldIsList(descriptor, refIndex),
      "StructPtr::initListField() type mismatch.");
  return initListFieldInternal(refIndex, elementCount);
}

inline ListPtr StructPtr::getListField(int refIndex) const {
  CAPNPROTO_DEBUG_ASSERT(internal::debug::refFieldIsList(descriptor, refIndex),
      "StructPtr::initListField() type mismatch.");
  return getListFieldInternal(refIndex);
}

// -------------------------------------------------------------------

template <typename T>
T StructReadPtr::getDataField(int fieldNumber, unsigned int offset) const {
  CAPNPROTO_DEBUG_ASSERT(
      internal::debug::fieldIsData(descriptor, fieldNumber, offset, sizeof(T) * 8),
      "StructReadPtr::getDataField() type mismatch.");
  const void* dataPtr = ptr[fieldNumber >= fieldCount];
  return reinterpret_cast<WireValue<T>*>(dataPtr)[-offset].get();
}

template <>
inline bool StructReadPtr::getDataField<bool>(int fieldNumber, unsigned int offset) const {
  CAPNPROTO_DEBUG_ASSERT(
      internal::debug::fieldIsData(descriptor, fieldNumber, offset, 1),
      "StructReadPtr::getDataField<bool>() type mismatch.");

  // This branch should always be optimized away when inlining.
  if (offset == 0) offset = bit0Offset;

  const void* dataPtr = ptr[fieldNumber >= fieldCount];
  uint8_t byte = *(reinterpret_cast<const uint8_t*>(dataPtr) - (offset / 8) - 1);
  return (byte & (1 << (offset % 8))) != 0;
}

inline StructReadPtr StructReadPtr::getStructField(int fieldNumber, unsigned int refIndex) const {
  CAPNPROTO_DEBUG_ASSERT(internal::debug::fieldIsStruct(descriptor, fieldNumber, refIndex),
      "StructReadPtr::getStructField() type mismatch.");
  return getStructFieldInternal(fieldNumber, refIndex);
}

inline ListReadPtr StructReadPtr::getListField(int fieldNumber, unsigned int refIndex) const {
  CAPNPROTO_DEBUG_ASSERT(internal::debug::fieldIsList(descriptor, fieldNumber, refIndex),
      "StructReadPtr::getListField() type mismatch.");
  return getListFieldInternal(fieldNumber, refIndex);
}

// -------------------------------------------------------------------

inline uint32_t ListPtr::size() { return elementCount; }

template <typename T>
inline T ListPtr::getDataElement(unsigned int index) const {
  CAPNPROTO_DEBUG_ASSERT(
      internal::debug::elementsAreData(descriptor, sizeof(T) * 8),
      "ListPtr::getDataElement() type mismatch.");
  return reinterpret_cast<WireValue<T>*>(ptr)[index].get();
}

template <>
inline bool ListPtr::getDataElement<bool>(unsigned int index) const {
  CAPNPROTO_DEBUG_ASSERT(
      internal::debug::elementsAreData(descriptor, 1),
      "ListPtr::getDataElement<bool>() type mismatch.");
  uint8_t byte = *(reinterpret_cast<uint8_t*>(ptr) + (index / 8));
  return (byte & (1 << (index % 8))) != 0;
}

template <typename T>
inline void ListPtr::setDataElement(unsigned int index, T value) const {
  CAPNPROTO_DEBUG_ASSERT(
      internal::debug::elementsAreData(descriptor, sizeof(T) * 8),
      "ListPtr::setDataElement() type mismatch.");
  reinterpret_cast<WireValue<T>*>(ptr)[index].set(value);
}

template <>
inline void ListPtr::setDataElement<bool>(unsigned int index, bool value) const {
  CAPNPROTO_DEBUG_ASSERT(
      internal::debug::elementsAreData(descriptor, 1),
      "ListPtr::setDataElement<bool>() type mismatch.");
  uint8_t* byte = reinterpret_cast<uint8_t*>(ptr) + (index / 8);
  *byte = (*byte & ~(1 << (index % 8)))
        | (static_cast<uint8_t>(value) << (index % 8));
}

inline StructPtr ListPtr::getStructElement(unsigned int index, uint32_t elementWordSize) const {
  CAPNPROTO_DEBUG_ASSERT(index < elementCount, "List index out of range.");
  CAPNPROTO_DEBUG_ASSERT(internal::debug::elementsAreStructs(descriptor, elementWordSize),
      "ListPtr::getStructElement() type mismatch.");
  return getStructElementInternal(index, elementWordSize);
}

inline ListPtr ListPtr::initListElement(unsigned int index, uint32_t size) const {
  CAPNPROTO_DEBUG_ASSERT(index < elementCount, "List index out of range.");
  CAPNPROTO_DEBUG_ASSERT(internal::debug::elementsAreLists(descriptor),
      "ListPtr::initListElement() type mismatch.");
  return initListElementInternal(index, size);
}

inline ListPtr ListPtr::getListElement(unsigned int index) const {
  CAPNPROTO_DEBUG_ASSERT(index < elementCount, "List index out of range.");
  CAPNPROTO_DEBUG_ASSERT(internal::debug::elementsAreLists(descriptor),
      "ListPtr::getListElement() type mismatch.");
  return getListElementInternal(index);
}

// -------------------------------------------------------------------

inline uint32_t ListReadPtr::size() { return elementCount; }

template <typename T>
inline T ListReadPtr::getDataElement(unsigned int index) const {
  CAPNPROTO_DEBUG_ASSERT(
      internal::debug::elementsAreData(descriptor, sizeof(T) * 8),
      "ListReadPtr::getDataElement() type mismatch.");
  return *reinterpret_cast<const T*>(
      reinterpret_cast<const uint8_t*>(ptr) + index * (stepBits / 8));
}

template <>
inline bool ListReadPtr::getDataElement<bool>(unsigned int index) const {
  CAPNPROTO_DEBUG_ASSERT(
      internal::debug::elementsAreData(descriptor, 1),
      "ListReadPtr::getDataElement<bool>() type mismatch.");
  unsigned int bitIndex = index * stepBits;
  uint8_t byte = *(reinterpret_cast<const uint8_t*>(ptr) + (bitIndex / 8));
  return (byte & (1 << (bitIndex % 8))) != 0;
}

inline StructReadPtr ListReadPtr::getStructElement(unsigned int index) const {
  CAPNPROTO_DEBUG_ASSERT(index < elementCount, "List index out of range.");
  CAPNPROTO_DEBUG_ASSERT(internal::debug::elementsAreStructs(descriptor),
      "ListReadPtr::getStructElement() type mismatch.");
  return getStructElementInternal(index);
}

inline ListReadPtr ListReadPtr::getListElement(unsigned int index, uint32_t size) const {
  CAPNPROTO_DEBUG_ASSERT(index < elementCount, "List index out of range.");
  CAPNPROTO_DEBUG_ASSERT(internal::debug::elementsAreLists(descriptor),
      "ListReadPtr::getListElement() type mismatch.");
  return getListElementInternal(index, size);
}

}  // namespace capnproto

#endif  // CAPNPROTO_WIRE_FORMAT_H_
