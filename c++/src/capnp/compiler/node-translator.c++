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
#include "parser.h"      // only for generateGroupId() and expressionString()
#include <capnp/serialize.h>
#include <kj/debug.h>
#include <kj/arena.h>
#include <kj/encoding.h>
#include <set>
#include <map>
#include <stdlib.h>
#include <capnp/stream.capnp.h>

namespace capnp {
namespace compiler {

bool shouldDetectIssue344() {
  return getenv("CAPNP_IGNORE_ISSUE_344") == nullptr;
}

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
                       UIntType limitLgSize = sizeof(HoleSet::holes) / sizeof(HoleSet::holes[0])) {
      // Add new holes of progressively larger sizes in the range [lgSize, limitLgSize) starting
      // from the given offset.  The idea is that you just allocated an lgSize-sized field from
      // an limitLgSize-sized space, such as a newly-added word on the end of the data segment.

      KJ_ASSUME(limitLgSize <= kj::size(holes));

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
      if (oldLgSize == kj::size(holes)) {
        // Old value is already a full word. Further expansion is impossible.
        return false;
      }
      KJ_ASSERT(oldLgSize < kj::size(holes));
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
    KJ_DISALLOW_COPY_AND_MOVE(Top);
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
    KJ_DISALLOW_COPY_AND_MOVE(Union);

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

  struct Group final: public StructOrGroup {
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
          if (tryExpandUsage(group, location, newSize, true)) {
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
          return tryExpandUsage(group, location, oldLgSize + expansionFactor, false);
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

      bool tryExpandUsage(Group& group, Union::DataLocation& location, uint desiredUsage,
                          bool newHoles) {
        if (desiredUsage > location.lgSize) {
          // Need to expand the underlying slot.
          if (!location.tryExpandTo(group.parent, desiredUsage)) {
            return false;
          }
        }

        // Underlying slot is big enough, so expand our size and update holes.
        if (newHoles) {
          holes.addHolesAtEnd(lgSizeUsed, 1, desiredUsage);
        } else if (shouldDetectIssue344()) {
          // Unfortunately, Cap'n Proto 0.5.x and below would always call addHolesAtEnd(), which
          // was the wrong thing to do when called from tryExpand(), which itself is only called
          // in cases involving unions nested in other unions. The bug could lead to multiple
          // fields in a group incorrectly being assigned overlapping offsets. Although the bug
          // is now fixed by adding the `newHoles` parameter, this silently breaks
          // backwards-compatibility with affected schemas. Therefore, for now, we throw an
          // exception to alert developers of the problem.
          //
          // TODO(cleanup): Once sufficient time has elapsed, remove this assert.
          KJ_FAIL_ASSERT("Bad news: Cap'n Proto 0.5.x and previous contained a bug which would cause this schema to be compiled incorrectly. Please see: https://github.com/capnproto/capnproto/issues/344");
        }
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
    KJ_DISALLOW_COPY_AND_MOVE(Group);

    void addMember() {
      if (!hasMembers) {
        hasMembers = true;
        parent.newGroupAddingFirstMember();
      }
    }

    void addVoid() override {
      addMember();

      // Make sure that if this is a member of a union which is in turn a member of another union,
      // that we let the outer union know that a field is being added, even though it is a
      // zero-size field. This is important because the union needs to allocate its discriminant
      // just before its second member is added.
      parent.parent.addVoid();
    }

    uint addData(uint lgSize) override {
      addMember();

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
      addMember();

      if (parentPointerLocationUsage < parent.pointerLocations.size()) {
        return parent.pointerLocations[parentPointerLocationUsage++];
      } else {
        parentPointerLocationUsage++;
        return parent.addNewPointerLocation();
      }
    }

    bool tryExpandData(uint oldLgSize, uint oldOffset, uint expansionFactor) override {
      bool mustFail = false;
      if (oldLgSize + expansionFactor > 6 ||
          (oldOffset & ((1 << expansionFactor) - 1)) != 0) {
        // Expansion is not possible because the new size is too large or the offset is not
        // properly-aligned.

        // Unfortunately, Cap'n Proto 0.5.x and prior forgot to "return false" here, instead
        // continuing to execute the rest of the method. In most cases, the method failed later
        // on, causing no harm. But, in cases where the method later succeeded, it probably
        // led to bogus layouts. We cannot simply add the return statement now as this would
        // silently break backwards-compatibility with affected schemas. Instead, we detect the
        // problem and throw an exception.
        //
        // TODO(cleanup): Once sufficient time has elapsed, switch to "return false;" here.
        if (shouldDetectIssue344()) {
          mustFail = true;
        } else {
          return false;
        }
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
          bool result = usage.tryExpand(
              *this, location, oldLgSize, localOldOffset, expansionFactor);
          if (mustFail && result) {
            KJ_FAIL_ASSERT("Bad news: Cap'n Proto 0.5.x and previous contained a bug which would cause this schema to be compiled incorrectly. Please see: https://github.com/capnproto/capnproto/issues/344");
          }
          return result;
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
      compileAnnotations(compileAnnotations),
      localBrand(kj::refcounted<BrandScope>(
          errorReporter, wipNodeParam.getReader().getId(),
          decl.getParameters().size(), resolver)),
      wipNode(kj::mv(wipNodeParam)),
      sourceInfo(orphanage.newOrphan<schema::Node::SourceInfo>()) {
  compileNode(decl, wipNode.get());
}

NodeTranslator::~NodeTranslator() noexcept(false) {}

NodeTranslator::NodeSet NodeTranslator::getBootstrapNode() {
  auto sourceInfos = kj::heapArrayBuilder<schema::Node::SourceInfo::Reader>(
      1 + groups.size() + paramStructs.size());
  sourceInfos.add(sourceInfo.getReader());
  for (auto& group: groups) {
    sourceInfos.add(group.sourceInfo.getReader());
  }
  for (auto& paramStruct: paramStructs) {
    sourceInfos.add(paramStruct.sourceInfo.getReader());
  }

  auto nodeReader = wipNode.getReader();
  if (nodeReader.isInterface()) {
    return NodeSet {
      nodeReader,
      KJ_MAP(g, paramStructs) { return g.node.getReader(); },
      sourceInfos.finish()
    };
  } else {
    return NodeSet {
      nodeReader,
      KJ_MAP(g, groups) { return g.node.getReader(); },
      sourceInfos.finish()
    };
  }
}

NodeTranslator::NodeSet NodeTranslator::finish(Schema selfBootstrapSchema) {
  // Careful about iteration here:  compileFinalValue() may actually add more elements to
  // `unfinishedValues`, invalidating iterators in the process.
  for (size_t i = 0; i < unfinishedValues.size(); i++) {
    auto& value = unfinishedValues[i];
    compileValue(value.source, value.type, value.typeScope.orDefault(selfBootstrapSchema),
                 value.target, false);
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

  auto genericParams = decl.getParameters();
  if (genericParams.size() > 0) {
    auto paramsBuilder = builder.initParameters(genericParams.size());
    for (auto i: kj::indices(genericParams)) {
      paramsBuilder[i].setName(genericParams[i].getName());
    }
  }

  builder.setIsGeneric(localBrand->isGeneric());

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

  auto di = sourceInfo.get();
  di.setId(wipNode.getReader().getId());
  if (decl.hasDocComment()) {
    di.setDocComment(decl.getDocComment());
  }
}

static kj::StringPtr getExpressionTargetName(Expression::Reader exp) {
  switch (exp.which()) {
    case Expression::ABSOLUTE_NAME:
      return exp.getAbsoluteName().getValue();
    case Expression::RELATIVE_NAME:
      return exp.getRelativeName().getValue();
    case Expression::APPLICATION:
      return getExpressionTargetName(exp.getApplication().getFunction());
    case Expression::MEMBER:
      return exp.getMember().getName().getValue();
    default:
      return nullptr;
  }
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
        case Declaration::USING: {
          kj::StringPtr targetName = getExpressionTargetName(decl.getUsing().getTarget());
          if (targetName.size() > 0 && targetName[0] >= 'a' && targetName[0] <= 'z') {
            // Target starts with lower-case letter, so alias should too.
            if (nameText.size() > 0 && (nameText[0] < 'a' || nameText[0] > 'z')) {
              errorReporter.addErrorOn(name,
                  "Non-type names must begin with a lower-case letter.");
            }
          } else {
            // Target starts with capital or is not named (probably, an import). Require
            // capitalization.
            if (nameText.size() > 0 && (nameText[0] < 'A' || nameText[0] > 'Z')) {
              errorReporter.addErrorOn(name,
                  "Type names must begin with a capital letter.");
            }
          }
          break;
        }

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
  if (compileType(decl.getType(), typeBuilder, ImplicitParams::none())) {
    compileBootstrapValue(decl.getValue(), typeBuilder.asReader(), builder.initValue());
  }
}

void NodeTranslator::compileAnnotation(Declaration::Annotation::Reader decl,
                                       schema::Node::Annotation::Builder builder) {
  compileType(decl.getType(), builder.initType(), ImplicitParams::none());

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
  auto sourceInfoList = sourceInfo.get().initMembers(enumerants.size());
  uint i = 0;
  DuplicateOrdinalDetector dupDetector(errorReporter);

  for (auto& entry: enumerants) {
    uint codeOrder = entry.second.first;
    Declaration::Reader enumerantDecl = entry.second.second;

    dupDetector.check(enumerantDecl.getId().getOrdinal());

    if (enumerantDecl.hasDocComment()) {
      sourceInfoList[i].setDocComment(enumerantDecl.getDocComment());
    }

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
  explicit StructTranslator(NodeTranslator& translator, ImplicitParams implicitMethodParams)
      : translator(translator), errorReporter(translator.errorReporter),
        implicitMethodParams(implicitMethodParams) {}
  KJ_DISALLOW_COPY_AND_MOVE(StructTranslator);

  void translate(Void decl, List<Declaration>::Reader members, schema::Node::Builder builder,
                 schema::Node::SourceInfo::Builder sourceInfo) {
    // Build the member-info-by-ordinal map.
    MemberInfo root(builder, sourceInfo);
    traverseTopOrGroup(members, root, layout.getTop());
    translateInternal(root, builder);
  }

  void translate(List<Declaration::Param>::Reader params, schema::Node::Builder builder,
                 schema::Node::SourceInfo::Builder sourceInfo) {
    // Build a struct from a method param / result list.
    MemberInfo root(builder, sourceInfo);
    traverseParams(params, root, layout.getTop());
    translateInternal(root, builder);
  }

private:
  NodeTranslator& translator;
  ErrorReporter& errorReporter;
  ImplicitParams implicitMethodParams;
  StructLayout layout;
  kj::Arena arena;

  struct NodeSourceInfoBuilderPair {
    schema::Node::Builder node;
    schema::Node::SourceInfo::Builder sourceInfo;
  };

  struct FieldSourceInfoBuilderPair {
    schema::Field::Builder field;
    schema::Node::SourceInfo::Member::Builder sourceInfo;
  };

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
    Expression::Reader fieldType;               // if declKind == FIELD
    Expression::Reader fieldDefaultValue;       // if declKind == FIELD && hasDefaultValue
    List<Declaration::AnnotationApplication>::Reader declAnnotations;
    uint startByte = 0;
    uint endByte = 0;
    // Information about the field declaration.  We don't use Declaration::Reader because it might
    // have come from a Declaration::Param instead.

    kj::Maybe<Text::Reader> docComment = nullptr;

    kj::Maybe<schema::Field::Builder> schema;
    // Schema for the field.  Initialized when getSchema() is first called.

    schema::Node::Builder node;
    schema::Node::SourceInfo::Builder sourceInfo;
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

    inline explicit MemberInfo(schema::Node::Builder node,
                               schema::Node::SourceInfo::Builder sourceInfo)
        : parent(nullptr), codeOrder(0), isInUnion(false), node(node), sourceInfo(sourceInfo),
          unionScope(nullptr) {}
    inline MemberInfo(MemberInfo& parent, uint codeOrder,
                      const Declaration::Reader& decl,
                      StructLayout::StructOrGroup& fieldScope,
                      bool isInUnion)
        : parent(&parent), codeOrder(codeOrder), isInUnion(isInUnion),
          name(decl.getName().getValue()), declId(decl.getId()), declKind(Declaration::FIELD),
          declAnnotations(decl.getAnnotations()),
          startByte(decl.getStartByte()), endByte(decl.getEndByte()),
          node(nullptr), sourceInfo(nullptr), fieldScope(&fieldScope) {
      KJ_REQUIRE(decl.which() == Declaration::FIELD);
      auto fieldDecl = decl.getField();
      fieldType = fieldDecl.getType();
      if (fieldDecl.getDefaultValue().isValue()) {
        hasDefaultValue = true;
        fieldDefaultValue = fieldDecl.getDefaultValue().getValue();
      }
      if (decl.hasDocComment()) {
        docComment = decl.getDocComment();
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
          node(nullptr), sourceInfo(nullptr), fieldScope(&fieldScope) {
      fieldType = decl.getType();
      if (decl.getDefaultValue().isValue()) {
        hasDefaultValue = true;
        fieldDefaultValue = decl.getDefaultValue().getValue();
      }
    }
    inline MemberInfo(MemberInfo& parent, uint codeOrder,
                      const Declaration::Reader& decl,
                      NodeSourceInfoBuilderPair builderPair,
                      bool isInUnion)
        : parent(&parent), codeOrder(codeOrder), isInUnion(isInUnion),
          name(decl.getName().getValue()), declId(decl.getId()), declKind(decl.which()),
          declAnnotations(decl.getAnnotations()),
          startByte(decl.getStartByte()), endByte(decl.getEndByte()),
          node(builderPair.node), sourceInfo(builderPair.sourceInfo), unionScope(nullptr) {
      KJ_REQUIRE(decl.which() != Declaration::FIELD);
      if (decl.hasDocComment()) {
        docComment = decl.getDocComment();
      }
    }

    schema::Field::Builder getSchema() {
      KJ_IF_MAYBE(result, schema) {
        return *result;
      } else {
        index = parent->childInitializedCount;
        auto builderPair = parent->addMemberSchema();
        auto builder = builderPair.field;
        if (isInUnion) {
          builder.setDiscriminantValue(parent->unionDiscriminantCount++);
        }
        builder.setName(name);
        builder.setCodeOrder(codeOrder);

        KJ_IF_MAYBE(dc, docComment) {
          builderPair.sourceInfo.setDocComment(*dc);
        }

        schema = builder;
        return builder;
      }
    }

    FieldSourceInfoBuilderPair addMemberSchema() {
      // Get the schema builder for the child member at the given index.  This lazily/dynamically
      // builds the builder tree.

      KJ_REQUIRE(childInitializedCount < childCount);

      auto structNode = node.getStruct();
      if (!structNode.hasFields()) {
        if (parent != nullptr) {
          getSchema();  // Make sure field exists in parent once the first child is added.
        }
        FieldSourceInfoBuilderPair result {
          structNode.initFields(childCount)[childInitializedCount],
          sourceInfo.initMembers(childCount)[childInitializedCount]
        };
        ++childInitializedCount;
        return result;
      } else {
        FieldSourceInfoBuilderPair result {
          structNode.getFields()[childInitializedCount],
          sourceInfo.getMembers()[childInitializedCount]
        };
        ++childInitializedCount;
        return result;
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

        sourceInfo.setId(groupId);
        KJ_IF_MAYBE(dc, docComment) {
          sourceInfo.setDocComment(*dc);
        }
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

  NodeSourceInfoBuilderPair newGroupNode(schema::Node::Reader parent, kj::StringPtr name) {
    AuxNode aux {
      translator.orphanage.newOrphan<schema::Node>(),
      translator.orphanage.newOrphan<schema::Node::SourceInfo>()
    };
    auto node = aux.node.get();
    auto sourceInfo = aux.sourceInfo.get();

    // We'll set the ID and scope ID later.
    node.setDisplayName(kj::str(parent.getDisplayName(), '.', name));
    node.setDisplayNamePrefixLength(node.getDisplayName().size() - name.size());
    node.setIsGeneric(parent.getIsGeneric());
    node.initStruct().setIsGroup(true);

    // The remaining contents of node.struct will be filled in later.

    translator.groups.add(kj::mv(aux));
    return { node, sourceInfo };
  }

  void translateInternal(MemberInfo& root, schema::Node::Builder builder) {
    auto structBuilder = builder.initStruct();

    // Go through each member in ordinal order, building each member schema.
    DuplicateOrdinalDetector dupDetector(errorReporter);
    for (auto& entry: membersByOrdinal) {
      MemberInfo& member = *entry.second;

      // Make sure the exceptions added relating to
      // https://github.com/capnproto/capnproto/issues/344 identify the affected field.
      KJ_CONTEXT(member.name);

      if (member.declId.isOrdinal()) {
        dupDetector.check(member.declId.getOrdinal());
      }

      schema::Field::Builder fieldBuilder = member.getSchema();
      fieldBuilder.getOrdinal().setExplicit(entry.first);

      switch (member.declKind) {
        case Declaration::FIELD: {
          auto slot = fieldBuilder.initSlot();
          auto typeBuilder = slot.initType();
          if (translator.compileType(member.fieldType, typeBuilder, implicitMethodParams)) {
            if (member.hasDefaultValue) {
              if (member.isParam &&
                  member.fieldDefaultValue.isRelativeName() &&
                  member.fieldDefaultValue.getRelativeName().getValue() == "null") {
                // special case: parameter set null
                switch (typeBuilder.which()) {
                  case schema::Type::TEXT:
                  case schema::Type::DATA:
                  case schema::Type::LIST:
                  case schema::Type::STRUCT:
                  case schema::Type::INTERFACE:
                  case schema::Type::ANY_POINTER:
                    break;
                  default:
                    errorReporter.addErrorOn(member.fieldDefaultValue.getRelativeName(),
                        "Only pointer parameters can declare their default as 'null'.");
                    break;
                }
                translator.compileDefaultDefaultValue(typeBuilder, slot.initDefaultValue());
              } else {
                translator.compileBootstrapValue(member.fieldDefaultValue,
                                                 typeBuilder, slot.initDefaultValue());
              }
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

    for (auto& group: translator.groups) {
      auto groupBuilder = group.node.get().getStruct();
      groupBuilder.setDataWordCount(structBuilder.getDataWordCount());
      groupBuilder.setPointerCount(structBuilder.getPointerCount());
      groupBuilder.setPreferredListEncoding(structBuilder.getPreferredListEncoding());
    }
  }
};

void NodeTranslator::compileStruct(Void decl, List<Declaration>::Reader members,
                                   schema::Node::Builder builder) {
  StructTranslator(*this, ImplicitParams::none())
      .translate(decl, members, builder, sourceInfo.get());
}

// -------------------------------------------------------------------

void NodeTranslator::compileInterface(Declaration::Interface::Reader decl,
                                      List<Declaration>::Reader members,
                                      schema::Node::Builder builder) {
  auto interfaceBuilder = builder.initInterface();

  auto superclassesDecl = decl.getSuperclasses();
  auto superclassesBuilder = interfaceBuilder.initSuperclasses(superclassesDecl.size());
  for (uint i: kj::indices(superclassesDecl)) {
    auto superclass = superclassesDecl[i];

    KJ_IF_MAYBE(decl, compileDeclExpression(superclass, ImplicitParams::none())) {
      KJ_IF_MAYBE(kind, decl->getKind()) {
        if (*kind == Declaration::INTERFACE) {
          auto s = superclassesBuilder[i];
          s.setId(decl->getIdAndFillBrand([&]() { return s.initBrand(); }));
        } else {
          decl->addError(errorReporter, kj::str(
            "'", decl->toString(), "' is not an interface."));
        }
      } else {
        // A variable?
        decl->addError(errorReporter, kj::str(
            "'", decl->toString(), "' is an unbound generic parameter. Currently we don't support "
            "extending these."));
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
  auto sourceInfoList = sourceInfo.get().initMembers(methods.size());
  uint i = 0;
  DuplicateOrdinalDetector dupDetector(errorReporter);

  for (auto& entry: methods) {
    uint codeOrder = entry.second.first;
    Declaration::Reader methodDecl = entry.second.second;
    auto methodReader = methodDecl.getMethod();

    auto ordinalDecl = methodDecl.getId().getOrdinal();
    dupDetector.check(ordinalDecl);
    uint16_t ordinal = ordinalDecl.getValue();

    if (methodDecl.hasDocComment()) {
      sourceInfoList[i].setDocComment(methodDecl.getDocComment());
    }

    auto methodBuilder = list[i++];
    methodBuilder.setName(methodDecl.getName().getValue());
    methodBuilder.setCodeOrder(codeOrder);

    auto implicits = methodDecl.getParameters();
    auto implicitsBuilder = methodBuilder.initImplicitParameters(implicits.size());
    for (auto i: kj::indices(implicits)) {
      implicitsBuilder[i].setName(implicits[i].getName());
    }

    auto params = methodReader.getParams();
    if (params.isStream()) {
      errorReporter.addErrorOn(params, "'stream' can only appear after '->', not before.");
    }
    methodBuilder.setParamStructType(compileParamList(
        methodDecl.getName().getValue(), ordinal, false,
        params, implicits,
        [&]() { return methodBuilder.initParamBrand(); }));

    auto results = methodReader.getResults();
    Declaration::ParamList::Reader resultList;
    if (results.isExplicit()) {
      resultList = results.getExplicit();
    } else {
      // We just stick with `resultList` uninitialized, which is equivalent to the default
      // instance. This works because `namedList` is the default kind of ParamList, and it will
      // default to an empty list.
    }
    methodBuilder.setResultStructType(compileParamList(
        methodDecl.getName().getValue(), ordinal, true,
        resultList, implicits,
        [&]() { return methodBuilder.initResultBrand(); }));

    methodBuilder.adoptAnnotations(compileAnnotationApplications(
        methodDecl.getAnnotations(), "targetsMethod"));
  }
}

template <typename InitBrandFunc>
uint64_t NodeTranslator::compileParamList(
    kj::StringPtr methodName, uint16_t ordinal, bool isResults,
    Declaration::ParamList::Reader paramList,
    typename List<Declaration::BrandParameter>::Reader implicitParams,
    InitBrandFunc&& initBrand) {
  switch (paramList.which()) {
    case Declaration::ParamList::NAMED_LIST: {
      auto newStruct = orphanage.newOrphan<schema::Node>();
      auto newSourceInfo = orphanage.newOrphan<schema::Node::SourceInfo>();
      auto builder = newStruct.get();
      auto parent = wipNode.getReader();

      kj::String typeName = kj::str(methodName, isResults ? "$Results" : "$Params");

      builder.setId(generateMethodParamsId(parent.getId(), ordinal, isResults));
      builder.setDisplayName(kj::str(parent.getDisplayName(), '.', typeName));
      builder.setDisplayNamePrefixLength(builder.getDisplayName().size() - typeName.size());
      builder.setIsGeneric(parent.getIsGeneric() || implicitParams.size() > 0);
      builder.setScopeId(0);  // detached struct type

      builder.initStruct();

      // Note that the struct we create here has a brand parameter list mirrioring the method's
      // implicit parameter list. Of course, fields inside the struct using the method's implicit
      // params as types actually need to refer to them as regular params, so we create an
      // ImplicitParams with a scopeId here.
      StructTranslator(*this, ImplicitParams { builder.getId(), implicitParams })
          .translate(paramList.getNamedList(), builder, newSourceInfo.get());
      uint64_t id = builder.getId();
      paramStructs.add(AuxNode { kj::mv(newStruct), kj::mv(newSourceInfo) });

      auto brand = localBrand->push(builder.getId(), implicitParams.size());

      if (implicitParams.size() > 0) {
        auto implicitDecls = kj::heapArrayBuilder<BrandedDecl>(implicitParams.size());
        auto implicitBuilder = builder.initParameters(implicitParams.size());

        for (auto i: kj::indices(implicitParams)) {
          auto param = implicitParams[i];
          implicitDecls.add(BrandedDecl::implicitMethodParam(i));
          implicitBuilder[i].setName(param.getName());
        }

        brand->setParams(implicitDecls.finish(), Declaration::STRUCT, Expression::Reader());
      }

      brand->compile(initBrand);
      return id;
    }
    case Declaration::ParamList::TYPE:
      KJ_IF_MAYBE(target, compileDeclExpression(
          paramList.getType(), ImplicitParams { 0, implicitParams })) {
        KJ_IF_MAYBE(kind, target->getKind()) {
          if (*kind == Declaration::STRUCT) {
            return target->getIdAndFillBrand(kj::fwd<InitBrandFunc>(initBrand));
          } else {
            errorReporter.addErrorOn(
                paramList.getType(),
                kj::str("'", expressionString(paramList.getType()), "' is not a struct type."));
          }
        } else {
          // A variable?
          target->addError(errorReporter,
              "Cannot use generic parameter as whole input or output of a method. Instead, "
              "use a parameter/result list containing a field with this type.");
          return 0;
        }
      }
      return 0;
    case Declaration::ParamList::STREAM:
      KJ_IF_MAYBE(streamCapnp, resolver.resolveImport("/capnp/stream.capnp")) {
        if (streamCapnp->resolver->resolveMember("StreamResult") == nullptr) {
          errorReporter.addErrorOn(paramList,
              "The version of '/capnp/stream.capnp' found in your import path does not appear "
              "to be the official one; it is missing the declaration of StreamResult.");
        }
      } else {
        errorReporter.addErrorOn(paramList,
            "A method declaration uses streaming, but '/capnp/stream.capnp' is not found "
            "in the import path. This is a standard file that should always be installed "
            "with the Cap'n Proto compiler.");
      }
      return typeId<StreamResult>();
  }
  KJ_UNREACHABLE;
}

// -------------------------------------------------------------------

kj::Maybe<BrandedDecl>
NodeTranslator::compileDeclExpression(
    Expression::Reader source, ImplicitParams implicitMethodParams) {
  return localBrand->compileDeclExpression(source, resolver, implicitMethodParams);
}

/* static */ kj::Maybe<Resolver::ResolveResult> NodeTranslator::compileDecl(
    uint64_t scopeId, uint scopeParameterCount, Resolver& resolver, ErrorReporter& errorReporter,
    Expression::Reader expression, schema::Brand::Builder brandBuilder) {
  auto scope = kj::refcounted<BrandScope>(errorReporter, scopeId, scopeParameterCount, resolver);
  KJ_IF_MAYBE(decl, scope->compileDeclExpression(expression, resolver, ImplicitParams::none())) {
    return decl->asResolveResult(scope->getScopeId(), brandBuilder);
  } else {
    return nullptr;
  }
}

bool NodeTranslator::compileType(Expression::Reader source, schema::Type::Builder target,
                                 ImplicitParams implicitMethodParams) {
  KJ_IF_MAYBE(decl, compileDeclExpression(source, implicitMethodParams)) {
    return decl->compileAsType(errorReporter, target);
  } else {
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

void NodeTranslator::compileBootstrapValue(
    Expression::Reader source, schema::Type::Reader type, schema::Value::Builder target,
    kj::Maybe<Schema> typeScope) {
  // Start by filling in a default default value so that if for whatever reason we don't end up
  // initializing the value, this won't cause schema validation to fail.
  compileDefaultDefaultValue(type, target);

  switch (type.which()) {
    case schema::Type::LIST:
    case schema::Type::STRUCT:
    case schema::Type::INTERFACE:
    case schema::Type::ANY_POINTER:
      unfinishedValues.add(UnfinishedValue { source, type, typeScope, target });
      break;

    default:
      // Primitive value. (Note that the scope can't possibly matter since primitives are not
      // generic.)
      compileValue(source, type, typeScope.orDefault(Schema()), target, true);
      break;
  }
}

void NodeTranslator::compileValue(Expression::Reader source, schema::Type::Reader type,
                                  Schema typeScope, schema::Value::Builder target,
                                  bool isBootstrap) {
  class ResolverGlue: public ValueTranslator::Resolver {
  public:
    inline ResolverGlue(NodeTranslator& translator, bool isBootstrap)
        : translator(translator), isBootstrap(isBootstrap) {}

    kj::Maybe<DynamicValue::Reader> resolveConstant(Expression::Reader name) override {
      return translator.readConstant(name, isBootstrap);
    }

    kj::Maybe<kj::Array<const byte>> readEmbed(LocatedText::Reader filename) override {
      return translator.readEmbed(filename);
    }

  private:
    NodeTranslator& translator;
    bool isBootstrap;
  };

  ResolverGlue glue(*this, isBootstrap);
  ValueTranslator valueTranslator(glue, errorReporter, orphanage);

  KJ_IF_MAYBE(typeSchema, resolver.resolveBootstrapType(type, typeScope)) {
    kj::StringPtr fieldName = Schema::from<schema::Type>()
        .getUnionFields()[static_cast<uint>(typeSchema->which())].getProto().getName();

    KJ_IF_MAYBE(value, valueTranslator.compileValue(source, *typeSchema)) {
      if (typeSchema->isEnum()) {
        target.setEnum(value->getReader().as<DynamicEnum>().getRaw());
      } else {
        toDynamic(target).adopt(fieldName, kj::mv(*value));
      }
    }
  }
}

kj::Maybe<Orphan<DynamicValue>> ValueTranslator::compileValue(Expression::Reader src, Type type) {
  if (type.isAnyPointer()) {
    if (type.getBrandParameter() != nullptr || type.getImplicitParameter() != nullptr) {
      errorReporter.addErrorOn(src,
          "Cannot interpret value because the type is a generic type parameter which is not "
          "yet bound. We don't know what type to expect here.");
      return nullptr;
    }
  }

  Orphan<DynamicValue> result = compileValueInner(src, type);

  if (result.getType() == DynamicValue::UNKNOWN) {
    // Error already reported.
    return nullptr;
  } else if (matchesType(src, type, result)) {
    return kj::mv(result);
  } else {
    // If the expected type is a struct, we try matching its first field.
    if (type.isStruct()) {
      auto structType = type.asStruct();
      auto fields = structType.getFields();
      if (fields.size() > 0) {
        auto field = fields[0];
        if (matchesType(src, field.getType(), result)) {
          // Success. Wrap in a struct.
          auto outer = orphanage.newOrphan(type.asStruct());
          outer.get().adopt(field, kj::mv(result));
          return Orphan<DynamicValue>(kj::mv(outer));
        }
      }
    }

    // That didn't work, so this is just a type mismatch.
    errorReporter.addErrorOn(src, kj::str("Type mismatch; expected ", makeTypeName(type), "."));
    return nullptr;
  }
}

bool ValueTranslator::matchesType(Expression::Reader src, Type type, Orphan<DynamicValue>& result) {
  // compileValueInner() evaluated `src` and only used `type` as a hint in interpreting `src` if
  // `src`'s type wasn't already obvious. So, now we need to check that the resulting value
  // actually matches `type`.

  switch (result.getType()) {
    case DynamicValue::UNKNOWN:
      KJ_UNREACHABLE;

    case DynamicValue::VOID:
      return type.isVoid();

    case DynamicValue::BOOL:
      return type.isBool();

    case DynamicValue::INT: {
      int64_t value = result.getReader().as<int64_t>();
      if (value < 0) {
        int64_t minValue;
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

          default: return false;
        }

        if (value < minValue) {
          errorReporter.addErrorOn(src, "Integer value out of range.");
          result = minValue;
        }
        return true;
      }

    } KJ_FALLTHROUGH;  // value is positive, so we can just go on to the uint case below.

    case DynamicValue::UINT: {
      uint64_t maxValue;
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

        default: return false;
      }

      if (result.getReader().as<uint64_t>() > maxValue) {
        errorReporter.addErrorOn(src, "Integer value out of range.");
        result = maxValue;
      }
      return true;
    }

    case DynamicValue::FLOAT:
      return type.isFloat32() || type.isFloat64();

    case DynamicValue::TEXT:
      return type.isText();

    case DynamicValue::DATA:
      return type.isData();

    case DynamicValue::LIST:
      if (type.isList()) {
        return result.getReader().as<DynamicList>().getSchema() == type.asList();
      } else if (type.isAnyPointer()) {
        switch (type.whichAnyPointerKind()) {
          case schema::Type::AnyPointer::Unconstrained::ANY_KIND:
          case schema::Type::AnyPointer::Unconstrained::LIST:
            return true;
          case schema::Type::AnyPointer::Unconstrained::STRUCT:
          case schema::Type::AnyPointer::Unconstrained::CAPABILITY:
            return false;
        }
        KJ_UNREACHABLE;
      } else {
        return false;
      }

    case DynamicValue::ENUM:
      return type.isEnum() &&
             result.getReader().as<DynamicEnum>().getSchema() == type.asEnum();

    case DynamicValue::STRUCT:
      if (type.isStruct()) {
        return result.getReader().as<DynamicStruct>().getSchema() == type.asStruct();
      } else if (type.isAnyPointer()) {
        switch (type.whichAnyPointerKind()) {
          case schema::Type::AnyPointer::Unconstrained::ANY_KIND:
          case schema::Type::AnyPointer::Unconstrained::STRUCT:
            return true;
          case schema::Type::AnyPointer::Unconstrained::LIST:
          case schema::Type::AnyPointer::Unconstrained::CAPABILITY:
            return false;
        }
        KJ_UNREACHABLE;
      } else {
        return false;
      }

    case DynamicValue::CAPABILITY:
      KJ_FAIL_ASSERT("Interfaces can't have literal values.");

    case DynamicValue::ANY_POINTER:
      KJ_FAIL_ASSERT("AnyPointers can't have literal values.");
  }

  KJ_UNREACHABLE;
}

Orphan<DynamicValue> ValueTranslator::compileValueInner(Expression::Reader src, Type type) {
  switch (src.which()) {
    case Expression::RELATIVE_NAME: {
      auto name = src.getRelativeName();

      // The name is just a bare identifier.  It may be a literal value or an enumerant.
      kj::StringPtr id = name.getValue();

      if (type.isEnum()) {
        KJ_IF_MAYBE(enumerant, type.asEnum().findEnumerantByName(id)) {
          return DynamicEnum(*enumerant);
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

      // Apparently not a literal. Try resolving it.
      KJ_IF_MAYBE(constValue, resolver.resolveConstant(src)) {
        return orphanage.newOrphanCopy(*constValue);
      } else {
        return nullptr;
      }
    }

    case Expression::ABSOLUTE_NAME:
    case Expression::IMPORT:
    case Expression::APPLICATION:
    case Expression::MEMBER:
      KJ_IF_MAYBE(constValue, resolver.resolveConstant(src)) {
        return orphanage.newOrphanCopy(*constValue);
      } else {
        return nullptr;
      }

    case Expression::EMBED:
      KJ_IF_MAYBE(data, resolver.readEmbed(src.getEmbed())) {
        switch (type.which()) {
          case schema::Type::TEXT: {
            // Sadly, we need to make a copy to add the NUL terminator.
            auto text = orphanage.newOrphan<Text>(data->size());
            memcpy(text.get().begin(), data->begin(), data->size());
            return kj::mv(text);
          }
          case schema::Type::DATA:
            // TODO(perf): It would arguably be neat to use orphanage.referenceExternalData(),
            //   since typically the data is mmap()ed and this would avoid forcing a large file
            //   to become memory-resident. However, we'd have to figure out who should own the
            //   Array<byte>. Also, we'd have to deal with the possibility of misaligned data --
            //   though arguably in that case we know it's not mmap()ed so whatever. One more
            //   thing: it would be neat to be able to reference text blobs this way too, if only
            //   we could rely on the assumption that as long as the data doesn't end on a page
            //   boundary, it will be zero-padded, thus giving us our NUL terminator (4095/4096 of
            //   the time), but this seems to require documenting constraints on the underlying
            //   file-reading interfaces. Hm.
            return orphanage.newOrphanCopy(Data::Reader(*data));
          case schema::Type::STRUCT: {
            // We will almost certainly
            if (data->size() % sizeof(word) != 0) {
              errorReporter.addErrorOn(src,
                  "Embedded file is not a valid Cap'n Proto message.");
              return nullptr;
            }
            kj::Array<word> copy;
            kj::ArrayPtr<const word> words;
            if (reinterpret_cast<uintptr_t>(data->begin()) % sizeof(void*) == 0) {
              // Hooray, data is aligned.
              words = kj::ArrayPtr<const word>(
                  reinterpret_cast<const word*>(data->begin()),
                  data->size() / sizeof(word));
            } else {
              // Ugh, data not aligned. Make a copy.
              copy = kj::heapArray<word>(data->size() / sizeof(word));
              memcpy(copy.begin(), data->begin(), data->size());
              words = copy;
            }
            ReaderOptions options;
            options.traversalLimitInWords = kj::maxValue;
            options.nestingLimit = kj::maxValue;
            FlatArrayMessageReader reader(words, options);
            return orphanage.newOrphanCopy(reader.getRoot<DynamicStruct>(type.asStruct()));
          }
          default:
            errorReporter.addErrorOn(src,
                "Embeds can only be used when Text, Data, or a struct is expected.");
            return nullptr;
        }
      } else {
        return nullptr;
      }

    case Expression::POSITIVE_INT:
      return src.getPositiveInt();

    case Expression::NEGATIVE_INT: {
      uint64_t nValue = src.getNegativeInt();
      if (nValue > ((uint64_t)kj::maxValue >> 1) + 1) {
        errorReporter.addErrorOn(src, "Integer is too big to be negative.");
        return nullptr;
      } else {
        return kj::implicitCast<int64_t>(-nValue);
      }
    }

    case Expression::FLOAT:
      return src.getFloat();
      break;

    case Expression::STRING:
      if (type.isData()) {
        Text::Reader text = src.getString();
        return orphanage.newOrphanCopy(Data::Reader(text.asBytes()));
      } else {
        return orphanage.newOrphanCopy(src.getString());
      }
      break;

    case Expression::BINARY:
      if (!type.isData()) {
        errorReporter.addErrorOn(src, kj::str("Type mismatch; expected ", makeTypeName(type), "."));
        return nullptr;
      }
      return orphanage.newOrphanCopy(src.getBinary());

    case Expression::LIST: {
      if (!type.isList()) {
        errorReporter.addErrorOn(src, kj::str("Type mismatch; expected ", makeTypeName(type), "."));
        return nullptr;
      }
      auto listSchema = type.asList();
      Type elementType = listSchema.getElementType();
      auto srcList = src.getList();
      Orphan<DynamicList> result = orphanage.newOrphan(listSchema, srcList.size());
      auto dstList = result.get();
      for (uint i = 0; i < srcList.size(); i++) {
        KJ_IF_MAYBE(value, compileValue(srcList[i], elementType)) {
          dstList.adopt(i, kj::mv(*value));
        }
      }
      return kj::mv(result);
    }

    case Expression::TUPLE: {
      if (!type.isStruct()) {
        errorReporter.addErrorOn(src, kj::str("Type mismatch; expected ", makeTypeName(type), "."));
        return nullptr;
      }
      auto structSchema = type.asStruct();
      Orphan<DynamicStruct> result = orphanage.newOrphan(structSchema);
      fillStructValue(result.get(), src.getTuple());
      return kj::mv(result);
    }

    case Expression::UNKNOWN:
      // Ignore earlier error.
      return nullptr;
  }

  KJ_UNREACHABLE;
}

void ValueTranslator::fillStructValue(DynamicStruct::Builder builder,
                                      List<Expression::Param>::Reader assignments) {
  for (auto assignment: assignments) {
    if (assignment.isNamed()) {
      auto fieldName = assignment.getNamed();
      KJ_IF_MAYBE(field, builder.getSchema().findFieldByName(fieldName.getValue())) {
        auto fieldProto = field->getProto();
        auto value = assignment.getValue();

        switch (fieldProto.which()) {
          case schema::Field::SLOT:
            KJ_IF_MAYBE(compiledValue, compileValue(value, field->getType())) {
              builder.adopt(*field, kj::mv(*compiledValue));
            }
            break;

          case schema::Field::GROUP:
            auto groupBuilder = builder.init(*field).as<DynamicStruct>();
            if (value.isTuple()) {
              fillStructValue(groupBuilder, value.getTuple());
            } else {
              auto groupFields = groupBuilder.getSchema().getFields();
              if (groupFields.size() > 0) {
                auto groupField = groupFields[0];

                // Call compileValueInner() using the group's type as `type`. Since we already
                // established `value` is not a tuple, this will only return a valid result if
                // the value has unambiguous type.
                auto result = compileValueInner(value, field->getType());

                // Does it match the first field?
                if (matchesType(value, groupField.getType(), result)) {
                  groupBuilder.adopt(groupField, kj::mv(result));
                  break;
                }
              }

              errorReporter.addErrorOn(value, "Type mismatch; expected group.");
            }
            break;
        }
      } else {
        errorReporter.addErrorOn(fieldName, kj::str(
            "Struct has no field named '", fieldName.getValue(), "'."));
      }
    } else {
      errorReporter.addErrorOn(assignment.getValue(), kj::str("Missing field name."));
    }
  }
}

kj::String ValueTranslator::makeNodeName(Schema schema) {
  schema::Node::Reader proto = schema.getProto();
  return kj::str(proto.getDisplayName().slice(proto.getDisplayNamePrefixLength()));
}

kj::String ValueTranslator::makeTypeName(Type type) {
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
      return kj::str("List(", makeTypeName(type.asList().getElementType()), ")");
    case schema::Type::ENUM: return makeNodeName(type.asEnum());
    case schema::Type::STRUCT: return makeNodeName(type.asStruct());
    case schema::Type::INTERFACE: return makeNodeName(type.asInterface());
    case schema::Type::ANY_POINTER: return kj::str("AnyPointer");
  }
  KJ_UNREACHABLE;
}

kj::Maybe<DynamicValue::Reader> NodeTranslator::readConstant(
    Expression::Reader source, bool isBootstrap) {
  // Look up the constant decl.
  BrandedDecl constDecl = nullptr;
  KJ_IF_MAYBE(decl, compileDeclExpression(source, ImplicitParams::none())) {
    constDecl = *decl;
  } else {
    // Lookup will have reported an error.
    return nullptr;
  }

  // Is it a constant?
  if(constDecl.getKind().orDefault(Declaration::FILE) != Declaration::CONST) {
    errorReporter.addErrorOn(source,
        kj::str("'", expressionString(source), "' does not refer to a constant."));
    return nullptr;
  }

  // Extract the ID and brand.
  MallocMessageBuilder builder(256);
  auto constBrand = builder.getRoot<schema::Brand>();
  uint64_t id = constDecl.getIdAndFillBrand([&]() { return constBrand; });

  // Look up the schema -- we'll need this to compile the constant's type.
  Schema constSchema;
  KJ_IF_MAYBE(s, resolver.resolveBootstrapSchema(id, constBrand)) {
    constSchema = *s;
  } else {
    // The constant's schema is broken for reasons already reported.
    return nullptr;
  }

  // If we're bootstrapping, then we know we're expecting a primitive value, so if the
  // constant turns out to be non-primitive, we'll error out anyway.  If we're not
  // bootstrapping, we may be compiling a non-primitive value and so we need the final
  // version of the constant to make sure its value is filled in.
  schema::Node::Reader proto = constSchema.getProto();
  if (!isBootstrap) {
    KJ_IF_MAYBE(finalProto, resolver.resolveFinalSchema(id)) {
      proto = *finalProto;
    } else {
      // The constant's final schema is broken for reasons already reported.
      return nullptr;
    }
  }

  auto constReader = proto.getConst();
  auto dynamicConst = toDynamic(constReader.getValue());
  auto constValue = dynamicConst.get(KJ_ASSERT_NONNULL(dynamicConst.which()));

  if (constValue.getType() == DynamicValue::ANY_POINTER) {
    // We need to assign an appropriate schema to this pointer.
    AnyPointer::Reader objValue = constValue.as<AnyPointer>();

    auto constType = constSchema.asConst().getType();
    switch (constType.which()) {
      case schema::Type::STRUCT:
        constValue = objValue.getAs<DynamicStruct>(constType.asStruct());
        break;
      case schema::Type::LIST:
        constValue = objValue.getAs<DynamicList>(constType.asList());
        break;
      case schema::Type::ANY_POINTER:
        // Fine as-is.
        break;
      default:
        KJ_FAIL_ASSERT("Unrecognized AnyPointer-typed member of schema::Value.");
        break;
    }
  }

  if (source.isRelativeName()) {
    // A fully unqualified identifier looks like it might refer to a constant visible in the
    // current scope, but if that's really what the user wanted, we want them to use a
    // qualified name to make it more obvious.  Report an error.
    KJ_IF_MAYBE(scope, resolver.resolveBootstrapSchema(proto.getScopeId(),
                                                       schema::Brand::Reader())) {
      auto scopeReader = scope->getProto();
      kj::StringPtr parent;
      if (scopeReader.isFile()) {
        parent = "";
      } else {
        parent = scopeReader.getDisplayName().slice(scopeReader.getDisplayNamePrefixLength());
      }
      kj::StringPtr id = source.getRelativeName().getValue();

      errorReporter.addErrorOn(source, kj::str(
          "Constant names must be qualified to avoid confusion.  Please replace '",
          expressionString(source), "' with '", parent, ".", id,
          "', if that's what you intended."));
    }
  }

  return constValue;
}

kj::Maybe<kj::Array<const byte>> NodeTranslator::readEmbed(LocatedText::Reader filename) {
  KJ_IF_MAYBE(data, resolver.readEmbed(filename.getValue())) {
    return kj::mv(*data);
  } else {
    errorReporter.addErrorOn(filename,
        kj::str("Couldn't read file for embed: ", filename.getValue()));
    return nullptr;
  }
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
    KJ_IF_MAYBE(decl, compileDeclExpression(name, ImplicitParams::none())) {
      KJ_IF_MAYBE(kind, decl->getKind()) {
        if (*kind != Declaration::ANNOTATION) {
          errorReporter.addErrorOn(name, kj::str(
              "'", expressionString(name), "' is not an annotation."));
        } else {
          annotationBuilder.setId(decl->getIdAndFillBrand(
              [&]() { return annotationBuilder.initBrand(); }));
          KJ_IF_MAYBE(annotationSchema,
                      resolver.resolveBootstrapSchema(annotationBuilder.getId(),
                                                      annotationBuilder.getBrand())) {
            auto node = annotationSchema->getProto().getAnnotation();
            if (!toDynamic(node).get(targetsFlagName).as<bool>()) {
              errorReporter.addErrorOn(name, kj::str(
                  "'", expressionString(name), "' cannot be applied to this kind of declaration."));
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
                      "'", expressionString(name), "' requires a value."));
                  compileDefaultDefaultValue(node.getType(), annotationBuilder.getValue());
                }
                break;

              case Declaration::AnnotationApplication::Value::EXPRESSION:
                compileBootstrapValue(value.getExpression(), node.getType(),
                                      annotationBuilder.getValue(),
                                      *annotationSchema);
                break;
            }
          }
        }
      } else {
        errorReporter.addErrorOn(name, kj::str(
            "'", expressionString(name), "' is not an annotation."));
      }
    }
  }

  return result;
}

}  // namespace compiler
}  // namespace capnp
