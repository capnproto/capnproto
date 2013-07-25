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

#include "node-translator.h"
#include <kj/debug.h>
#include <kj/arena.h>
#include <set>
#include <map>
#include <limits.h>

namespace capnp {
namespace compiler {

class NodeTranslator::StructLayout {
  // Massive, disgusting class which implements the layout algorithm, which decides the offset
  // for each field.

public:
  template <typename UIntType>
  struct HoleSet {
    inline HoleSet() {}

    // Represents a set of "holes" within a segment of allocated space, up to one hole of each
    // power-of-two size between 1 bit and 32 bits.
    //
    // The amount of "used" space in a struct's data segment can always be represented as a
    // combination of a word count and a HoleSet.  The HoleSet represents the space lost to
    // "padding".
    //
    // There can never be more than one hole of any particular size.  Why is this?  Well, consider
    // that every data field has a power-of-two size, every field must be aligned to a multiple of
    // its size, and the maximum size of a single field is 64 bits.  If we need to add a new field
    // of N bits, there are two possibilities:
    // 1. A hole of size N or larger exists.  In this case, we find the smallest hole that is at
    //    least N bits.  Let's say that that hole has size M.  We allocate the first N bits of the
    //    hole to the new field.  The remaining M - N bits become a series of holes of sizes N*2,
    //    N*4, ..., M / 2.  We know no holes of these sizes existed before because we chose M to be
    //    the smallest available hole larger than N.  So, there is still no more than one hole of
    //    each size, and no hole larger than any hole that existed previously.
    // 2. No hole equal or larger N exists.  In that case we extend the data section's size by one
    //    word, creating a new 64-bit hole at the end.  We then allocate N bits from it, creating
    //    a series of holes between N and 64 bits, as described in point (1).  Thus, again, there
    //    is still at most one hole of each size, and the largest hole is 32 bits.

    UIntType holes[6] = {0, 0, 0, 0, 0, 0};
    // The offset of each hole as a multiple of its size.  A value of zero indicates that no hole
    // exists.  Notice that it is impossible for any actual hole to have an offset of zero, because
    // the first field allocated is always placed at the very beginning of the section.  So either
    // the section has a size of zero (in which case there are no holes), or offset zero is
    // already allocated and therefore cannot be a hole.

    kj::Maybe<UIntType> tryAllocate(UIntType lgSize) {
      // Try to find space for a field of size lgSize^2 within the set of holes.  If found,
      // remove it from the holes, and return its offset (as a multiple of its size).  If there
      // is no such space, returns zero (no hole can be at offset zero, as explained above).

      if (holes[lgSize] != 0) {
        UIntType result = holes[lgSize];
        holes[lgSize] = 0;
        return result;
      } else {
        KJ_IF_MAYBE(next, tryAllocate(lgSize + 1)) {
          UIntType result = *next * 2;
          holes[lgSize] = result + 1;
          return result;
        } else {
          return nullptr;
        }
      }
    }

    uint assertHoleAndAllocate(UIntType lgSize) {
      KJ_ASSERT(holes[lgSize] != 0);
      uint result = holes[lgSize];
      holes[lgSize] = 0;
      return result;
    }

    void addHolesAtEnd(UIntType lgSize, UIntType offset,
                       UIntType limitLgSize = KJ_ARRAY_SIZE(holes)) {
      // Add new holes of progressively larger sizes in the range [lgSize, limitLgSize) starting
      // from the given offset.  The idea is that you just allocated an lgSize-sized field from
      // an limitLgSize-sized space, such as a newly-added word on the end of the data segment.

      KJ_DREQUIRE(limitLgSize <= KJ_ARRAY_SIZE(holes));

      while (lgSize < limitLgSize) {
        KJ_DREQUIRE(holes[lgSize] == 0);
        KJ_DREQUIRE(offset % 2 == 1);
        holes[lgSize] = offset;
        ++lgSize;
        offset = (offset + 1) / 2;
      }
    }

    bool tryExpand(UIntType oldLgSize, uint oldOffset, uint expansionFactor) {
      // Try to expand the value at the given location by combining it with subsequent holes, so
      // as to expand the location to be 2^expansionFactor times the size that it started as.
      // (In other words, the new lgSize is oldLgSize + expansionFactor.)

      if (expansionFactor == 0) {
        // No expansion requested.
        return true;
      }
      if (holes[oldLgSize] != oldOffset + 1) {
        // The space immediately after the location is not a hole.
        return false;
      }

      // We can expand the location by one factor by combining it with a hole.  Try to further
      // expand from there to the number of factors requested.
      if (tryExpand(oldLgSize + 1, oldOffset >> 1, expansionFactor - 1)) {
        // Success.  Consume the hole.
        holes[oldLgSize] = 0;
        return true;
      } else {
        return false;
      }
    }

    kj::Maybe<int> smallestAtLeast(uint size) {
      // Return the size of the smallest hole that is equal to or larger than the given size.

      for (uint i = size; i < KJ_ARRAY_SIZE(holes); i++) {
        if (holes[i] != 0) {
          return i;
        }
      }
      return nullptr;
    }
  };

