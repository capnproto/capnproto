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

#include "serialize-snappy.h"
#include "wire-format.h"
#include <snappy/snappy.h>
#include <snappy/snappy-sinksource.h>
#include <vector>

namespace capnproto {

namespace {

class InputStreamSource: public snappy::Source {
public:
  inline InputStreamSource(InputStream& inputStream, size_t available)
      : inputStream(inputStream), available(available), pos(nullptr), end(nullptr) {
    // Read at least 10 bytes (or all available bytes if less than 10), and at most the whole buffer
    // (unless less is available).
    size_t firstSize = inputStream.read(buffer,
        std::min<size_t>(available, 10),
        std::min<size_t>(available, sizeof(buffer)));

    CAPNPROTO_ASSERT(snappy::GetUncompressedLength(buffer, firstSize, &uncompressedSize),
                     "Invalid snappy-compressed data.");

    pos = buffer;
    end = pos + firstSize;
  }
  inline ~InputStreamSource() {};

  inline size_t getUncompressedSize() { return uncompressedSize; }

  // implements snappy::Source ---------------------------------------

  size_t Available() const override {
    return available;
  }

  const char* Peek(size_t* len) override {
    *len = end - pos;
    return pos;
  }

  void Skip(size_t n) override {
    pos += n;
    available -= n;

    if (pos == end && available > 0) {
      // Read more from the input.
      pos = buffer;
      end = pos + inputStream.read(buffer, 1, std::min<size_t>(available, sizeof(buffer)));
    }
  }

private:
  InputStream& inputStream;
  size_t available;
  size_t uncompressedSize;
  char* pos;
  char* end;
  char buffer[8192];
};

}  // namespcae

SnappyMessageReader::SnappyMessageReader(
    InputStream& inputStream, ReaderOptions options, ArrayPtr<word> scratchSpace)
    : MessageReader(options), inputStream(inputStream) {
  internal::WireValue<uint32_t> wireCompressedSize;
  inputStream.read(&wireCompressedSize, sizeof(wireCompressedSize));

  size_t compressedSize = wireCompressedSize.get();
  InputStreamSource source(inputStream, compressedSize);

  CAPNPROTO_ASSERT(source.getUncompressedSize() % sizeof(word) == 0,
                   "Uncompressed size was not a whole number of words.");
  size_t uncompressedWords = source.getUncompressedSize() / sizeof(word);

  if (scratchSpace.size() < uncompressedWords) {
    space = newArray<word>(uncompressedWords);
    scratchSpace = space;
  }

  CAPNPROTO_ASSERT(
      snappy::RawUncompress(&source, reinterpret_cast<char*>(scratchSpace.begin())),
      "Snappy decompression failed.");

  new(&underlyingReader) FlatArrayMessageReader(scratchSpace, options);
}

SnappyMessageReader::~SnappyMessageReader() {
  underlyingReader.~FlatArrayMessageReader();
}

ArrayPtr<const word> SnappyMessageReader::getSegment(uint id) {
  return underlyingReader.getSegment(id);
}

SnappyFdMessageReader::~SnappyFdMessageReader() {}

// =======================================================================================

namespace {

class SegmentArraySource: public snappy::Source {
public:
  SegmentArraySource(ArrayPtr<const ArrayPtr<const byte>> pieces)
      : pieces(pieces), offset(0), available(0) {
    for (auto& piece: pieces) {
      available += piece.size();
    }
    Skip(0);  // Skip leading zero-sized pieces, if any.
  }
  ~SegmentArraySource() {}

  // implements snappy::Source ---------------------------------------

  size_t Available() const override {
    return available;
  }

  const char* Peek(size_t* len) override {
    if (pieces.size() == 0) {
      *len = 0;
      return nullptr;
    } else {
      *len = pieces[0].size() - offset;
      return reinterpret_cast<const char*>(pieces[0].begin()) + offset;
    }
  }

