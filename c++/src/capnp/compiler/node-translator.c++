// Copyright (c) 2013-2014 Sandstorm Development Group, Inc. and contributors
// Licensed under the MIT License:
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#include "node-translator.h"
#include "parser.h"      // only for generateGroupId()
#include <kj/debug.h>
#include <kj/arena.h>
#include <set>
#include <map>

namespace capnp {
namespace compiler {

class NodeTranslator::StructLayout {
  // Massive, disgusting class which implements the layout algorithm, which decides the offset
  // for each field.

public:
  template <typename UIntType>
  struct HoleSet {
    inline HoleSet(): holes{0, 0, 0, 0, 0, 0} {}

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

    UIntType holes[6];
    // The offset of each hole as a multiple of its size.  A value of zero indicates that no hole
    // exists.  Notice that it is impossible for any actual hole to have an offset of zero, because
    // the first field allocated is always placed at the very beginning of the section.  So either
    // the section has a size of zero (in which case there are no holes), or offset zero is
    // already allocated and therefore cannot be a hole.

    kj::Maybe<UIntType> tryAllocate(UIntType lgSize) {
      // Try to find space for a field of size 2^lgSize within the set of holes.  If found,
      // remove it from the holes, and return its offset (as a multiple of its size).  If there
      // is no such space, returns zero (no hole can be at offset zero, as explained above).

      if (lgSize >= kj::size(holes)) {
        return nullptr;
      } else if (holes[lgSize] != 0) {
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
                       UIntType limitLgSize = sizeof(holes) / sizeof(holes[0])) {
      // Add new holes of progressively larger sizes in the range [lgSize, limitLgSize) starting
      // from the given offset.  The idea is that you just allocated an lgSize-sized field from
      // an limitLgSize-sized space, such as a newly-added word on the end of the data segment.

      KJ_DREQUIRE(limitLgSize <= kj::size(holes));

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

    kj::Maybe<uint> smallestAtLeast(uint size) {
      // Return the size of the smallest hole that is equal to or larger than the given size.

      for (uint i = size; i < kj::size(holes); i++) {
        if (holes[i] != 0) {
          return i;
        }
      }
      return nullptr;
    }

    uint getFirstWordUsed() {
      // Computes the lg of the amount of space used in the first word of the section.

      // If there is a 32-bit hole with a 32-bit offset, no more than the first 32 bits are used.
      // If no more than the first 32 bits are used, and there is a 16-bit hole with a 16-bit
      // offset, then no more than the first 16 bits are used.  And so on.
      for (uint i = kj::size(holes); i > 0; i--) {
        if (holes[i - 1] != 1) {
          return i;
        }
      }
      return 0;
    }
  };

  struct StructOrGroup {
    // Abstract interface for scopes in which fields can be added.

    virtual void addVoid() = 0;
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

    void addVoid() override {}

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
    kj::Maybe<uint> discriminantOffset;
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

    void newGroupAddingFirstMember() {
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
        // to reduce fragmentation.  Returns the size of the hole, if found.

        if (!isUsed) {
          // The location is effectively one big hole.
          if (lgSize <= location.lgSize) {
            return location.lgSize;
          } else {
            return nullptr;
          }
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
            return location.offset << (location.lgSize - lgSize);
          } else {
            return nullptr;
          }
        } else {
          uint newSize = kj::max(lgSizeUsed, lgSize) + 1;
          if (tryExpandUsage(group, location, newSize)) {
            uint result = KJ_ASSERT_NONNULL(holes.tryAllocate(lgSize));
            uint locationOffset = location.offset << (location.lgSize - lgSize);
            return locationOffset + result;
          } else {
            return nullptr;
          }
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

    bool hasMembers = false;

    inline Group(Union& parent): parent(parent) {}
    KJ_DISALLOW_COPY(Group);

    void addVoid() override {
      if (!hasMembers) {
        hasMembers = true;
        parent.newGroupAddingFirstMember();
      }
    }

    uint addData(uint lgSize) override {
      addVoid();

      uint bestSize = kj::maxValue;
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
      addVoid();

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

  Top& getTop() { return top; }

private:
  Top top;
};

// =======================================================================================

NodeTranslator::NodeTranslator(
    Resolver& resolver, ErrorReporter& errorReporter,
    const Declaration::Reader& decl, Orphan<schema::Node> wipNodeParam,
    bool compileAnnotations)
    : resolver(resolver), errorReporter(errorReporter),
      orphanage(Orphanage::getForMessageContaining(wipNodeParam.get())),
      compileAnnotations(compileAnnotations), wipNode(kj::mv(wipNodeParam)) {
  compileNode(decl, wipNode.get());
}

NodeTranslator::NodeSet NodeTranslator::getBootstrapNode() {
  auto nodeReader = wipNode.getReader();
  if (nodeReader.isInterface()) {
    return NodeSet {
      nodeReader,
      KJ_MAP(g, paramStructs) { return g.getReader(); }
    };
  } else {
    return NodeSet {
      nodeReader,
      KJ_MAP(g, groups) { return g.getReader(); }
    };
  }
}

NodeTranslator::NodeSet NodeTranslator::finish() {
  // Careful about iteration here:  compileFinalValue() may actually add more elements to
  // `unfinishedValues`, invalidating iterators in the process.
  for (size_t i = 0; i < unfinishedValues.size(); i++) {
    auto& value = unfinishedValues[i];
    compileValue(value.source, value.type, value.target, false);
  }

  return getBootstrapNode();
}

class NodeTranslator::DuplicateNameDetector {
public:
  inline explicit DuplicateNameDetector(ErrorReporter& errorReporter)
      : errorReporter(errorReporter) {}
  void check(List<Declaration>::Reader nestedDecls, Declaration::Which parentKind);

private:
  ErrorReporter& errorReporter;
  std::map<kj::StringPtr, LocatedText::Reader> names;
};

void NodeTranslator::compileNode(Declaration::Reader decl, schema::Node::Builder builder) {
  DuplicateNameDetector(errorReporter)
      .check(decl.getNestedDecls(), decl.which());

  kj::StringPtr targetsFlagName;

  switch (decl.which()) {
    case Declaration::FILE:
      targetsFlagName = "targetsFile";
      break;
    case Declaration::CONST:
      compileConst(decl.getConst(), builder.initConst());
      targetsFlagName = "targetsConst";
      break;
    case Declaration::ANNOTATION:
      compileAnnotation(decl.getAnnotation(), builder.initAnnotation());
      targetsFlagName = "targetsAnnotation";
      break;
    case Declaration::ENUM:
      compileEnum(decl.getEnum(), decl.getNestedDecls(), builder);
      targetsFlagName = "targetsEnum";
      break;
    case Declaration::STRUCT:
      compileStruct(decl.getStruct(), decl.getNestedDecls(), builder);
      targetsFlagName = "targetsStruct";
      break;
    case Declaration::INTERFACE:
      compileInterface(decl.getInterface(), decl.getNestedDecls(), builder);
      targetsFlagName = "targetsInterface";
      break;

    default:
      KJ_FAIL_REQUIRE("This Declaration is not a node.");
      break;
  }

  builder.adoptAnnotations(compileAnnotationApplications(decl.getAnnotations(), targetsFlagName));
}

void NodeTranslator::DuplicateNameDetector::check(
    List<Declaration>::Reader nestedDecls, Declaration::Which parentKind) {
  for (auto decl: nestedDecls) {
    {
      auto name = decl.getName();
      auto nameText = name.getValue();
      auto insertResult = names.insert(std::make_pair(nameText, name));
      if (!insertResult.second) {
        if (nameText.size() == 0 && decl.isUnion()) {
          errorReporter.addErrorOn(
              name, kj::str("An unnamed union is already defined in this scope."));
          errorReporter.addErrorOn(
              insertResult.first->second, kj::str("Previously defined here."));
        } else {
          errorReporter.addErrorOn(
              name, kj::str("'", nameText, "' is already defined in this scope."));
          errorReporter.addErrorOn(
              insertResult.first->second, kj::str("'", nameText, "' previously defined here."));
        }
      }

      switch (decl.which()) {
        case Declaration::USING:
        case Declaration::ENUM:
        case Declaration::STRUCT:
        case Declaration::INTERFACE:
          if (nameText.size() > 0 && (nameText[0] < 'A' || nameText[0] > 'Z')) {
            errorReporter.addErrorOn(name,
                "Type names must begin with a capital letter.");
          }
          break;

        case Declaration::CONST:
        case Declaration::ANNOTATION:
        case Declaration::ENUMERANT:
        case Declaration::METHOD:
        case Declaration::FIELD:
        case Declaration::UNION:
        case Declaration::GROUP:
          if (nameText.size() > 0 && (nameText[0] < 'a' || nameText[0] > 'z')) {
            errorReporter.addErrorOn(name,
                "Non-type names must begin with a lower-case letter.");
          }
          break;

        default:
          KJ_ASSERT(nameText.size() == 0, "Don't know what naming rules to enforce for node type.",
                    (uint)decl.which());
          break;
      }

      if (nameText.findFirst('_') != nullptr) {
        errorReporter.addErrorOn(name,
            "Cap'n Proto declaration names should use camelCase and must not contain "
            "underscores. (Code generators may convert names to the appropriate style for the "
            "target language.)");
      }
    }

    switch (decl.which()) {
      case Declaration::USING:
      case Declaration::CONST:
      case Declaration::ENUM:
      case Declaration::STRUCT:
      case Declaration::INTERFACE:
      case Declaration::ANNOTATION:
        switch (parentKind) {
          case Declaration::FILE:
          case Declaration::STRUCT:
          case Declaration::INTERFACE:
            // OK.
            break;
          default:
            errorReporter.addErrorOn(decl, "This kind of declaration doesn't belong here.");
            break;
        }
        break;

      case Declaration::ENUMERANT:
        if (parentKind != Declaration::ENUM) {
          errorReporter.addErrorOn(decl, "Enumerants can only appear in enums.");
        }
        break;
      case Declaration::METHOD:
        if (parentKind != Declaration::INTERFACE) {
          errorReporter.addErrorOn(decl, "Methods can only appear in interfaces.");
        }
        break;
      case Declaration::FIELD:
      case Declaration::UNION:
      case Declaration::GROUP:
        switch (parentKind) {
          case Declaration::STRUCT:
          case Declaration::UNION:
          case Declaration::GROUP:
            // OK.
            break;
          default:
            errorReporter.addErrorOn(decl, "This declaration can only appear in structs.");
            break;
        }

        // Struct members may have nested decls.  We need to check those here, because no one else
        // is going to do it.
        if (decl.getName().getValue().size() == 0) {
          // Unnamed union.  Check members as if they are in the same scope.
          check(decl.getNestedDecls(), decl.which());
        } else {
          // Children are in their own scope.
          DuplicateNameDetector(errorReporter)
              .check(decl.getNestedDecls(), decl.which());
        }

        break;

      default:
        errorReporter.addErrorOn(decl, "This kind of declaration doesn't belong here.");
        break;
    }
  }
}

void NodeTranslator::compileConst(Declaration::Const::Reader decl,
                                  schema::Node::Const::Builder builder) {
  auto typeBuilder = builder.initType();
  if (compileType(decl.getType(), typeBuilder)) {
    compileBootstrapValue(decl.getValue(), typeBuilder.asReader(), builder.initValue());
  }
}

void NodeTranslator::compileAnnotation(Declaration::Annotation::Reader decl,
                                       schema::Node::Annotation::Builder builder) {
  compileType(decl.getType(), builder.initType());

  // Dynamically copy over the values of all of the "targets" members.
  DynamicStruct::Reader src = decl;
  DynamicStruct::Builder dst = builder;
  for (auto srcField: src.getSchema().getFields()) {
    kj::StringPtr fieldName = srcField.getProto().getName();
    if (fieldName.startsWith("targets")) {
      auto dstField = dst.getSchema().getFieldByName(fieldName);
      dst.set(dstField, src.get(srcField));
    }
  }
}

class NodeTranslator::DuplicateOrdinalDetector {
public:
  DuplicateOrdinalDetector(ErrorReporter& errorReporter): errorReporter(errorReporter) {}

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
      expectedOrdinal = ordinal.getValue() + 1;
    } else {
      ++expectedOrdinal;
      lastOrdinalLocation = ordinal;
    }
  }

private:
  ErrorReporter& errorReporter;
  uint expectedOrdinal = 0;
  kj::Maybe<LocatedInteger::Reader> lastOrdinalLocation;
};

void NodeTranslator::compileEnum(Void decl,
                                 List<Declaration>::Reader members,
                                 schema::Node::Builder builder) {
  // maps ordinal -> (code order, declaration)
  std::multimap<uint, std::pair<uint, Declaration::Reader>> enumerants;

  uint codeOrder = 0;
  for (auto member: members) {
    if (member.isEnumerant()) {
      enumerants.insert(
          std::make_pair(member.getId().getOrdinal().getValue(),
                         std::make_pair(codeOrder++, member)));
    }
  }

  auto list = builder.initEnum().initEnumerants(enumerants.size());
  uint i = 0;
  DuplicateOrdinalDetector dupDetector(errorReporter);

  for (auto& entry: enumerants) {
    uint codeOrder = entry.second.first;
    Declaration::Reader enumerantDecl = entry.second.second;

    dupDetector.check(enumerantDecl.getId().getOrdinal());

    auto enumerantBuilder = list[i++];
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

  void translate(Void decl, List<Declaration>::Reader members, schema::Node::Builder builder) {
    // Build the member-info-by-ordinal map.
    MemberInfo root(builder);
    traverseTopOrGroup(members, root, layout.getTop());
    translateInternal(root, builder);
  }

  void translate(List<Declaration::Param>::Reader params, schema::Node::Builder builder) {
    // Build a struct from a method param / result list.
    MemberInfo root(builder);
    traverseParams(params, root, layout.getTop());
    translateInternal(root, builder);
  }

private:
  NodeTranslator& translator;
  ErrorReporter& errorReporter;
  StructLayout layout;
  kj::Arena arena;

  struct MemberInfo {
    MemberInfo* parent;
    // The MemberInfo for the parent scope.

    uint codeOrder;
    // Code order within the parent.

    uint index = 0;
    // Index within the parent.

    uint childCount = 0;
    // Number of children this member has.

    uint childInitializedCount = 0;
    // Number of children whose `schema` member has been initialized.  This initialization happens
    // while walking the fields in ordinal order.

    uint unionDiscriminantCount = 0;
    // Number of children who are members of the scope's union and have had their discriminant
    // value decided.

    bool isInUnion;
    // Whether or not this field is in the parent's union.

    kj::StringPtr name;
    Declaration::Id::Reader declId;
    Declaration::Which declKind;
    bool isParam = false;
    bool hasDefaultValue = false;               // if declKind == FIELD
    TypeExpression::Reader fieldType;           // if declKind == FIELD
    ValueExpression::Reader fieldDefaultValue;  // if declKind == FIELD && hasDefaultValue
    List<Declaration::AnnotationApplication>::Reader declAnnotations;
    uint startByte = 0;
    uint endByte = 0;
    // Information about the field declaration.  We don't use Declaration::Reader because it might
    // have come from a Declaration::Param instead.

    kj::Maybe<schema::Field::Builder> schema;
    // Schema for the field.  Initialized when getSchema() is first called.

    schema::Node::Builder node;
    // If it's a group, or the top-level struct.

    union {
      StructLayout::StructOrGroup* fieldScope;
      // If this member is a field, the scope of that field.  This will be used to assign an
      // offset for the field when going through in ordinal order.

      StructLayout::Union* unionScope;
      // If this member is a union, or it is a group or top-level struct containing an unnamed
      // union, this is the union.  This will be used to assign a discriminant offset when the
      // union's ordinal comes up (if the union has an explicit ordinal), as well as to finally
      // copy over the discriminant offset to the schema.
    };

    inline explicit MemberInfo(schema::Node::Builder node)
        : parent(nullptr), codeOrder(0), isInUnion(false), node(node), unionScope(nullptr) {}
    inline MemberInfo(MemberInfo& parent, uint codeOrder,
                      const Declaration::Reader& decl,
                      StructLayout::StructOrGroup& fieldScope,
                      bool isInUnion)
        : parent(&parent), codeOrder(codeOrder), isInUnion(isInUnion),
          name(decl.getName().getValue()), declId(decl.getId()), declKind(Declaration::FIELD),
          declAnnotations(decl.getAnnotations()),
          startByte(decl.getStartByte()), endByte(decl.getEndByte()),
          node(nullptr), fieldScope(&fieldScope) {
      KJ_REQUIRE(decl.which() == Declaration::FIELD);
      auto fieldDecl = decl.getField();
      fieldType = fieldDecl.getType();
      if (fieldDecl.getDefaultValue().isValue()) {
        hasDefaultValue = true;
        fieldDefaultValue = fieldDecl.getDefaultValue().getValue();
      }
    }
    inline MemberInfo(MemberInfo& parent, uint codeOrder,
                      const Declaration::Param::Reader& decl,
                      StructLayout::StructOrGroup& fieldScope,
                      bool isInUnion)
        : parent(&parent), codeOrder(codeOrder), isInUnion(isInUnion),
          name(decl.getName().getValue()), declKind(Declaration::FIELD), isParam(true),
          declAnnotations(decl.getAnnotations()),
          startByte(decl.getStartByte()), endByte(decl.getEndByte()),
          node(nullptr), fieldScope(&fieldScope) {
      fieldType = decl.getType();
      if (decl.getDefaultValue().isValue()) {
        hasDefaultValue = true;
        fieldDefaultValue = decl.getDefaultValue().getValue();
      }
    }
    inline MemberInfo(MemberInfo& parent, uint codeOrder,
                      const Declaration::Reader& decl,
                      schema::Node::Builder node,
                      bool isInUnion)
        : parent(&parent), codeOrder(codeOrder), isInUnion(isInUnion),
          name(decl.getName().getValue()), declId(decl.getId()), declKind(decl.which()),
          declAnnotations(decl.getAnnotations()),
          startByte(decl.getStartByte()), endByte(decl.getEndByte()),
          node(node), unionScope(nullptr) {
      KJ_REQUIRE(decl.which() != Declaration::FIELD);
    }

    schema::Field::Builder getSchema() {
      KJ_IF_MAYBE(result, schema) {
        return *result;
      } else {
        index = parent->childInitializedCount;
        auto builder = parent->addMemberSchema();
        if (isInUnion) {
          builder.setDiscriminantValue(parent->unionDiscriminantCount++);
        }
        builder.setName(name);
        builder.setCodeOrder(codeOrder);
        schema = builder;
        return builder;
      }
    }

    schema::Field::Builder addMemberSchema() {
      // Get the schema builder for the child member at the given index.  This lazily/dynamically
      // builds the builder tree.

      KJ_REQUIRE(childInitializedCount < childCount);

      auto structNode = node.getStruct();
      if (!structNode.hasFields()) {
        if (parent != nullptr) {
          getSchema();  // Make sure field exists in parent once the first child is added.
        }
        return structNode.initFields(childCount)[childInitializedCount++];
      } else {
        return structNode.getFields()[childInitializedCount++];
      }
    }

    void finishGroup() {
      if (unionScope != nullptr) {
        unionScope->addDiscriminant();  // if it hasn't happened already
        auto structNode = node.getStruct();
        structNode.setDiscriminantCount(unionDiscriminantCount);
        structNode.setDiscriminantOffset(KJ_ASSERT_NONNULL(unionScope->discriminantOffset));
      }

      if (parent != nullptr) {
        uint64_t groupId = generateGroupId(parent->node.getId(), index);
        node.setId(groupId);
        node.setScopeId(parent->node.getId());
        getSchema().initGroup().setTypeId(groupId);
      }
    }
  };

  std::multimap<uint, MemberInfo*> membersByOrdinal;
  // Every member that has an explicit ordinal goes into this map.  We then iterate over the map
  // to assign field offsets (or discriminant offsets for unions).

  kj::Vector<MemberInfo*> allMembers;
  // All members, including ones that don't have ordinals.

  void traverseUnion(const Declaration::Reader& decl,
                     List<Declaration>::Reader members, MemberInfo& parent,
                     StructLayout::Union& layout, uint& codeOrder) {
    if (members.size() < 2) {
      errorReporter.addErrorOn(decl, "Union must have at least two members.");
    }

    for (auto member: members) {
      kj::Maybe<uint> ordinal;
      MemberInfo* memberInfo = nullptr;

      switch (member.which()) {
        case Declaration::FIELD: {
          parent.childCount++;
          // For layout purposes, pretend this field is enclosed in a one-member group.
          StructLayout::Group& singletonGroup =
              arena.allocate<StructLayout::Group>(layout);
          memberInfo = &arena.allocate<MemberInfo>(parent, codeOrder++, member, singletonGroup,
                                                   true);
          allMembers.add(memberInfo);
          ordinal = member.getId().getOrdinal().getValue();
          break;
        }

        case Declaration::UNION:
          if (member.getName().getValue() == "") {
            errorReporter.addErrorOn(member, "Unions cannot contain unnamed unions.");
          } else {
            parent.childCount++;

            // For layout purposes, pretend this union is enclosed in a one-member group.
            StructLayout::Group& singletonGroup =
                arena.allocate<StructLayout::Group>(layout);
            StructLayout::Union& unionLayout = arena.allocate<StructLayout::Union>(singletonGroup);

            memberInfo = &arena.allocate<MemberInfo>(
                parent, codeOrder++, member,
                newGroupNode(parent.node, member.getName().getValue()),
                true);
            allMembers.add(memberInfo);
            memberInfo->unionScope = &unionLayout;
            uint subCodeOrder = 0;
            traverseUnion(member, member.getNestedDecls(), *memberInfo, unionLayout, subCodeOrder);
            if (member.getId().isOrdinal()) {
              ordinal = member.getId().getOrdinal().getValue();
            }
          }
          break;

        case Declaration::GROUP: {
          parent.childCount++;
          StructLayout::Group& group = arena.allocate<StructLayout::Group>(layout);
          memberInfo = &arena.allocate<MemberInfo>(
              parent, codeOrder++, member,
              newGroupNode(parent.node, member.getName().getValue()),
              true);
          allMembers.add(memberInfo);
          traverseGroup(member.getNestedDecls(), *memberInfo, group);
          break;
        }

        default:
          // Ignore others.
          break;
      }

      KJ_IF_MAYBE(o, ordinal) {
        membersByOrdinal.insert(std::make_pair(*o, memberInfo));
      }
    }
  }

  void traverseGroup(List<Declaration>::Reader members, MemberInfo& parent,
                     StructLayout::StructOrGroup& layout) {
    if (members.size() < 1) {
      errorReporter.addError(parent.startByte, parent.endByte,
                             "Group must have at least one member.");
    }

    traverseTopOrGroup(members, parent, layout);
  }

  void traverseTopOrGroup(List<Declaration>::Reader members, MemberInfo& parent,
                          StructLayout::StructOrGroup& layout) {
    uint codeOrder = 0;

    for (auto member: members) {
      kj::Maybe<uint> ordinal;
      MemberInfo* memberInfo = nullptr;

      switch (member.which()) {
        case Declaration::FIELD: {
          parent.childCount++;
          memberInfo = &arena.allocate<MemberInfo>(
              parent, codeOrder++, member, layout, false);
          allMembers.add(memberInfo);
          ordinal = member.getId().getOrdinal().getValue();
          break;
        }

        case Declaration::UNION: {
          StructLayout::Union& unionLayout = arena.allocate<StructLayout::Union>(layout);

          uint independentSubCodeOrder = 0;
          uint* subCodeOrder = &independentSubCodeOrder;
          if (member.getName().getValue() == "") {
            memberInfo = &parent;
            subCodeOrder = &codeOrder;
          } else {
            parent.childCount++;
            memberInfo = &arena.allocate<MemberInfo>(
                parent, codeOrder++, member,
                newGroupNode(parent.node, member.getName().getValue()),
                false);
            allMembers.add(memberInfo);
          }
          memberInfo->unionScope = &unionLayout;
          traverseUnion(member, member.getNestedDecls(), *memberInfo, unionLayout, *subCodeOrder);
          if (member.getId().isOrdinal()) {
            ordinal = member.getId().getOrdinal().getValue();
          }
          break;
        }

        case Declaration::GROUP:
          parent.childCount++;
          memberInfo = &arena.allocate<MemberInfo>(
              parent, codeOrder++, member,
              newGroupNode(parent.node, member.getName().getValue()),
              false);
          allMembers.add(memberInfo);

          // Members of the group are laid out just like they were members of the parent, so we
          // just pass along the parent layout.
          traverseGroup(member.getNestedDecls(), *memberInfo, layout);

          // No ordinal for groups.
          break;

        default:
          // Ignore others.
          break;
      }

      KJ_IF_MAYBE(o, ordinal) {
        membersByOrdinal.insert(std::make_pair(*o, memberInfo));
      }
    }
  }

  void traverseParams(List<Declaration::Param>::Reader params, MemberInfo& parent,
                      StructLayout::StructOrGroup& layout) {
    for (uint i: kj::indices(params)) {
      auto param = params[i];
      parent.childCount++;
      MemberInfo* memberInfo = &arena.allocate<MemberInfo>(parent, i, param, layout, false);
      allMembers.add(memberInfo);
      membersByOrdinal.insert(std::make_pair(i, memberInfo));
    }
  }

  schema::Node::Builder newGroupNode(schema::Node::Reader parent, kj::StringPtr name) {
    auto orphan = translator.orphanage.newOrphan<schema::Node>();
    auto node = orphan.get();

    // We'll set the ID and scope ID later.
    node.setDisplayName(kj::str(parent.getDisplayName(), '.', name));
    node.setDisplayNamePrefixLength(node.getDisplayName().size() - name.size());
    node.initStruct().setIsGroup(true);

    // The remaining contents of node.struct will be filled in later.

    translator.groups.add(kj::mv(orphan));
    return node;
  }

  void translateInternal(MemberInfo& root, schema::Node::Builder builder) {
    auto structBuilder = builder.initStruct();

    // Go through each member in ordinal order, building each member schema.
    DuplicateOrdinalDetector dupDetector(errorReporter);
    for (auto& entry: membersByOrdinal) {
      MemberInfo& member = *entry.second;

      if (member.declId.isOrdinal()) {
        dupDetector.check(member.declId.getOrdinal());
      }

      schema::Field::Builder fieldBuilder = member.getSchema();
      fieldBuilder.getOrdinal().setExplicit(entry.first);

      switch (member.declKind) {
        case Declaration::FIELD: {
          auto slot = fieldBuilder.initSlot();
          auto typeBuilder = slot.initType();
          if (translator.compileType(member.fieldType, typeBuilder)) {
            if (member.hasDefaultValue) {
              translator.compileBootstrapValue(member.fieldDefaultValue,
                                               typeBuilder, slot.initDefaultValue());
              slot.setHadExplicitDefault(true);
            } else {
              translator.compileDefaultDefaultValue(typeBuilder, slot.initDefaultValue());
            }
          } else {
            translator.compileDefaultDefaultValue(typeBuilder, slot.initDefaultValue());
          }

          int lgSize = -1;
          switch (typeBuilder.which()) {
            case schema::Type::VOID: lgSize = -1; break;
            case schema::Type::BOOL: lgSize = 0; break;
            case schema::Type::INT8: lgSize = 3; break;
            case schema::Type::INT16: lgSize = 4; break;
            case schema::Type::INT32: lgSize = 5; break;
            case schema::Type::INT64: lgSize = 6; break;
            case schema::Type::UINT8: lgSize = 3; break;
            case schema::Type::UINT16: lgSize = 4; break;
            case schema::Type::UINT32: lgSize = 5; break;
            case schema::Type::UINT64: lgSize = 6; break;
            case schema::Type::FLOAT32: lgSize = 5; break;
            case schema::Type::FLOAT64: lgSize = 6; break;

            case schema::Type::TEXT: lgSize = -2; break;
            case schema::Type::DATA: lgSize = -2; break;
            case schema::Type::LIST: lgSize = -2; break;
            case schema::Type::ENUM: lgSize = 4; break;
            case schema::Type::STRUCT: lgSize = -2; break;
            case schema::Type::INTERFACE: lgSize = -2; break;
            case schema::Type::ANY_POINTER: lgSize = -2; break;
          }

          if (lgSize == -2) {
            // pointer
            slot.setOffset(member.fieldScope->addPointer());
          } else if (lgSize == -1) {
            // void
            member.fieldScope->addVoid();
            slot.setOffset(0);
          } else {
            slot.setOffset(member.fieldScope->addData(lgSize));
          }
          break;
        }

        case Declaration::UNION:
          if (!member.unionScope->addDiscriminant()) {
            errorReporter.addErrorOn(member.declId.getOrdinal(),
                "Union ordinal, if specified, must be greater than no more than one of its "
                "member ordinals (i.e. there can only be one field retroactively unionized).");
          }
          break;

        case Declaration::GROUP:
          KJ_FAIL_ASSERT("Groups don't have ordinals.");
          break;

        default:
          KJ_FAIL_ASSERT("Unexpected member type.");
          break;
      }
    }

    // OK, we should have built all the members.  Now go through and make sure the discriminant
    // offsets have been copied over to the schemas and annotations have been applied.
    root.finishGroup();
    for (auto member: allMembers) {
      kj::StringPtr targetsFlagName;
      if (member->isParam) {
        targetsFlagName = "targetsParam";
      } else {
        switch (member->declKind) {
          case Declaration::FIELD:
            targetsFlagName = "targetsField";
            break;

          case Declaration::UNION:
            member->finishGroup();
            targetsFlagName = "targetsUnion";
            break;

          case Declaration::GROUP:
            member->finishGroup();
            targetsFlagName = "targetsGroup";
            break;

          default:
            KJ_FAIL_ASSERT("Unexpected member type.");
            break;
        }
      }

      member->getSchema().adoptAnnotations(translator.compileAnnotationApplications(
          member->declAnnotations, targetsFlagName));
    }

    // And fill in the sizes.
    structBuilder.setDataWordCount(layout.getTop().dataWordCount);
    structBuilder.setPointerCount(layout.getTop().pointerCount);
    structBuilder.setPreferredListEncoding(schema::ElementSize::INLINE_COMPOSITE);

    if (layout.getTop().pointerCount == 0) {
      if (layout.getTop().dataWordCount == 0) {
        structBuilder.setPreferredListEncoding(schema::ElementSize::EMPTY);
      } else if (layout.getTop().dataWordCount == 1) {
        switch (layout.getTop().holes.getFirstWordUsed()) {
          case 0: structBuilder.setPreferredListEncoding(schema::ElementSize::BIT); break;
          case 1:
          case 2:
          case 3: structBuilder.setPreferredListEncoding(schema::ElementSize::BYTE); break;
          case 4: structBuilder.setPreferredListEncoding(schema::ElementSize::TWO_BYTES); break;
          case 5: structBuilder.setPreferredListEncoding(schema::ElementSize::FOUR_BYTES); break;
          case 6: structBuilder.setPreferredListEncoding(schema::ElementSize::EIGHT_BYTES); break;
          default: KJ_FAIL_ASSERT("Expected 0, 1, 2, 3, 4, 5, or 6."); break;
        }
      }
    } else if (layout.getTop().pointerCount == 1 &&
               layout.getTop().dataWordCount == 0) {
      structBuilder.setPreferredListEncoding(schema::ElementSize::POINTER);
    }

    for (auto& group: translator.groups) {
      auto groupBuilder = group.get().getStruct();
      groupBuilder.setDataWordCount(structBuilder.getDataWordCount());
      groupBuilder.setPointerCount(structBuilder.getPointerCount());
      groupBuilder.setPreferredListEncoding(structBuilder.getPreferredListEncoding());
    }
  }
};

void NodeTranslator::compileStruct(Void decl, List<Declaration>::Reader members,
                                   schema::Node::Builder builder) {
  StructTranslator(*this).translate(decl, members, builder);
}

// -------------------------------------------------------------------

static kj::String declNameString(DeclName::Reader name);

void NodeTranslator::compileInterface(Declaration::Interface::Reader decl,
                                      List<Declaration>::Reader members,
                                      schema::Node::Builder builder) {
  auto interfaceBuilder = builder.initInterface();

  auto extendsDecl = decl.getExtends();
  auto extendsBuilder = interfaceBuilder.initExtends(extendsDecl.size());
  for (uint i: kj::indices(extendsDecl)) {
    auto extend = extendsDecl[i];
    if (extend.getBase().isAbsoluteName() && !extend.getBase().hasAbsoluteName()) {
      // Compile error reported earlier.
    } else KJ_IF_MAYBE(target, resolver.resolve(extend)) {
      if (target->kind == Declaration::INTERFACE) {
        extendsBuilder.set(i, target->id);
      } else {
        errorReporter.addErrorOn(
            extend, kj::str("'", declNameString(extend), "' is not an interface type."));
      }
    }
  }

  // maps ordinal -> (code order, declaration)
  std::multimap<uint, std::pair<uint, Declaration::Reader>> methods;

  uint codeOrder = 0;
  for (auto member: members) {
    if (member.isMethod()) {
      methods.insert(
          std::make_pair(member.getId().getOrdinal().getValue(),
                         std::make_pair(codeOrder++, member)));
    }
  }

  auto list = interfaceBuilder.initMethods(methods.size());
  uint i = 0;
  DuplicateOrdinalDetector dupDetector(errorReporter);

  for (auto& entry: methods) {
    uint codeOrder = entry.second.first;
    Declaration::Reader methodDecl = entry.second.second;
    auto methodReader = methodDecl.getMethod();

    auto ordinalDecl = methodDecl.getId().getOrdinal();
    dupDetector.check(ordinalDecl);
    uint16_t ordinal = ordinalDecl.getValue();

    auto methodBuilder = list[i++];
    methodBuilder.setName(methodDecl.getName().getValue());
    methodBuilder.setCodeOrder(codeOrder);

    methodBuilder.setParamStructType(compileParamList(
        methodDecl.getName().getValue(), ordinal, false, methodReader.getParams()));

    auto results = methodReader.getResults();
    if (results.isExplicit()) {
      methodBuilder.setResultStructType(compileParamList(
          methodDecl.getName().getValue(), ordinal, true, results.getExplicit()));
    } else {
      // This works because namedList is the default kind of ParamList, and it will default to
      // an empty list.
      methodBuilder.setResultStructType(compileParamList(
          methodDecl.getName().getValue(), ordinal, true, Declaration::ParamList::Reader()));
    }

    methodBuilder.adoptAnnotations(compileAnnotationApplications(
        methodDecl.getAnnotations(), "targetsMethod"));
  }
}

uint64_t NodeTranslator::compileParamList(
    kj::StringPtr methodName, uint16_t ordinal, bool isResults,
    Declaration::ParamList::Reader paramList) {
  switch (paramList.which()) {
    case Declaration::ParamList::NAMED_LIST: {
      auto newStruct = orphanage.newOrphan<schema::Node>();
      auto builder = newStruct.get();
      auto parent = wipNode.getReader();

      kj::String typeName = kj::str(methodName, isResults ? "$Results" : "$Params");

      builder.setId(generateMethodParamsId(parent.getId(), ordinal, isResults));
      builder.setDisplayName(kj::str(parent.getDisplayName(), '.', typeName));
      builder.setDisplayNamePrefixLength(builder.getDisplayName().size() - typeName.size());
      builder.setScopeId(0);  // detached struct type

      builder.initStruct();

      StructTranslator(*this).translate(paramList.getNamedList(), builder);
      uint64_t id = builder.getId();
      paramStructs.add(kj::mv(newStruct));
      return id;
    }
    case Declaration::ParamList::TYPE:
      KJ_IF_MAYBE(target, resolver.resolve(paramList.getType())) {
        if (target->kind == Declaration::STRUCT) {
          return target->id;
        } else {
          errorReporter.addErrorOn(
              paramList.getType(),
              kj::str("'", declNameString(paramList.getType()), "' is not a struct type."));
        }
      }
      return 0;
  }
  KJ_UNREACHABLE;
}

// -------------------------------------------------------------------

static kj::String declNameString(DeclName::Reader name) {
  kj::String prefix;

  switch (name.getBase().which()) {
    case DeclName::Base::RELATIVE_NAME:
      prefix = kj::str(name.getBase().getRelativeName().getValue());
      break;
    case DeclName::Base::ABSOLUTE_NAME:
      prefix = kj::str(".", name.getBase().getAbsoluteName().getValue());
      break;
    case DeclName::Base::IMPORT_NAME:
      prefix = kj::str("import \"", name.getBase().getImportName().getValue(), "\"");
      break;
  }

  if (name.getMemberPath().size() == 0) {
    return prefix;
  } else {
    auto path = name.getMemberPath();
    KJ_STACK_ARRAY(kj::StringPtr, parts, path.size(), 16, 16);
    for (size_t i = 0; i < parts.size(); i++) {
      parts[i] = path[i].getValue();
    }
    return kj::str(prefix, ".", kj::strArray(parts, "."));
  }
}

bool NodeTranslator::compileType(TypeExpression::Reader source, schema::Type::Builder target) {
  auto name = source.getName();
  KJ_IF_MAYBE(base, resolver.resolve(name)) {
    bool handledParams = false;

    switch (base->kind) {
      case Declaration::ENUM: target.initEnum().setTypeId(base->id); break;
      case Declaration::STRUCT: target.initStruct().setTypeId(base->id); break;
      case Declaration::INTERFACE: target.initInterface().setTypeId(base->id); break;

      case Declaration::BUILTIN_LIST: {
        auto params = source.getParams();
        if (params.size() != 1) {
          errorReporter.addErrorOn(source, "'List' requires exactly one parameter.");
          return false;
        }

        auto elementType = target.initList().initElementType();
        if (!compileType(params[0], elementType)) {
          return false;
        }

        if (elementType.isAnyPointer()) {
          errorReporter.addErrorOn(source, "'List(AnyPointer)' is not supported.");
          // Seeing List(AnyPointer) later can mess things up, so change the type to Void.
          elementType.setVoid();
          return false;
        }

        handledParams = true;
        break;
      }

      case Declaration::BUILTIN_VOID: target.setVoid(); break;
      case Declaration::BUILTIN_BOOL: target.setBool(); break;
      case Declaration::BUILTIN_INT8: target.setInt8(); break;
      case Declaration::BUILTIN_INT16: target.setInt16(); break;
      case Declaration::BUILTIN_INT32: target.setInt32(); break;
      case Declaration::BUILTIN_INT64: target.setInt64(); break;
      case Declaration::BUILTIN_U_INT8: target.setUint8(); break;
      case Declaration::BUILTIN_U_INT16: target.setUint16(); break;
      case Declaration::BUILTIN_U_INT32: target.setUint32(); break;
      case Declaration::BUILTIN_U_INT64: target.setUint64(); break;
      case Declaration::BUILTIN_FLOAT32: target.setFloat32(); break;
      case Declaration::BUILTIN_FLOAT64: target.setFloat64(); break;
      case Declaration::BUILTIN_TEXT: target.setText(); break;
      case Declaration::BUILTIN_DATA: target.setData(); break;

      case Declaration::BUILTIN_OBJECT:
        errorReporter.addErrorOn(source,
            "As of Cap'n Proto 0.4, 'Object' has been renamed to 'AnyPointer'.  Sorry for the "
            "inconvenience, and thanks for being an early adopter.  :)");
        // no break
      case Declaration::BUILTIN_ANY_POINTER: target.setAnyPointer(); break;

      default:
        errorReporter.addErrorOn(source, kj::str("'", declNameString(name), "' is not a type."));
        return false;
    }

    if (!handledParams) {
      if (source.getParams().size() != 0) {
        errorReporter.addErrorOn(source, kj::str(
            "'", declNameString(name), "' does not accept parameters."));
        return false;
      }
    }

    return true;

  } else {
    target.setVoid();
    return false;
  }
}

// -------------------------------------------------------------------

void NodeTranslator::compileDefaultDefaultValue(
    schema::Type::Reader type, schema::Value::Builder target) {
  switch (type.which()) {
    case schema::Type::VOID: target.setVoid(); break;
    case schema::Type::BOOL: target.setBool(false); break;
    case schema::Type::INT8: target.setInt8(0); break;
    case schema::Type::INT16: target.setInt16(0); break;
    case schema::Type::INT32: target.setInt32(0); break;
    case schema::Type::INT64: target.setInt64(0); break;
    case schema::Type::UINT8: target.setUint8(0); break;
    case schema::Type::UINT16: target.setUint16(0); break;
    case schema::Type::UINT32: target.setUint32(0); break;
    case schema::Type::UINT64: target.setUint64(0); break;
    case schema::Type::FLOAT32: target.setFloat32(0); break;
    case schema::Type::FLOAT64: target.setFloat64(0); break;
    case schema::Type::ENUM: target.setEnum(0); break;
    case schema::Type::INTERFACE: target.setInterface(); break;

    // Bit of a hack:  For Text/Data, we adopt a null orphan, which sets the field to null.
    // TODO(cleanup):  Create a cleaner way to do this.
    case schema::Type::TEXT: target.adoptText(Orphan<Text>()); break;
    case schema::Type::DATA: target.adoptData(Orphan<Data>()); break;
    case schema::Type::STRUCT: target.initStruct(); break;
    case schema::Type::LIST: target.initList(); break;
    case schema::Type::ANY_POINTER: target.initAnyPointer(); break;
  }
}

void NodeTranslator::compileBootstrapValue(ValueExpression::Reader source,
                                           schema::Type::Reader type,
                                           schema::Value::Builder target) {
  // Start by filling in a default default value so that if for whatever reason we don't end up
  // initializing the value, this won't cause schema validation to fail.
  compileDefaultDefaultValue(type, target);

  switch (type.which()) {
    case schema::Type::LIST:
    case schema::Type::STRUCT:
    case schema::Type::INTERFACE:
    case schema::Type::ANY_POINTER:
      unfinishedValues.add(UnfinishedValue { source, type, target });
      break;

    default:
      // Primitive value.
      compileValue(source, type, target, true);
      break;
  }
}

void NodeTranslator::compileValue(ValueExpression::Reader source, schema::Type::Reader type,
                                  schema::Value::Builder target, bool isBootstrap) {
  class ResolverGlue: public ValueTranslator::Resolver {
  public:
    inline ResolverGlue(NodeTranslator& translator, bool isBootstrap)
        : translator(translator), isBootstrap(isBootstrap) {}

    kj::Maybe<Schema> resolveType(uint64_t id) override {
      // Always use bootstrap schemas when resolving types, because final schemas are unsafe to
      // use with the dynamic API and bootstrap schemas have all the info needed anyway.
      return translator.resolver.resolveBootstrapSchema(id);
    }

    kj::Maybe<DynamicValue::Reader> resolveConstant(DeclName::Reader name) override {
      return translator.readConstant(name, isBootstrap);
    }

  private:
    NodeTranslator& translator;
    bool isBootstrap;
  };

  ResolverGlue glue(*this, isBootstrap);
  ValueTranslator valueTranslator(glue, errorReporter, orphanage);

  kj::StringPtr fieldName = KJ_ASSERT_NONNULL(toDynamic(type).which()).getProto().getName();
  KJ_IF_MAYBE(value, valueTranslator.compileValue(source, type)) {
    if (type.isEnum()) {
      target.setEnum(value->getReader().as<DynamicEnum>().getRaw());
    } else {
      toDynamic(target).adopt(fieldName, kj::mv(*value));
    }
  }
}

kj::Maybe<Orphan<DynamicValue>> ValueTranslator::compileValue(
    ValueExpression::Reader src, schema::Type::Reader type) {
  Orphan<DynamicValue> result = compileValueInner(src, type);

  switch (result.getType()) {
    case DynamicValue::UNKNOWN:
      // Error already reported.
      return nullptr;

    case DynamicValue::VOID:
      if (type.isVoid()) {
        return kj::mv(result);
      }
      break;

    case DynamicValue::BOOL:
      if (type.isBool()) {
        return kj::mv(result);
      }
      break;

    case DynamicValue::INT: {
      int64_t value = result.getReader().as<int64_t>();
      if (value < 0) {
        int64_t minValue = 1;
        switch (type.which()) {
          case schema::Type::INT8: minValue = (int8_t)kj::minValue; break;
          case schema::Type::INT16: minValue = (int16_t)kj::minValue; break;
          case schema::Type::INT32: minValue = (int32_t)kj::minValue; break;
          case schema::Type::INT64: minValue = (int64_t)kj::minValue; break;
          case schema::Type::UINT8: minValue = (uint8_t)kj::minValue; break;
          case schema::Type::UINT16: minValue = (uint16_t)kj::minValue; break;
          case schema::Type::UINT32: minValue = (uint32_t)kj::minValue; break;
          case schema::Type::UINT64: minValue = (uint64_t)kj::minValue; break;

          case schema::Type::FLOAT32:
          case schema::Type::FLOAT64:
            // Any integer is acceptable.
            minValue = (int64_t)kj::minValue;
            break;

          default: break;
        }
        if (minValue == 1) break;

        if (value < minValue) {
          errorReporter.addErrorOn(src, "Integer value out of range.");
          result = minValue;
        }
        return kj::mv(result);
      }

      // No break -- value is positive, so we can just go on to the uint case below.
    }

    case DynamicValue::UINT: {
      uint64_t maxValue = 0;
      switch (type.which()) {
        case schema::Type::INT8: maxValue = (int8_t)kj::maxValue; break;
        case schema::Type::INT16: maxValue = (int16_t)kj::maxValue; break;
        case schema::Type::INT32: maxValue = (int32_t)kj::maxValue; break;
        case schema::Type::INT64: maxValue = (int64_t)kj::maxValue; break;
        case schema::Type::UINT8: maxValue = (uint8_t)kj::maxValue; break;
        case schema::Type::UINT16: maxValue = (uint16_t)kj::maxValue; break;
        case schema::Type::UINT32: maxValue = (uint32_t)kj::maxValue; break;
        case schema::Type::UINT64: maxValue = (uint64_t)kj::maxValue; break;

        case schema::Type::FLOAT32:
        case schema::Type::FLOAT64:
          // Any integer is acceptable.
          maxValue = (uint64_t)kj::maxValue;
          break;

        default: break;
      }
      if (maxValue == 0) break;

      if (result.getReader().as<uint64_t>() > maxValue) {
        errorReporter.addErrorOn(src, "Integer value out of range.");
        result = maxValue;
      }
      return kj::mv(result);
    }

    case DynamicValue::FLOAT:
      if (type.isFloat32() || type.isFloat64()) {
        return kj::mv(result);
      }
      break;

    case DynamicValue::TEXT:
      if (type.isText()) {
        return kj::mv(result);
      }
      break;

    case DynamicValue::DATA:
      if (type.isData()) {
        return kj::mv(result);
      }
      break;

    case DynamicValue::LIST:
      if (type.isList()) {
        KJ_IF_MAYBE(schema, makeListSchemaOf(type.getList().getElementType())) {
          if (result.getReader().as<DynamicList>().getSchema() == *schema) {
            return kj::mv(result);
          }
        } else {
          return nullptr;
        }
      }
      break;

    case DynamicValue::ENUM:
      if (type.isEnum()) {
        KJ_IF_MAYBE(schema, resolver.resolveType(type.getEnum().getTypeId())) {
          if (result.getReader().as<DynamicEnum>().getSchema() == *schema) {
            return kj::mv(result);
          }
        } else {
          return nullptr;
        }
      }
      break;

    case DynamicValue::STRUCT:
      if (type.isStruct()) {
        KJ_IF_MAYBE(schema, resolver.resolveType(type.getStruct().getTypeId())) {
          if (result.getReader().as<DynamicStruct>().getSchema() == *schema) {
            return kj::mv(result);
          }
        } else {
          return nullptr;
        }
      }
      break;

    case DynamicValue::CAPABILITY:
      KJ_FAIL_ASSERT("Interfaces can't have literal values.");

    case DynamicValue::ANY_POINTER:
      KJ_FAIL_ASSERT("AnyPointers can't have literal values.");
  }

  errorReporter.addErrorOn(src, kj::str("Type mismatch; expected ", makeTypeName(type), "."));
  return nullptr;
}

Orphan<DynamicValue> ValueTranslator::compileValueInner(
    ValueExpression::Reader src, schema::Type::Reader type) {
  switch (src.which()) {
    case ValueExpression::NAME: {
      auto name = src.getName();
      bool isBare = name.getBase().isRelativeName() &&
                    name.getMemberPath().size() == 0;
      if (isBare) {
        // The name is just a bare identifier.  It may be a literal value or an enumerant.
        kj::StringPtr id = name.getBase().getRelativeName().getValue();

        if (type.isEnum()) {
          KJ_IF_MAYBE(enumSchema, resolver.resolveType(type.getEnum().getTypeId())) {
            KJ_IF_MAYBE(enumerant, enumSchema->asEnum().findEnumerantByName(id)) {
              return DynamicEnum(*enumerant);
            }
          } else {
            // Enum type is broken.
            return nullptr;
          }
        } else {
          // Interpret known constant values.
          if (id == "void") {
            return VOID;
          } else if (id == "true") {
            return true;
          } else if (id == "false") {
            return false;
          } else if (id == "nan") {
            return kj::nan();
          } else if (id == "inf") {
            return kj::inf();
          }
        }
      }

      // Haven't resolved the name yet.  Try looking up a constant.
      KJ_IF_MAYBE(constValue, resolver.resolveConstant(src.getName())) {
        return orphanage.newOrphanCopy(*constValue);
      }

      return nullptr;
    }

    case ValueExpression::POSITIVE_INT:
      return src.getPositiveInt();

    case ValueExpression::NEGATIVE_INT: {
      uint64_t nValue = src.getNegativeInt();
      if (nValue > ((uint64_t)kj::maxValue >> 1) + 1) {
        errorReporter.addErrorOn(src, "Integer is too big to be negative.");
        return nullptr;
      } else {
        return kj::implicitCast<int64_t>(-nValue);
      }
    }

    case ValueExpression::FLOAT:
      return src.getFloat();
      break;

    case ValueExpression::STRING:
      if (type.isData()) {
        Text::Reader text = src.getString();
        return orphanage.newOrphanCopy(Data::Reader(
            reinterpret_cast<const byte*>(text.begin()), text.size()));
      } else {
        return orphanage.newOrphanCopy(src.getString());
      }
      break;

    case ValueExpression::BINARY:
      if (!type.isData()) {
        errorReporter.addErrorOn(src, kj::str("Type mismatch; expected ", makeTypeName(type), "."));
        return nullptr;
      }
      return orphanage.newOrphanCopy(src.getBinary());

    case ValueExpression::LIST: {
      if (!type.isList()) {
        errorReporter.addErrorOn(src, kj::str("Type mismatch; expected ", makeTypeName(type), "."));
        return nullptr;
      }
      auto elementType = type.getList().getElementType();
      KJ_IF_MAYBE(listSchema, makeListSchemaOf(elementType)) {
        auto srcList = src.getList();
        Orphan<DynamicList> result = orphanage.newOrphan(*listSchema, srcList.size());
        auto dstList = result.get();
        for (uint i = 0; i < srcList.size(); i++) {
          KJ_IF_MAYBE(value, compileValue(srcList[i], elementType)) {
            dstList.adopt(i, kj::mv(*value));
          }
        }
        return kj::mv(result);
      } else {
        return nullptr;
      }
    }

    case ValueExpression::STRUCT: {
      if (!type.isStruct()) {
        errorReporter.addErrorOn(src, kj::str("Type mismatch; expected ", makeTypeName(type), "."));
        return nullptr;
      }
      KJ_IF_MAYBE(schema, resolver.resolveType(type.getStruct().getTypeId())) {
        auto structSchema = schema->asStruct();
        Orphan<DynamicStruct> result = orphanage.newOrphan(structSchema);
        fillStructValue(result.get(), src.getStruct());
        return kj::mv(result);
      } else {
        return nullptr;
      }
    }

    case ValueExpression::UNKNOWN:
      // Ignore earlier error.
      return nullptr;
  }

  KJ_UNREACHABLE;
}

void ValueTranslator::fillStructValue(DynamicStruct::Builder builder,
                                      List<ValueExpression::FieldAssignment>::Reader assignments) {
  for (auto assignment: assignments) {
    auto fieldName = assignment.getFieldName();
    KJ_IF_MAYBE(field, builder.getSchema().findFieldByName(fieldName.getValue())) {
      auto fieldProto = field->getProto();
      auto value = assignment.getValue();

      switch (fieldProto.which()) {
        case schema::Field::SLOT:
          KJ_IF_MAYBE(compiledValue, compileValue(value, fieldProto.getSlot().getType())) {
            builder.adopt(*field, kj::mv(*compiledValue));
          }
          break;

        case schema::Field::GROUP:
          if (value.isStruct()) {
            fillStructValue(builder.init(*field).as<DynamicStruct>(), value.getStruct());
          } else {
            errorReporter.addErrorOn(value, "Type mismatch; expected group.");
          }
          break;
      }
    } else {
      errorReporter.addErrorOn(fieldName, kj::str(
          "Struct has no field named '", fieldName.getValue(), "'."));
    }
  }
}

kj::String ValueTranslator::makeNodeName(uint64_t id) {
  KJ_IF_MAYBE(schema, resolver.resolveType(id)) {
    schema::Node::Reader proto = schema->getProto();
    return kj::str(proto.getDisplayName().slice(proto.getDisplayNamePrefixLength()));
  } else {
    return kj::str("@0x", kj::hex(id));
  }
}

kj::String ValueTranslator::makeTypeName(schema::Type::Reader type) {
  switch (type.which()) {
    case schema::Type::VOID: return kj::str("Void");
    case schema::Type::BOOL: return kj::str("Bool");
    case schema::Type::INT8: return kj::str("Int8");
    case schema::Type::INT16: return kj::str("Int16");
    case schema::Type::INT32: return kj::str("Int32");
    case schema::Type::INT64: return kj::str("Int64");
    case schema::Type::UINT8: return kj::str("UInt8");
    case schema::Type::UINT16: return kj::str("UInt16");
    case schema::Type::UINT32: return kj::str("UInt32");
    case schema::Type::UINT64: return kj::str("UInt64");
    case schema::Type::FLOAT32: return kj::str("Float32");
    case schema::Type::FLOAT64: return kj::str("Float64");
    case schema::Type::TEXT: return kj::str("Text");
    case schema::Type::DATA: return kj::str("Data");
    case schema::Type::LIST:
      return kj::str("List(", makeTypeName(type.getList().getElementType()), ")");
    case schema::Type::ENUM: return makeNodeName(type.getEnum().getTypeId());
    case schema::Type::STRUCT: return makeNodeName(type.getStruct().getTypeId());
    case schema::Type::INTERFACE: return makeNodeName(type.getInterface().getTypeId());
    case schema::Type::ANY_POINTER: return kj::str("AnyPointer");
  }
  KJ_UNREACHABLE;
}

kj::Maybe<DynamicValue::Reader> NodeTranslator::readConstant(
    DeclName::Reader name, bool isBootstrap) {
  KJ_IF_MAYBE(resolved, resolver.resolve(name)) {
    if (resolved->kind != Declaration::CONST) {
      errorReporter.addErrorOn(name,
          kj::str("'", declNameString(name), "' does not refer to a constant."));
      return nullptr;
    }

    // If we're bootstrapping, then we know we're expecting a primitive value, so if the
    // constant turns out to be non-primitive, we'll error out anyway.  If we're not
    // bootstrapping, we may be compiling a non-primitive value and so we need the final
    // version of the constant to make sure its value is filled in.
    kj::Maybe<schema::Node::Reader> maybeConstSchema = isBootstrap ?
        resolver.resolveBootstrapSchema(resolved->id).map([](Schema s) { return s.getProto(); }) :
        resolver.resolveFinalSchema(resolved->id);
    KJ_IF_MAYBE(constSchema, maybeConstSchema) {
      auto constReader = constSchema->getConst();
      auto dynamicConst = toDynamic(constReader.getValue());
      auto constValue = dynamicConst.get(KJ_ASSERT_NONNULL(dynamicConst.which()));

      if (constValue.getType() == DynamicValue::ANY_POINTER) {
        // We need to assign an appropriate schema to this pointer.
        AnyPointer::Reader objValue = constValue.as<AnyPointer>();
        auto constType = constReader.getType();
        switch (constType.which()) {
          case schema::Type::STRUCT:
            KJ_IF_MAYBE(structSchema, resolver.resolveBootstrapSchema(
                constType.getStruct().getTypeId())) {
              constValue = objValue.getAs<DynamicStruct>(structSchema->asStruct());
            } else {
              // The struct's schema is broken for reasons already reported.
              return nullptr;
            }
            break;
          case schema::Type::LIST:
            KJ_IF_MAYBE(listSchema, makeListSchemaOf(constType.getList().getElementType())) {
              constValue = objValue.getAs<DynamicList>(*listSchema);
            } else {
              // The list's schema is broken for reasons already reported.
              return nullptr;
            }
            break;
          case schema::Type::ANY_POINTER:
            // Fine as-is.
            break;
          default:
            KJ_FAIL_ASSERT("Unrecognized AnyPointer-typed member of schema::Value.");
            break;
        }
      }

      if (name.getBase().isRelativeName() &&
          name.getMemberPath().size() == 0) {
        // A fully unqualified identifier looks like it might refer to a constant visible in the
        // current scope, but if that's really what the user wanted, we want them to use a
        // qualified name to make it more obvious.  Report an error.
        KJ_IF_MAYBE(scope, resolver.resolveBootstrapSchema(constSchema->getScopeId())) {
          auto scopeReader = scope->getProto();
          kj::StringPtr parent;
          if (scopeReader.isFile()) {
            parent = "";
          } else {
            parent = scopeReader.getDisplayName().slice(scopeReader.getDisplayNamePrefixLength());
          }
          kj::StringPtr id = name.getBase().getRelativeName().getValue();

          errorReporter.addErrorOn(name, kj::str(
              "Constant names must be qualified to avoid confusion.  Please replace '",
              declNameString(name), "' with '", parent, ".", id,
              "', if that's what you intended."));
        }
      }

      return constValue;
    } else {
      // The target is a constant, but the constant's schema is broken for reasons already reported.
      return nullptr;
    }
  } else {
    // Lookup will have reported an error.
    return nullptr;
  }
}

template <typename ResolveTypeFunc>
static kj::Maybe<ListSchema> makeListSchemaImpl(schema::Type::Reader elementType,
                                                const ResolveTypeFunc& resolveType) {
  switch (elementType.which()) {
    case schema::Type::ENUM:
      KJ_IF_MAYBE(enumSchema, resolveType(elementType.getEnum().getTypeId())) {
        return ListSchema::of(enumSchema->asEnum());
      } else {
        return nullptr;
      }
    case schema::Type::STRUCT:
      KJ_IF_MAYBE(structSchema, resolveType(elementType.getStruct().getTypeId())) {
        return ListSchema::of(structSchema->asStruct());
      } else {
        return nullptr;
      }
    case schema::Type::INTERFACE:
      KJ_IF_MAYBE(interfaceSchema, resolveType(elementType.getInterface().getTypeId())) {
        return ListSchema::of(interfaceSchema->asInterface());
      } else {
        return nullptr;
      }
    case schema::Type::LIST:
      KJ_IF_MAYBE(listSchema, makeListSchemaImpl(
          elementType.getList().getElementType(), resolveType)) {
        return ListSchema::of(*listSchema);
      } else {
        return nullptr;
      }
    default:
      return ListSchema::of(elementType.which());
  }
}

kj::Maybe<ListSchema> NodeTranslator::makeListSchemaOf(schema::Type::Reader elementType) {
  return makeListSchemaImpl(elementType,
      [this](uint64_t id) { return resolver.resolveBootstrapSchema(id); });
}

kj::Maybe<ListSchema> ValueTranslator::makeListSchemaOf(schema::Type::Reader elementType) {
  return makeListSchemaImpl(elementType,
      [this](uint64_t id) { return resolver.resolveType(id); });
}

Orphan<List<schema::Annotation>> NodeTranslator::compileAnnotationApplications(
    List<Declaration::AnnotationApplication>::Reader annotations,
    kj::StringPtr targetsFlagName) {
  if (annotations.size() == 0 || !compileAnnotations) {
    // Return null.
    return Orphan<List<schema::Annotation>>();
  }

  auto result = orphanage.newOrphan<List<schema::Annotation>>(annotations.size());
  auto builder = result.get();

  for (uint i = 0; i < annotations.size(); i++) {
    Declaration::AnnotationApplication::Reader annotation = annotations[i];
    schema::Annotation::Builder annotationBuilder = builder[i];

    // Set the annotation's value to void in case we fail to produce something better below.
    annotationBuilder.initValue().setVoid();

    auto name = annotation.getName();
    KJ_IF_MAYBE(decl, resolver.resolve(name)) {
      if (decl->kind != Declaration::ANNOTATION) {
        errorReporter.addErrorOn(name, kj::str(
            "'", declNameString(name), "' is not an annotation."));
      } else {
        annotationBuilder.setId(decl->id);
        KJ_IF_MAYBE(annotationSchema, resolver.resolveBootstrapSchema(decl->id)) {
          auto node = annotationSchema->getProto().getAnnotation();
          if (!toDynamic(node).get(targetsFlagName).as<bool>()) {
            errorReporter.addErrorOn(name, kj::str(
                "'", declNameString(name), "' cannot be applied to this kind of declaration."));
          }

          // Interpret the value.
          auto value = annotation.getValue();
          switch (value.which()) {
            case Declaration::AnnotationApplication::Value::NONE:
              // No value, i.e. void.
              if (node.getType().isVoid()) {
                annotationBuilder.getValue().setVoid();
              } else {
                errorReporter.addErrorOn(name, kj::str(
                    "'", declNameString(name), "' requires a value."));
                compileDefaultDefaultValue(node.getType(), annotationBuilder.getValue());
              }
              break;

            case Declaration::AnnotationApplication::Value::EXPRESSION:
              compileBootstrapValue(value.getExpression(), node.getType(),
                                    annotationBuilder.getValue());
              break;
          }
        }
      }
    }
  }

  return result;
}

}  // namespace compiler
}  // namespace capnp