  struct StructOrGroup {
    // Abstract interface for scopes in which fields can be added.

    virtual uint addData(uint lgSize) = 0;
    virtual uint addPointer() = 0;
    virtual bool tryExpandData(uint oldLgSize, uint oldOffset, uint expansionFactor) = 0;
    // Try to expand the given previously-allocated space by 2^expansionFactor.  Succeeds --
    // returning true -- if the following space happens to be empty, making this expansion possible.
    // Otherwise, returns false.
  };

  struct Top: public StructOrGroup {
    uint dataWordCount = 0;
    uint pointerCount = 0;
    // Size of the struct so far.

    HoleSet<uint> holes;

    uint addData(uint lgSize) override {
      KJ_IF_MAYBE(hole, holes.tryAllocate(lgSize)) {
        return *hole;
      } else {
        uint offset = dataWordCount++ << (6 - lgSize);
        holes.addHolesAtEnd(lgSize, offset + 1);
        return offset;
      }
    }

    uint addPointer() override {
      return pointerCount++;
    }

    bool tryExpandData(uint oldLgSize, uint oldOffset, uint expansionFactor) override {
      return holes.tryExpand(oldLgSize, oldOffset, expansionFactor);
    }

    Top() = default;
    KJ_DISALLOW_COPY(Top);
  };

  struct Union {
    struct DataLocation {
      uint lgSize;
      uint offset;

      bool tryExpandTo(Union& u, uint newLgSize) {
        if (newLgSize <= lgSize) {
          return true;
        } else if (u.parent.tryExpandData(lgSize, offset, newLgSize - lgSize)) {
          offset >>= (newLgSize - lgSize);
          lgSize = newLgSize;
          return true;
        } else {
          return false;
        }
      }
    };

    StructOrGroup& parent;
    uint groupCount = 0;
    kj::Maybe<int> discriminantOffset;
    kj::Vector<DataLocation> dataLocations;
    kj::Vector<uint> pointerLocations;

    inline Union(StructOrGroup& parent): parent(parent) {}
    KJ_DISALLOW_COPY(Union);

    uint addNewDataLocation(uint lgSize) {
      // Add a whole new data location to the union with the given size.

      uint offset = parent.addData(lgSize);
      dataLocations.add(DataLocation { lgSize, offset });
      return offset;
    }

    uint addNewPointerLocation() {
      // Add a whole new pointer location to the union with the given size.

      return pointerLocations.add(parent.addPointer());
    }

    void newGroup() {
      if (++groupCount == 2) {
        addDiscriminant();
      }
    }

    bool addDiscriminant() {
      if (discriminantOffset == nullptr) {
        discriminantOffset = parent.addData(4);  // 2^4 = 16 bits
        return true;
      } else {
        return false;
      }
    }
  };

  struct Group: public StructOrGroup {
  public:
    class DataLocationUsage {
    public:
      DataLocationUsage(): isUsed(false) {}
      explicit DataLocationUsage(uint lgSize): isUsed(true), lgSizeUsed(lgSize) {}

      kj::Maybe<uint> smallestHoleAtLeast(Union::DataLocation& location, uint lgSize) {
        // Find the smallest single hole that is at least the given size.  This is used to find the
        // optimal place to allocate each field -- it is placed in the smallest slot where it fits,
        // to reduce fragmentation.

        if (!isUsed) {
          // The location is effectively one big hole.
          return location.lgSize;
        } else if (lgSize >= lgSizeUsed) {
          // Requested size is at least our current usage, so clearly won't fit in any current
          // holes, but if the location's size is larger than what we're using, we'd be able to
          // expand.
          if (lgSize < location.lgSize) {
            return lgSize;
          } else {
            return nullptr;
          }
        } else KJ_IF_MAYBE(result, holes.smallestAtLeast(lgSize)) {
          // There's a hole.
          return *result;
        } else {
          // The requested size is smaller than what we're already using, but there are no holes
          // available.  If we could double our size, then we could allocate in the new space.

          if (lgSizeUsed < location.lgSize) {
            // We effectively create a new hole the same size as the current usage.
            return lgSizeUsed;
          } else {
            return nullptr;
          }
        }
      }

      uint allocateFromHole(Group& group, Union::DataLocation& location, uint lgSize) {
        // Allocate the given space from an existing hole, given smallestHoleAtLeast() already
        // returned non-null indicating such a hole exists.

        uint result;

        if (!isUsed) {
          // The location is totally unused, so just allocate from the beginning.
          KJ_DASSERT(lgSize <= location.lgSize, "Did smallestHoleAtLeast() really find a hole?");
          result = 0;
          isUsed = true;
          lgSizeUsed = lgSize;
        } else if (lgSize >= lgSizeUsed) {
          // Requested size is at least our current usage, so clearly won't fit in any holes.
          // We must expand to double the requested size, and return the second half.
          KJ_DASSERT(lgSize < location.lgSize, "Did smallestHoleAtLeast() really find a hole?");
          holes.addHolesAtEnd(lgSizeUsed, 1, lgSize);
          lgSizeUsed = lgSize + 1;
          result = 1;
        } else KJ_IF_MAYBE(hole, holes.tryAllocate(lgSize)) {
          // Found a hole.
          result = *hole;
        } else {
          // The requested size is smaller than what we're using so far, but didn't fit in a
          // hole.  We should double our "used" size, then allocate from the new space.
          KJ_DASSERT(lgSizeUsed < location.lgSize,
                     "Did smallestHoleAtLeast() really find a hole?");
          result = 1 << (lgSizeUsed - lgSize);
          holes.addHolesAtEnd(lgSize, result + 1, lgSizeUsed);
          lgSizeUsed += 1;
        }

        // Adjust the offset according to the location's offset before returning.
        uint locationOffset = location.offset << (location.lgSize - lgSize);
        return locationOffset + result;
      }