  void Skip(size_t n) override {
    available -= n;
    while (pieces.size() > 0 && n >= pieces[0].size() - offset) {
      n -= pieces[0].size() - offset;
      offset = 0;
      pieces = pieces.slice(1, pieces.size());
    }
    offset += n;
  }

private:
  ArrayPtr<const ArrayPtr<const byte>> pieces;
  size_t offset;
  size_t available;
};

class AccumulatingSink: public snappy::Sink {
public:
  AccumulatingSink()
      : pos(firstPiece + sizeof(compressedSize)),
        end(firstPiece + sizeof(firstPiece)), firstEnd(firstPiece) {}
  ~AccumulatingSink() {}

  void writeTo(size_t size, OutputStream& output) {
    finalizePiece();

    compressedSize.set(size);

    ArrayPtr<const byte> pieces[morePieces.size() + 1];

    pieces[0] = arrayPtr(reinterpret_cast<byte*>(firstPiece),
                         reinterpret_cast<byte*>(firstEnd));
    for (uint i = 0; i < morePieces.size(); i++) {
      auto& piece = morePieces[i];
      pieces[i + 1] = arrayPtr(reinterpret_cast<byte*>(piece.start.get()),
                               reinterpret_cast<byte*>(piece.end));
    }

    output.write(arrayPtr(pieces, morePieces.size() + 1));
  }

  // implements snappy::Sink -----------------------------------------

  void Append(const char* bytes, size_t n) override {
    if (bytes == pos) {
      pos += n;
    } else {
      size_t a = available();
      if (n > a) {
        memcpy(pos, bytes, a);
        bytes += a;
        addPiece(n);
      }

      memcpy(pos, bytes, n);
      pos += n;
    }
  }

  char* GetAppendBuffer(size_t length, char* scratch) override {
    if (length > available()) {
      addPiece(length);
    }
    return pos;
  }

private:
  char* pos;
  char* end;
  // Stand and end point of the current available space.

  char* firstEnd;
  // End point of the used portion of the first piece.

  struct Piece {
    std::unique_ptr<char[]> start;
    // Start point of the piece.

    char* end;
    // End point of the used portion of the piece.

    Piece() = default;
    Piece(std::unique_ptr<char[]> start, char* end): start(std::move(start)), end(end) {}
  };

  std::vector<Piece> morePieces;

  union {
    internal::WireValue<uint32_t> compressedSize;
    char firstPiece[8192];
  };

  inline size_t available() {
    return end - pos;
  }

  inline void finalizePiece() {
    if (morePieces.empty()) {
      firstEnd = pos;
    } else {
      morePieces.back().end = pos;
    }
  }

  void addPiece(size_t minSize) {
    finalizePiece();

    std::unique_ptr<char[]> newPiece(new char[minSize]);
    pos = newPiece.get();
    end = pos + minSize;
    morePieces.emplace_back(std::move(newPiece), pos);
  }
};

class SnappyOutputStream: public OutputStream {
public:
  SnappyOutputStream(OutputStream& output): output(output), sawWrite(false) {}

  // implements OutputStream -----------------------------------------

  void write(const void* buffer, size_t size) override {
    CAPNPROTO_ASSERT(false, "writeMessage() was not expected to call this.");
  }

  void write(ArrayPtr<const ArrayPtr<const byte>> pieces) {
    CAPNPROTO_ASSERT(!sawWrite, "writeMessage() was expected to issue exactly one write.");
    sawWrite = true;

    SegmentArraySource source(pieces);
    AccumulatingSink sink;
    size_t size = snappy::Compress(&source, &sink);

    sink.writeTo(size, output);
  }

private:
  OutputStream& output;
  bool sawWrite;
};

}  // namespace

void writeSnappyMessage(OutputStream& output, ArrayPtr<const ArrayPtr<const word>> segments) {
  SnappyOutputStream snappyOutput(output);
  writeMessage(snappyOutput, segments);
}

void writeSnappyMessageToFd(int fd, ArrayPtr<const ArrayPtr<const word>> segments) {
  FdOutputStream output(fd);
  writeSnappyMessage(output, segments);
}

}  // namespace capnproto
