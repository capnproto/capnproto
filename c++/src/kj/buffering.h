#pragma once

#include <kj/common.h>
#include <kj/async.h>
#include <kj/io.h>
#include <kj/async-io.h>
#include <kj/one-of.h>
#include <kj/debug.h>

KJ_BEGIN_HEADER

namespace kj {

// =======================================================================================
// Buffered reading helpers

struct FdRangeInputBufferOptions {
  size_t size;
  // Size of the buffer in bytes.

  size_t maxFds;
  // Max number of FDs allowed in a single underlying read.

  double retainRatio = 0.8;
  // When a `RetainableRange` occupies more than `retainRatio` of the buffer,
  // `retain()` will transfer ownership of the buffer and allocate a new one instead
  // of performing a copy.
};

struct EndOfFile{};
class RetainableRange;

namespace _ {

class FdRangeInputBufferBase {
protected:
  FdRangeInputBufferBase(FdRangeInputBufferOptions options);

  FdRangeInputBufferOptions options;
  Array<byte> allBytes;
  Array<AutoCloseFd> fds;

  struct Empty {
    bool eof = false;
  };
  struct FdRange {
    ArrayPtr<byte> bytes;
    Array<AutoCloseFd> fds;
  };
  struct TwoFdRange {
    ArrayPtr<byte> bytes;
    Array<AutoCloseFd> oneFds;
    Array<AutoCloseFd> twoFds;
    size_t lengthOfFirstRange;
  };
  OneOf<Empty, FdRange, TwoFdRange> state = Empty{};

  ArrayPtr<byte> getByteRange();
  void setByteRange(ArrayPtr<byte> bytes);
  bool overflowed();

  struct ReadParams {
    void* start;
    size_t max;
  };
  ReadParams prepareForRead(size_t min);
  void handleReadResult(size_t byteCount, Array<AutoCloseFd> fds);

  Own<RetainableRange> split(size_t splitAt);

  friend class ::kj::RetainableRange;
};

} // namespace _

template<typename Stream>
class FdRangeInputBuffer : _::FdRangeInputBufferBase {
  // A buffer of up to two FD ranges that can be filled by reading from a stream.
  //
  // An FD range is defined as a range of bytes with FDs associated with the last byte in the range.
  // FDs being associated with the last byte of the range is for the purpose of the buffering logic,
  // resulting from the rules of SCM_RIGHTS control messages where a read that includes FDs will not
  // read past the last byte written by the other side of the socket. One can infer from this rule,
  // that if the other side of the socket sent multiple messages, any FDs received are part of the
  // last message in the FD range.
  //
  // The buffer is emptied by splitting off prefixes of it. If a split-off prefix includes the last
  // byte of an FD range with FDs, those FDs are also split off.
  //
  // The limit of two buffered FD ranges results from the optimization described in `Handle::append`.
  // It's necessary to create a new FD range if the existing range already had FDs in it,
  // otherwise it's safe to extend the existing range.
  //
  // It is assumed that when two FD ranges are buffered, once FDs are associated with the second range,
  // then a complete "message" is present in some prefix of the buffer, and this prefix is >= the
  // size of the first FD range. The caller must split this prefix out of the buffer
  // before attempting to read any more bytes into the buffer.

public:
  KJ_DISALLOW_COPY(FdRangeInputBuffer);

  FdRangeInputBuffer(Stream& stream, FdRangeInputBufferOptions options);

  class Handle final {
    // A handle to access a non-empty `FdRangeInputBuffer`
    // Prefixes of the buffer can be split off and retained for long-term usage.

  public:
    inline ArrayPtr<byte> peek() { return parent.getByteRange(); }

    inline Own<RetainableRange> split(size_t splitAt) { return parent.split(splitAt); }
    // Splits off the range before splitAt and returns it.
    // The returned range is consumed from the buffer, such that it will not be viewable through the
    // Handle again.
    //
    // Splitting such that FDs from two underlying reads would be included is an error.

    inline Promise<size_t> append(size_t min) { return parent.tryRead(min); }
    // Append at least `min` bytes to the end of the range.
    // Returns the actual # of bytes read, or 0 on EOF.
    //
    // If the current range + `min` does not fit in the buffer,
    // a new buffer (owned by this range) will be allocated.
    //
    // As an optimization, if the current range + `min` DOES fit in the buffer AND the buffer has not
    // overflowed, the underlying `read` call will allow reading up until the end of the buffer
    // (i.e more than `min`).