      kj::Maybe<uint> tryAllocateByExpanding(
          Group& group, Union::DataLocation& location, uint lgSize) {
        // Attempt to allocate the given size by requesting that the parent union expand this
        // location to fit.  This is used if smallestHoleAtLeast() already determined that there
        // are no holes that would fit, so we don't bother checking that.

        if (!isUsed) {
          if (location.tryExpandTo(group.parent, lgSize)) {
            isUsed = true;
            lgSizeUsed = lgSize;
            return kj::implicitCast<uint>(0);
          } else {
            return nullptr;
          }
        } else {
          uint newSize = kj::max(lgSizeUsed, lgSize) + 1;
          if (tryExpandUsage(group, location, newSize)) {
            return holes.assertHoleAndAllocate(lgSize);
          } else {
            return nullptr;
          }
        }
      }

      kj::Maybe<uint> tryAllocate(Group& group, Union::DataLocation& location, uint lgSize) {
        if (isUsed) {
          // We've already used some space in this location.  Try to allocate from a hole.
          uint result;
          KJ_IF_MAYBE(hole, holes.tryAllocate(lgSize)) {
            result = *hole;
          } else {
            // Failure.  But perhaps we could expand the location to include a new hole which would
            // be big enough for the value.
            uint neededSizeUsed;
            if (lgSize <= lgSizeUsed) {
              // We are already at least as big as the desired size, so doubling should be good
              // enough.  The new value will be located just past the end of our current used
              // space.
              neededSizeUsed = lgSizeUsed + 1;
              result = 1 << (lgSizeUsed - lgSize);
            } else {
              // We are smaller than the desired size, so we'll have to grow to 2x the desired size.
              // The new value will be at an offset of 1x its own size.
              neededSizeUsed = lgSize + 1;
              result = 1;
            }

            if (!tryExpandUsage(group, location, neededSizeUsed)) {
              return nullptr;
            }
            holes.addHolesAtEnd(lgSize, result + 1, neededSizeUsed - 1);
          }

          // OK, we found space.  Adjust the offset according to the location's offset before
          // returning.
          uint locationOffset = location.offset << (location.lgSize - lgSize);
          return locationOffset + result;

        } else {
          // We haven't used this location at all yet.

          if (location.lgSize < lgSize) {
            // Not enough space.  Try to expand the location.
            if (!location.tryExpandTo(group.parent, lgSize)) {
              // Couldn't expand.  This location is not viable.
              return nullptr;
            }
          }

          // Either the location was already big enough, or we expanded it.
          KJ_DASSERT(location.lgSize >= lgSize);

          // Just mark the first part used for now.
          lgSizeUsed = lgSize;

          // Return the offset, adjusted to be appropriate for the size.
          return location.offset << (location.lgSize - lgSize);
        }
      }

      bool tryExpand(Group& group, Union::DataLocation& location,
                     uint oldLgSize, uint oldOffset, uint expansionFactor) {
        if (oldOffset == 0 && lgSizeUsed == oldLgSize) {
          // This location contains exactly the requested data, so just expand the whole thing.
          return tryExpandUsage(group, location, oldLgSize + expansionFactor);
        } else {
          // This location contains the requested data plus other stuff.  Therefore the data cannot
          // possibly expand past the end of the space we've already marked used without either
          // overlapping with something else or breaking alignment rules.  We only have to combine
          // it with holes.
          return holes.tryExpand(oldLgSize, oldOffset, expansionFactor);
        }
      }

    private:
      bool isUsed;
      // Whether or not this location has been used at all by the group.

      uint8_t lgSizeUsed;
      // Amount of space from the location which is "used".  This is the minimum size needed to
      // cover all allocated space.  Only meaningful if `isUsed` is true.

      HoleSet<uint8_t> holes;
      // Indicates holes present in the space designated by `lgSizeUsed`.  The offsets in this
      // HoleSet are relative to the beginning of this particular data location, not the beginning
      // of the struct.

      bool tryExpandUsage(Group& group, Union::DataLocation& location, uint desiredUsage) {
        if (desiredUsage > location.lgSize) {
          // Need to expand the underlying slot.
          if (!location.tryExpandTo(group.parent, desiredUsage)) {
            return false;
          }
        }

        // Underlying slot is big enough, so expand our size and update holes.
        holes.addHolesAtEnd(lgSizeUsed, 1, desiredUsage);
        lgSizeUsed = desiredUsage;
        return true;
      }
    };

