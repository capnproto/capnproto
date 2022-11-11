#include "buffering.h"
#include <kj/debug.h>

namespace kj {

namespace _ {

static inline uint lg(uint value) {
  // Compute floor(log2(value)).
  //
  // Undefined for value = 0.
#if _MSC_VER && !defined(__clang__)
  unsigned long i;
  auto found = _BitScanReverse(&i, value);
  KJ_DASSERT(found);  // !found means value = 0
  return i;
#else
  return sizeof(uint) * 8 - 1 - __builtin_clz(value);
#endif
}

FdRangeInputBufferBase::FdRangeInputBufferBase(FdRangeInputBufferOptions options)
    : options(options),
      allBytes(heapArray<byte>(options.size)), fds(heapArray<AutoCloseFd>(options.maxFds)) {}

ArrayPtr<byte> FdRangeInputBufferBase::getByteRange() {
  KJ_SWITCH_ONEOF(state) {
    KJ_CASE_ONEOF(_, Empty) {
      return ArrayPtr<byte>(allBytes.begin(), size_t(0));
    }
    KJ_CASE_ONEOF(range, FdRange) {
      return range.bytes;
    }
    KJ_CASE_ONEOF(ranges, TwoFdRange) {
      return ranges.bytes;
    }
  }
  KJ_UNREACHABLE;
}

void FdRangeInputBufferBase::setByteRange(ArrayPtr<byte> bytes) {
  KJ_SWITCH_ONEOF(state) {
    KJ_CASE_ONEOF(_, Empty) {
      KJ_FAIL_ASSERT("buffer is empty");
    }
    KJ_CASE_ONEOF(range, FdRange) {
      range.bytes = bytes;
    }
    KJ_CASE_ONEOF(ranges, TwoFdRange) {
      ranges.bytes = bytes;
    }
  }
}

bool FdRangeInputBufferBase::overflowed() { return allBytes.size() > options.size; }

FdRangeInputBufferBase::ReadParams FdRangeInputBufferBase::prepareForRead(size_t min) {
  auto nextPowerOf2 = [](size_t n) {
    // Round up to the next power of 2.
    size_t result = 1u << (_::lg(n) + 1);
    KJ_DASSERT(result > n);
    KJ_DASSERT(result <= n * 2);
    return result;
  };

  auto bytes = getByteRange();

  auto spaceNeeded = bytes.size() + min;

  if (spaceNeeded > allBytes.size()) {
    // We need an overflow buffer. Allocate one (sized to the next power of 2 after spaceNeeded),
    // copy our buffer into it, and empty the state of our old parent buffer.

    auto overflow = heapArray<byte>(nextPowerOf2(spaceNeeded));
    memcpy(overflow.begin(), bytes.begin(), bytes.size());
    bytes = ArrayPtr<byte>(overflow.begin(), bytes.size());
    setByteRange(bytes);

    allBytes = mv(overflow);
  } else if (bytes.begin() != allBytes.begin()) {
    // Move range to front of buffer before reading
    memmove(allBytes.begin(), bytes.begin(), bytes.size());
    bytes = ArrayPtr<byte>(allBytes.begin(), bytes.size());
    setByteRange(bytes);
  }

  size_t max = min;
  if (allBytes.size() == options.size) {
    // Optimization: allow reading up to the end of the parent buffer
    // when it is not an overflow buffer.
    max = allBytes.size() - bytes.size();
  }

  return ReadParams{ bytes.end(), max };
}

void FdRangeInputBufferBase::handleReadResult(size_t byteCount, Array<AutoCloseFd> fds) {
  KJ_SWITCH_ONEOF(state) {
    KJ_CASE_ONEOF(_, FdRangeInputBufferBase::Empty) {
      state = FdRangeInputBufferBase::FdRange {
          ArrayPtr<byte>(allBytes.begin(), byteCount), mv(fds) };
    }
    KJ_CASE_ONEOF(range, FdRangeInputBufferBase::FdRange) {
      KJ_DASSERT(range.bytes.begin() = allBytes.begin());
      if (range.fds.size()) {
        // The current range already has some FDs.
        // We need to handle this by treating the extra bytes as part of a new FD range.
        auto two = FdRangeInputBufferBase::FdRange {
            ArrayPtr<byte>(range.bytes.end(), byteCount), mv(fds) };

        state = FdRangeInputBufferBase::TwoFdRange {
          ArrayPtr<byte>(range.bytes.begin(), range.bytes.size() + byteCount),
          mv(range.fds),
          mv(fds),
          range.bytes.size()
        };
      } else {
        // We can treat this read as an extension of the existing FD range.
        state = FdRangeInputBufferBase::FdRange {
            ArrayPtr<byte>(range.bytes.begin(), range.bytes.size() + byteCount),
            mv(fds) };
      }
    }
    KJ_CASE_ONEOF(ranges, FdRangeInputBufferBase::TwoFdRange) {
      KJ_DASSERT(ranges.bytes.begin() = allBytes.begin());
      KJ_REQUIRE(ranges.twoFds.size() == 0, "Cannot buffer more than two FD ranges at once.");

      ranges.bytes = ArrayPtr<byte>(ranges.bytes.begin(), ranges.bytes.size() + byteCount);
      ranges.twoFds = mv(fds);
    }
  }
}

Own<RetainableRange> FdRangeInputBufferBase::split(size_t splitAt) {
  auto bytes = getByteRange();
  auto newSize = bytes.size() - splitAt;

  Own<RetainableRange> result;
  if (overflowed()) {
    // This is an overflow buffer, so when we call `split` it's expected that we split out
    // the entire contents of the buffer, and the buffer has a single FD range in it.

    KJ_REQUIRE(newSize == 0,
        "Attempted to split() an overflow buffer such that leftover bytes would be left inside.");
    result = refcounted<RetainableRange>(Badge<FdRangeInputBufferBase>(),
        mv(allBytes), ArrayPtr<byte>(bytes.begin(), splitAt), nullptr);
    allBytes = heapArray<byte>(options.size);
  } else {
    result = refcounted<RetainableRange>(Badge<FdRangeInputBufferBase>(),
        *this, ArrayPtr<byte>(bytes.begin(), splitAt), nullptr);
  }

  auto newFront = bytes.begin() + splitAt;
  bytes = ArrayPtr<byte>(newFront, newSize);

  KJ_SWITCH_ONEOF(state) {
    KJ_CASE_ONEOF(_, FdRangeInputBufferBase::Empty) {
      KJ_FAIL_ASSERT("Cannot call split() on empty range");
    }
    KJ_CASE_ONEOF(range, FdRangeInputBufferBase::FdRange) {
      if (newSize == 0) {
        result->fds = mv(range.fds);
        state = FdRangeInputBufferBase::Empty{};
      } else {
        range.bytes = bytes;
      }
    }
    KJ_CASE_ONEOF(ranges, FdRangeInputBufferBase::TwoFdRange) {
      if (newSize == 0) {
        KJ_REQUIRE(ranges.twoFds.size() == 0, "split() cannot include FDs from two ranges",
              ranges.bytes.size(), ranges.lengthOfFirstRange, ranges.oneFds.size(), ranges.twoFds.size());

        result->fds = mv(ranges.oneFds);
        state = FdRangeInputBufferBase::Empty{};
      } else if (splitAt >= ranges.lengthOfFirstRange) {
        result->fds = mv(ranges.oneFds);
        state = FdRangeInputBufferBase::FdRange { bytes, mv(ranges.twoFds) };
      } else {
        ranges.bytes = bytes;
        ranges.lengthOfFirstRange -= splitAt;
      }
    }
  }
  return mv(result);
}

} // namespace _

RetainableRange::RetainableRange(Badge<_::FdRangeInputBufferBase>,
    _::FdRangeInputBufferBase& parent, ArrayPtr<byte> range, Array<AutoCloseFd> fds)
    : parentOrRetainedBytes(&parent),
      range(range),
      fds(mv(fds)) {}

RetainableRange::RetainableRange(Badge<_::FdRangeInputBufferBase>,
    Array<byte> ownBytes, ArrayPtr<byte> range, Array<AutoCloseFd> fds)
    : parentOrRetainedBytes(mv(ownBytes)),
      range(range),
      fds(mv(fds)) {}

void RetainableRange::retainLongTerm() {
  KJ_SWITCH_ONEOF(parentOrRetainedBytes) {
    KJ_CASE_ONEOF(parent, _::FdRangeInputBufferBase*) {
      Array<byte> ownBytes;
      if (range.size() > parent->allBytes.size() * parent->options.retainRatio) {
        // Take ownership of our parent's bytes, and allocate a new buffer for the parent.
        ownBytes = mv(parent->allBytes);

        // Note that at this point the FdRanges don't contain the retained range anymore,
        // as it was already split off. So we can directly copy the FdRanges to the beginning
        // of the newly allocated buffer.
        KJ_SWITCH_ONEOF(parent->state) {
          KJ_CASE_ONEOF(_, _::FdRangeInputBufferBase::Empty) {
            // Nothing to do here
          }
          KJ_CASE_ONEOF(range, _::FdRangeInputBufferBase::FdRange) {
            auto newParentBuffer = heapArray<byte>(ownBytes.size());
            memcpy(newParentBuffer.begin(), range.bytes.begin(), range.bytes.size());

            range.bytes = ArrayPtr<byte>(newParentBuffer.begin(), range.bytes.size());

            parent->allBytes = mv(newParentBuffer);
          }
          KJ_CASE_ONEOF(ranges, _::FdRangeInputBufferBase::TwoFdRange) {
            auto newParentBuffer = heapArray<byte>(ownBytes.size());
            memcpy(newParentBuffer.begin(), ranges.bytes.begin(), ranges.bytes.size());

            ranges.bytes = ArrayPtr<byte>(newParentBuffer.begin(), ranges.bytes.size());

            parent->allBytes = mv(newParentBuffer);
          }
        }
      } else {
        // Perform a copy from the parent buffer.
        ownBytes = heapArray<byte>(range.size());
        memcpy(ownBytes.begin(), range.begin(), range.size());
      }

      range = ownBytes;
      parentOrRetainedBytes = mv(ownBytes);
    }
    KJ_CASE_ONEOF(_, Array<byte>) {
      // Nothing to do here, already retained.
    }
  }
}

} // namespace kj