  protected:
    FdRangeInputBuffer& parent;

    Handle(FdRangeInputBuffer& parent) : parent(parent) {};
    friend class FdRangeInputBuffer;
  };

  OneOf<EndOfFile, Promise<void>, Handle> tryStartRead();
  // Start a new top-level read, returning a handle to the buffer if it has some bytes in it.
  //
  // If the buffer is empty, returns a promise representing a read operation for as many bytes are
  // available on the underlying stream up to the capacity of the buffer. Calling `tryStartRead`
  // again after this promise resolves should either return Handle or EOF

private:
  Stream& stream;
  Promise<size_t> tryRead(size_t min);
};

class RetainableRange final : public Refcounted {
  // A handle to access a partial range of bytes in an `FdRangeInputBuffer`, that may be used to
  // retain the range for long-term ownership.

public:
  KJ_DISALLOW_COPY(RetainableRange);

  inline ArrayPtr<byte> getBytes() { return range; }
  inline ArrayPtr<AutoCloseFd> getFds() { return fds; }

  void retainLongTerm();
  // Request ownership of this range from the FdRangeInputBuffer. The buffer may choose to perform
  // a copy, or transfer ownership of the buffer and allocate a new one to replace it.


  RetainableRange(Badge<_::FdRangeInputBufferBase>,
      _::FdRangeInputBufferBase& parent, ArrayPtr<byte> range, Array<AutoCloseFd> fds);
  RetainableRange(Badge<_::FdRangeInputBufferBase>,
      Array<byte> ownBytes, ArrayPtr<byte> range, Array<AutoCloseFd> fds);
  // Public for kj::refcounted

private:
  OneOf<_::FdRangeInputBufferBase*, Array<byte>> parentOrRetainedBytes;
  ArrayPtr<byte> range;
  Array<AutoCloseFd> fds;

  friend class _::FdRangeInputBufferBase;
};

// =======================================================================================
// inline implementation details

template<typename Stream>
FdRangeInputBuffer<Stream>::FdRangeInputBuffer(Stream& stream, FdRangeInputBufferOptions options)
    : _::FdRangeInputBufferBase(options), stream(stream) {}

template<typename Stream>
OneOf<EndOfFile, Promise<void>, typename FdRangeInputBuffer<Stream>::Handle>
FdRangeInputBuffer<Stream>::tryStartRead() {
  if (overflowed() && getByteRange().size() > 0) {
    KJ_FAIL_REQUIRE(
        "Must read all bytes out of overflow buffer before starting a new top-level read.");
  }

  KJ_IF_MAYBE(empty, state.tryGet<Empty>()) {
    if (empty->eof) return EndOfFile{};

    return tryRead(1).ignoreResult();
  }

  return Handle{ *this };
}

template<>
inline Promise<size_t> FdRangeInputBuffer<AsyncInputStream>::tryRead(size_t min) {
  auto params = prepareForRead(min);
  return stream.tryRead(params.start, min, params.max).then([this](size_t n) {
    handleReadResult(n, nullptr);
    return n;
  });
}

template<>
inline Promise<size_t> FdRangeInputBuffer<AsyncIoStream>::tryRead(size_t min) {
  auto params = prepareForRead(min);
  return stream.tryRead(params.start, min, params.max).then([this](size_t n) {
    handleReadResult(n, nullptr);
    return n;
  });
}

template<>
inline Promise<size_t> FdRangeInputBuffer<AsyncCapabilityStream>::tryRead(size_t min) {
  auto params = prepareForRead(min);
  return stream.tryReadWithFds(params.start, min, params.max, fds.begin(), fds.size())
      .then([this](auto result) {
    Array<AutoCloseFd> newFds;
    if (result.capCount > 0) {
      newFds = fds.slice(0, result.capCount).attach(mv(fds));
      fds = heapArray<AutoCloseFd>(options.maxFds);
    }

    handleReadResult(result.byteCount, mv(newFds));
    return result.byteCount;
  });
}

} // namespace kj

KJ_END_HEADER