    Union& parent;

    kj::Vector<DataLocationUsage> parentDataLocationUsage;
    // Vector corresponding to the parent union's `dataLocations`, indicating how much of each
    // location has already been allocated.

    uint parentPointerLocationUsage = 0;
    // Number of parent's pointer locations that have been used by this group.

    inline Group(Union& parent): parent(parent) {
      parent.newGroup();
    }
    KJ_DISALLOW_COPY(Group);

    uint addData(uint lgSize) override {
      uint bestSize = UINT_MAX;
      kj::Maybe<uint> bestLocation = nullptr;

      for (uint i = 0; i < parent.dataLocations.size(); i++) {
        // If we haven't seen this DataLocation yet, add a corresponding DataLocationUsage.
        if (parentDataLocationUsage.size() == i) {
          parentDataLocationUsage.add();
        }

        auto& usage = parentDataLocationUsage[i];
        KJ_IF_MAYBE(hole, usage.smallestHoleAtLeast(parent.dataLocations[i], lgSize)) {
          if (*hole < bestSize) {
            bestSize = *hole;
            bestLocation = i;
          }
        }
      }

      KJ_IF_MAYBE(best, bestLocation) {
        return parentDataLocationUsage[*best].allocateFromHole(
            *this, parent.dataLocations[*best], lgSize);
      }

      // There are no holes at all in the union big enough to fit this field.  Go back through all
      // of the locations and attempt to expand them to fit.
      for (uint i = 0; i < parent.dataLocations.size(); i++) {
        KJ_IF_MAYBE(result, parentDataLocationUsage[i].tryAllocateByExpanding(
            *this, parent.dataLocations[i], lgSize)) {
          return *result;
        }
      }

      // Couldn't find any space in the existing locations, so add a new one.
      uint result = parent.addNewDataLocation(lgSize);
      parentDataLocationUsage.add(lgSize);
      return result;
    }

    uint addPointer() override {
      if (parentPointerLocationUsage < parent.pointerLocations.size()) {
        return parent.pointerLocations[parentPointerLocationUsage++];
      } else {
        parentPointerLocationUsage++;
        return parent.addNewPointerLocation();
      }
    }

    bool tryExpandData(uint oldLgSize, uint oldOffset, uint expansionFactor) override {
      if (oldLgSize + expansionFactor > 6 ||
          (oldOffset & ((1 << expansionFactor) - 1)) != 0) {
        // Expansion is not possible because the new size is too large or the offset is not
        // properly-aligned.
      }

      for (uint i = 0; i < parentDataLocationUsage.size(); i++) {
        auto& location = parent.dataLocations[i];
        if (location.lgSize >= oldLgSize &&
            oldOffset >> (location.lgSize - oldLgSize) == location.offset) {
          // The location we're trying to expand is a subset of this data location.
          auto& usage = parentDataLocationUsage[i];

          // Adjust the offset to be only within this location.
          uint localOldOffset = oldOffset - (location.offset << (location.lgSize - oldLgSize));

          // Try to expand.
          return usage.tryExpand(*this, location, oldLgSize, localOldOffset, expansionFactor);
        }
      }

      KJ_FAIL_ASSERT("Tried to expand field that was never allocated.");
      return false;
    }
  };

  Top& getTop();

private:
  Top top;
};

// =======================================================================================

NodeTranslator::NodeTranslator(
    const Resolver& resolver, const ErrorReporter& errorReporter,
    const Declaration::Reader& decl, Orphan<schema::Node> wipNodeParam)
    : resolver(resolver), errorReporter(errorReporter),
      wipNode(kj::mv(wipNodeParam)) {
  compileNode(decl, wipNode.get());
}

schema::Node::Reader NodeTranslator::finish() {
  // Careful about iteration here:  compileFinalValue() may actually add more elements to
  // `unfinishedValues`, invalidating iterators in the process.
  for (size_t i = 0; i < unfinishedValues.size(); i++) {
    auto& value = unfinishedValues[i];
    compileFinalValue(value.source, value.type, value.target);
  }

  return wipNode.getReader();
}

void NodeTranslator::compileNode(Declaration::Reader decl, schema::Node::Builder builder) {
  checkMembers(decl.getNestedDecls(), decl.getBody().which());

  switch (decl.getBody().which()) {
    case Declaration::Body::FILE_DECL:
      compileFile(decl, builder.getBody().initFileNode());
      break;
    case Declaration::Body::CONST_DECL:
      compileConst(decl.getBody().getConstDecl(), builder.getBody().initConstNode());
      break;
    case Declaration::Body::ANNOTATION_DECL:
      compileAnnotation(decl.getBody().getAnnotationDecl(), builder.getBody().initAnnotationNode());
      break;
    case Declaration::Body::ENUM_DECL:
      compileEnum(decl.getBody().getEnumDecl(), decl.getNestedDecls(),
                  builder.getBody().initEnumNode());
      break;
    case Declaration::Body::STRUCT_DECL:
      compileStruct(decl.getBody().getStructDecl(), decl.getNestedDecls(),
                    builder.getBody().initStructNode());
      break;
    case Declaration::Body::INTERFACE_DECL:
      compileInterface(decl.getBody().getInterfaceDecl(), decl.getNestedDecls(),
                       builder.getBody().initInterfaceNode());
      break;

    default:
      KJ_FAIL_REQUIRE("This Declaration is not a node.");
      break;
  }

  // TODO(now): annotations
}

void NodeTranslator::checkMembers(
    List<Declaration>::Reader nestedDecls, Declaration::Body::Which parentKind) {
  std::map<uint, Declaration::Reader> ordinals;
  std::map<kj::StringPtr, LocatedText::Reader> names;

  for (auto decl: nestedDecls) {
    {
      auto name = decl.getName();
      auto nameText = name.getValue();
      auto insertResult = names.insert(std::make_pair(nameText, name));
      if (!insertResult.second) {
        errorReporter.addErrorOn(
            name, kj::str("'", nameText, "' is already defined in this scope."));
        errorReporter.addErrorOn(
            insertResult.first->second, kj::str("'", nameText, "' previously defined here."));
      }
    }

    switch (decl.getBody().which()) {
      case Declaration::Body::USING_DECL:
      case Declaration::Body::CONST_DECL:
      case Declaration::Body::ENUM_DECL:
      case Declaration::Body::STRUCT_DECL:
      case Declaration::Body::INTERFACE_DECL:
      case Declaration::Body::ANNOTATION_DECL:
        switch (parentKind) {
          case Declaration::Body::FILE_DECL:
          case Declaration::Body::STRUCT_DECL:
          case Declaration::Body::INTERFACE_DECL:
            // OK.
            break;
          default:
            errorReporter.addErrorOn(decl, "This kind of declaration doesn't belong here.");
            break;
        }
        break;

      case Declaration::Body::ENUMERANT_DECL:
        if (parentKind != Declaration::Body::ENUM_DECL) {
          errorReporter.addErrorOn(decl, "Enumerants can only appear in enums.");
        }
        break;
      case Declaration::Body::METHOD_DECL:
        if (parentKind != Declaration::Body::INTERFACE_DECL) {
          errorReporter.addErrorOn(decl, "Methods can only appear in interfaces.");
        }
        break;
      case Declaration::Body::FIELD_DECL:
      case Declaration::Body::UNION_DECL:
      case Declaration::Body::GROUP_DECL:
        switch (parentKind) {
          case Declaration::Body::STRUCT_DECL:
          case Declaration::Body::UNION_DECL:
          case Declaration::Body::GROUP_DECL:
            // OK.
            break;
          default:
            errorReporter.addErrorOn(decl, "This declaration can only appear in structs.");
            break;
        }
        break;

      default:
        errorReporter.addErrorOn(decl, "This kind of declaration doesn't belong here.");
        break;
    }
  }
}

void NodeTranslator::disallowNested(List<Declaration>::Reader nestedDecls) {
  for (auto decl: nestedDecls) {
    errorReporter.addErrorOn(decl, "Nested declaration not allowed here.");
  }
}

static void findImports(DynamicValue::Reader value, std::set<kj::StringPtr>& output) {
  switch (value.getType()) {
    case DynamicValue::STRUCT: {
      auto structValue = value.as<DynamicStruct>();
      StructSchema schema = structValue.getSchema();

      if (schema == Schema::from<DeclName>()) {
        auto declName = structValue.as<DeclName>();
        if (declName.getBase().which() == DeclName::Base::IMPORT_NAME) {
          output.insert(declName.getBase().getImportName().getValue());
        }
      } else {
        for (auto member: schema.getMembers()) {
          if (structValue.has(member)) {
            findImports(structValue.get(member), output);
          }
        }
      }
      break;
    }

    case DynamicValue::LIST:
      for (auto element: value.as<DynamicList>()) {
        findImports(element, output);
      }
      break;

    default:
      break;
  }
}

void NodeTranslator::compileFile(Declaration::Reader decl, schema::FileNode::Builder builder) {
  std::set<kj::StringPtr> imports;
  findImports(decl, imports);

  auto list = builder.initImports(imports.size());
  auto iter = imports.begin();
  for (auto element: list) {
    element.setName(*iter++);
  }
  KJ_ASSERT(iter == imports.end());
}

void NodeTranslator::compileConst(Declaration::Const::Reader decl,
                                  schema::ConstNode::Builder builder) {
  auto typeBuilder = builder.initType();
  if (compileType(decl.getType(), typeBuilder)) {
    compileBootstrapValue(decl.getValue(), typeBuilder.asReader(), builder.initValue());
  }
}

void NodeTranslator::compileAnnotation(Declaration::Annotation::Reader decl,
                                       schema::AnnotationNode::Builder builder) {
  compileType(decl.getType(), builder.initType());

  // Dynamically copy over the values of all of the "targets" members.
  DynamicStruct::Reader src = decl;
  DynamicStruct::Builder dst = builder;
  for (auto srcMember: src.getSchema().getMembers()) {
    kj::StringPtr memberName = srcMember.getProto().getName();
    if (memberName.startsWith("targets")) {
      auto dstMember = dst.getSchema().getMemberByName(memberName);
      dst.set(dstMember, src.get(srcMember));
    }
  }
}

class NodeTranslator::DuplicateOrdinalDetector {
public:
  DuplicateOrdinalDetector(const ErrorReporter& errorReporter): errorReporter(errorReporter) {}

  void check(LocatedInteger::Reader ordinal) {
    if (ordinal.getValue() < expectedOrdinal) {
      errorReporter.addErrorOn(ordinal, "Duplicate ordinal number.");
      KJ_IF_MAYBE(last, lastOrdinalLocation) {
        errorReporter.addErrorOn(
            *last, kj::str("Ordinal @", last->getValue(), " originally used here."));
        // Don't report original again.
        lastOrdinalLocation = nullptr;
      }
    } else if (ordinal.getValue() > expectedOrdinal) {
      errorReporter.addErrorOn(ordinal,
          kj::str("Skipped ordinal @", expectedOrdinal, ".  Ordinals must be sequential with no "
                  "holes."));
    } else {
      ++expectedOrdinal;
      lastOrdinalLocation = ordinal;
    }
  }

private:
  const ErrorReporter& errorReporter;
  uint expectedOrdinal = 0;
  kj::Maybe<LocatedInteger::Reader> lastOrdinalLocation;
};

void NodeTranslator::compileEnum(Declaration::Enum::Reader decl,
                                 List<Declaration>::Reader members,
                                 schema::EnumNode::Builder builder) {
  // maps ordinal -> (code order, declaration)
  std::multimap<uint, std::pair<uint, Declaration::Reader>> enumerants;

  uint codeOrder = 0;
  for (auto member: members) {
    if (member.getBody().which() == Declaration::Body::ENUMERANT_DECL) {
      enumerants.insert(
          std::make_pair(member.getId().getOrdinal().getValue(),
                         std::make_pair(codeOrder++, member)));
    }
  }

  auto list = builder.initEnumerants(enumerants.size());
  uint i = 0;
  DuplicateOrdinalDetector dupDetector(errorReporter);

  for (auto& entry: enumerants) {
    uint codeOrder = entry.second.first;
    Declaration::Reader enumerantDecl = entry.second.second;

    dupDetector.check(enumerantDecl.getId().getOrdinal());

    auto enumerantBuilder = list[i];
    enumerantBuilder.setName(enumerantDecl.getName().getValue());
    enumerantBuilder.setCodeOrder(codeOrder);
    enumerantBuilder.adoptAnnotations(compileAnnotationApplications(
        enumerantDecl.getAnnotations(), "targetsEnumerant"));
  }
}

// -------------------------------------------------------------------

class NodeTranslator::StructTranslator {
public:
  explicit StructTranslator(NodeTranslator& translator)
      : translator(translator), errorReporter(translator.errorReporter) {}
  KJ_DISALLOW_COPY(StructTranslator);

  void translate(Declaration::Struct::Reader decl, List<Declaration>::Reader members,
                 schema::StructNode::Builder builder) {
    // Build the member-info-by-ordinal map.
    MemberInfo root(layout.getTop());
    traverseGroup(members, root);

    // Init the root.
    root.memberSchemas = builder.initMembers(root.childCount);

    // Go through each member in ordinal order, building each member schema.
    DuplicateOrdinalDetector dupDetector(errorReporter);
    for (auto& entry: membersByOrdinal) {
      MemberInfo& member = *entry.second;

      if (member.decl.getId().which() == Declaration::Id::ORDINAL) {
        dupDetector.check(member.decl.getId().getOrdinal());
      }

      schema::StructNode::Member::Builder builder = member.parent->getMemberSchema(member.index);

      builder.setName(member.decl.getName().getValue());
      builder.setOrdinal(entry.first);
      builder.setCodeOrder(member.codeOrder);

      kj::StringPtr targetsFlagName;

      switch (member.decl.getBody().which()) {
        case Declaration::Body::FIELD_DECL: {
          auto fieldReader = member.decl.getBody().getFieldDecl();
          auto fieldBuilder = builder.getBody().initFieldMember();
          auto typeBuilder = fieldBuilder.initType();
          if (translator.compileType(fieldReader.getType(), typeBuilder)) {
            switch (fieldReader.getDefaultValue().which()) {
              case Declaration::Field::DefaultValue::VALUE:
                translator.compileBootstrapValue(fieldReader.getDefaultValue().getValue(),
                                                 typeBuilder, fieldBuilder.initDefaultValue());
                break;
              case Declaration::Field::DefaultValue::NONE:
                translator.compileDefaultDefaultValue(typeBuilder, fieldBuilder.initDefaultValue());
                break;
            }

            int lgSize = -1;
            switch (typeBuilder.getBody().which()) {
              case schema::Type::Body::VOID_TYPE: lgSize = -1; break;
              case schema::Type::Body::BOOL_TYPE: lgSize = 0; break;
              case schema::Type::Body::INT8_TYPE: lgSize = 3; break;
              case schema::Type::Body::INT16_TYPE: lgSize = 4; break;
              case schema::Type::Body::INT32_TYPE: lgSize = 5; break;
              case schema::Type::Body::INT64_TYPE: lgSize = 6; break;
              case schema::Type::Body::UINT8_TYPE: lgSize = 3; break;
              case schema::Type::Body::UINT16_TYPE: lgSize = 4; break;
              case schema::Type::Body::UINT32_TYPE: lgSize = 5; break;
              case schema::Type::Body::UINT64_TYPE: lgSize = 6; break;
              case schema::Type::Body::FLOAT32_TYPE: lgSize = 5; break;
              case schema::Type::Body::FLOAT64_TYPE: lgSize = 6; break;

              case schema::Type::Body::TEXT_TYPE: lgSize = -2; break;
              case schema::Type::Body::DATA_TYPE: lgSize = -2; break;
              case schema::Type::Body::LIST_TYPE: lgSize = -2; break;
              case schema::Type::Body::ENUM_TYPE: lgSize = 4; break;
              case schema::Type::Body::STRUCT_TYPE: lgSize = -2; break;
              case schema::Type::Body::INTERFACE_TYPE: lgSize = -2; break;
              case schema::Type::Body::OBJECT_TYPE: lgSize = -2; break;
            }

            if (lgSize == -2) {
              // pointer
              fieldBuilder.setOffset(member.fieldScope->addPointer());
            } else if (lgSize == -1) {
              // void
              fieldBuilder.setOffset(0);
            } else {
              fieldBuilder.setOffset(member.fieldScope->addData(lgSize));
            }
          }

          targetsFlagName = "targetsField";
          break;
        }

        case Declaration::Body::UNION_DECL:
          if (member.decl.getId().which() == Declaration::Id::ORDINAL) {
            if (!member.unionScope->addDiscriminant()) {
              errorReporter.addErrorOn(member.decl.getId().getOrdinal(),
                  "Union ordinal, if specified, must be greater than no more than one of its "
                  "member ordinals (i.e. there can only be one field retroactively unionized).");
            }
          }
          lateUnions.add(&member);
          // No need to fill in members as this is done automatically elsewhere.
          targetsFlagName = "targetsUnion";
          break;

        case Declaration::Body::GROUP_DECL:
          // Nothing to do here; members are filled in automatically elsewhere.
          targetsFlagName = "targetsGroup";
          break;

        default:
          KJ_FAIL_ASSERT("Unexpected member type.");
          break;
      }

      builder.adoptAnnotations(translator.compileAnnotationApplications(
          member.decl.getAnnotations(), targetsFlagName));
    }

    // OK, all members are built.  The only thing left is the late unions.
    for (auto member: lateUnions) {
      member->unionScope->addDiscriminant();  // if it hasn't happened already
      KJ_IF_MAYBE(offset, member->unionScope->discriminantOffset) {
        member->parent->getMemberSchema(member->index).getBody().getUnionMember()
            .setDiscriminantOffset(*offset);
      } else {
        KJ_FAIL_ASSERT("addDiscriminant() didn't set the offset?");
      }
    }
  }

private:
  NodeTranslator& translator;
  const ErrorReporter& errorReporter;
  StructLayout layout;
  kj::Arena arena;

  struct MemberInfo {
    MemberInfo* parent;
    // The MemberInfo for the parent scope.

    uint codeOrder;
    // Code order within the parent.

    uint childCount = 0;
    // Number of children this member has.

    uint index = 0;

    Declaration::Reader decl;

    List<schema::StructNode::Member>::Builder memberSchemas;

    union {
      StructLayout::StructOrGroup* fieldScope;
      // If this member is a field, the scope of that field.  This will be used to assign an
      // offset for the field when going through in ordinal order.
      //
      // If the member is a group, this is is the group itself.

      StructLayout::Union* unionScope;
      // If this member is a union, this is the union.  This will be used to assign a discriminant
      // offset.
    };

    inline MemberInfo(StructLayout::Top& topScope)
        : parent(nullptr), codeOrder(0), fieldScope(&topScope) {}
    inline MemberInfo(MemberInfo& parent, uint codeOrder,
                      const Declaration::Reader& decl,
                      StructLayout::StructOrGroup& fieldScope)
        : parent(&parent), codeOrder(codeOrder), decl(decl), fieldScope(&fieldScope) {}
    inline MemberInfo(MemberInfo& parent, uint codeOrder,
                      const Declaration::Reader& decl, StructLayout::Union& unionScope)
        : parent(&parent), codeOrder(codeOrder), decl(decl), unionScope(&unionScope) {}

    schema::StructNode::Member::Builder getMemberSchema(uint childIndex) {
      // Get the schema builder for the child member at the given index.  This lazily/dynamically
      // builds the builder tree.

      KJ_REQUIRE(childIndex < childCount);

      if (memberSchemas.size() == 0) {
        switch (decl.getBody().which()) {
          case Declaration::Body::FIELD_DECL:
            KJ_FAIL_ASSERT("Fields don't have members.");
            break;
          case Declaration::Body::UNION_DECL:
            memberSchemas = parent->getMemberSchema(index).getBody()
                .initUnionMember().initMembers(childCount);
            break;
          case Declaration::Body::GROUP_DECL:
            memberSchemas = parent->getMemberSchema(index).getBody()
                .initGroupMember().initMembers(childCount);
            break;
          default:
            KJ_FAIL_ASSERT("Unexpected member type.");
            break;
        }
      }
      return memberSchemas[childIndex];
    }
  };

  std::multimap<uint, MemberInfo*> membersByOrdinal;
  // For fields, the key is the ordinal.  For unions and groups, the key is the lowest ordinal
  // number among their members, or the union's explicit ordinal number if it has one.

  kj::Vector<MemberInfo*> lateUnions;
  // Unions that need to have their discriminant offsets filled in after layout is complete.

  uint traverseUnion(List<Declaration>::Reader members, MemberInfo& parent) {
    uint minOrdinal = UINT_MAX;
    uint codeOrder = 0;

    if (members.size() < 2) {
      errorReporter.addErrorOn(parent.decl, "Union must have at least two members.");
    }

    for (auto member: members) {
      uint ordinal = 0;
      MemberInfo* memberInfo = nullptr;

      switch (member.getBody().which()) {
        case Declaration::Body::FIELD_DECL: {
          StructLayout::Group& singletonGroup =
              arena.allocate<StructLayout::Group>(*parent.unionScope);
          memberInfo = &arena.allocate<MemberInfo>(parent, codeOrder++, member, singletonGroup);
          ordinal = member.getId().getOrdinal().getValue();
          break;
        }

        case Declaration::Body::UNION_DECL:
          errorReporter.addErrorOn(member, "Unions cannot contain unions.");
          break;

        case Declaration::Body::GROUP_DECL: {
          StructLayout::Group& group =
              arena.allocate<StructLayout::Group>(*parent.unionScope);
          memberInfo = &arena.allocate<MemberInfo>(parent, codeOrder++, member, group);
          ordinal = traverseGroup(member.getNestedDecls(), *memberInfo);
          break;
        }

        default:
          // Ignore others.
          break;
      }

      if (memberInfo != nullptr) {
        memberInfo->index = parent.childCount++;
        membersByOrdinal.insert(std::make_pair(ordinal, memberInfo));
        minOrdinal = kj::min(minOrdinal, ordinal);
      }
    }

    return minOrdinal;
  }

  uint traverseGroup(List<Declaration>::Reader members, MemberInfo& parent) {
    uint minOrdinal = UINT_MAX;
    uint codeOrder = 0;

    if (members.size() < 2) {
      errorReporter.addErrorOn(parent.decl, "Group must have at least two members.");
    }

    for (auto member: members) {
      uint ordinal = 0;
      MemberInfo* memberInfo = nullptr;

      switch (member.getBody().which()) {
        case Declaration::Body::FIELD_DECL: {
          memberInfo = &arena.allocate<MemberInfo>(
              parent, codeOrder++, member, *parent.fieldScope);
          break;
        }

        case Declaration::Body::UNION_DECL: {
          StructLayout::Union& unionLayout = arena.allocate<StructLayout::Union>(
              *parent.fieldScope);
          memberInfo = &arena.allocate<MemberInfo>(
              parent, codeOrder++, member, unionLayout);
          ordinal = traverseUnion(member.getNestedDecls(), *memberInfo);
          if (member.getId().which() == Declaration::Id::ORDINAL) {
            ordinal = member.getId().getOrdinal().getValue();
          }
          break;
        }

        case Declaration::Body::GROUP_DECL:
          errorReporter.addErrorOn(member, "Groups should only appear inside unions.");
          break;

        default:
          // Ignore others.
          break;
      }

      if (memberInfo != nullptr) {
        memberInfo->index = parent.childCount++;
        membersByOrdinal.insert(std::make_pair(ordinal, memberInfo));
        minOrdinal = kj::min(minOrdinal, ordinal);
      }
    }

    return minOrdinal;
  }
};

void NodeTranslator::compileStruct(Declaration::Struct::Reader decl,
                                   List<Declaration>::Reader members,
                                   schema::StructNode::Builder builder) {

}

// -------------------------------------------------------------------

void NodeTranslator::compileInterface(Declaration::Interface::Reader decl,
                                      List<Declaration>::Reader members,
                                      schema::InterfaceNode::Builder builder) {

}

void NodeTranslator::compileBootstrapValue(ValueExpression::Reader source,
                                           schema::Type::Reader type,
                                           schema::Value::Builder target) {

}

void NodeTranslator::compileFinalValue(ValueExpression::Reader source,
                                       schema::Type::Reader type, schema::Value::Builder target) {

}

Orphan<List<schema::Annotation>> NodeTranslator::compileAnnotationApplications(
    List<Declaration::AnnotationApplication>::Reader annotations,
    kj::StringPtr targetsFlagName) {

}

}  // namespace compiler
}  // namespace capnp
