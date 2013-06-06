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

#include "common.h"
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <google/protobuf/io/coded_stream.h>
#include <thread>
#if HAVE_SNAPPY
#include <snappy/snappy.h>
#include <snappy/snappy-sinksource.h>
#endif  // HAVE_SNAPPY

namespace capnp {
namespace benchmark {
namespace protobuf {

// =======================================================================================

struct SingleUseMessages {
  template <typename MessageType>
  struct Message {
    struct Reusable {};
    struct SingleUse: public MessageType {
      inline SingleUse(Reusable&) {}
    };
  };

  struct ReusableString {};
  struct SingleUseString: std::string {
    inline SingleUseString(ReusableString&) {}
  };

  template <typename MessageType>
  static inline void doneWith(MessageType& message) {
    // Don't clear -- single-use.
  }
};

struct ReusableMessages {
  template <typename MessageType>
  struct Message {
    struct Reusable: public MessageType {};
    typedef MessageType& SingleUse;
  };

  typedef std::string ReusableString;
  typedef std::string& SingleUseString;

  template <typename MessageType>
  static inline void doneWith(MessageType& message) {
    message.Clear();
  }
};

// =======================================================================================
// The protobuf Java library defines a format for writing multiple protobufs to a stream, in which
// each message is prefixed by a varint size.  This was never added to the C++ library.  It's easy
// to do naively, but tricky to implement without accidentally losing various optimizations.  These
// two functions should be optimal.

struct Uncompressed {
  typedef google::protobuf::io::FileInputStream InputStream;
  typedef google::protobuf::io::FileOutputStream OutputStream;

  static uint64_t write(const google::protobuf::MessageLite& message,
                        google::protobuf::io::FileOutputStream* rawOutput) {
    google::protobuf::io::CodedOutputStream output(rawOutput);
    const int size = message.ByteSize();
    output.WriteVarint32(size);
    uint8_t* buffer = output.GetDirectBufferForNBytesAndAdvance(size);
    if (buffer != NULL) {
      message.SerializeWithCachedSizesToArray(buffer);
    } else {
      message.SerializeWithCachedSizes(&output);
      if (output.HadError()) {
        throw OsException(rawOutput->GetErrno());
      }
    }

    return size;
  }

  static void read(google::protobuf::io::ZeroCopyInputStream* rawInput,
                   google::protobuf::MessageLite* message) {
    google::protobuf::io::CodedInputStream input(rawInput);
    uint32_t size;
    GOOGLE_CHECK(input.ReadVarint32(&size));

    auto limit = input.PushLimit(size);

    GOOGLE_CHECK(message->MergePartialFromCodedStream(&input) &&
                 input.ConsumedEntireMessage());

    input.PopLimit(limit);
  }

  static void flush(google::protobuf::io::FileOutputStream* output) {
    if (!output->Flush()) throw OsException(output->GetErrno());
  }
};

// =======================================================================================
// The Snappy interface is really obnoxious.  I gave up here and am just reading/writing flat
// arrays in some static scratch space.  This probably gives protobufs an edge that it doesn't
// deserve.

#if HAVE_SNAPPY

static char scratch[1 << 20];
static char scratch2[1 << 20];

struct SnappyCompressed {
  typedef int InputStream;
  typedef int OutputStream;

  static uint64_t write(const google::protobuf::MessageLite& message, int* output) {
    size_t size = message.ByteSize();
    GOOGLE_CHECK_LE(size, sizeof(scratch));

    message.SerializeWithCachedSizesToArray(reinterpret_cast<uint8_t*>(scratch));

    size_t compressedSize = 0;
    snappy::RawCompress(scratch, size, scratch2 + sizeof(uint32_t), &compressedSize);
    uint32_t tag = compressedSize;
    memcpy(scratch2, &tag, sizeof(tag));

    writeAll(*output, scratch2, compressedSize + sizeof(tag));
    return compressedSize + sizeof(tag);
  }

  static void read(int* input, google::protobuf::MessageLite* message) {
    uint32_t size;
    readAll(*input, &size, sizeof(size));
    readAll(*input, scratch, size);

    size_t uncompressedSize;
    GOOGLE_CHECK(snappy::GetUncompressedLength(scratch, size, &uncompressedSize));
    GOOGLE_CHECK(snappy::RawUncompress(scratch, size, scratch2));

    GOOGLE_CHECK(message->ParsePartialFromArray(scratch2, uncompressedSize));
  }

  static void flush(OutputStream*) {}
};

#endif  // HAVE_SNAPPY

// =======================================================================================

#define REUSABLE(type) \
  typename ReuseStrategy::template Message<typename TestCase::type>::Reusable
#define SINGLE_USE(type) \
  typename ReuseStrategy::template Message<typename TestCase::type>::SingleUse

template <typename TestCase, typename ReuseStrategy, typename Compression>
struct BenchmarkMethods {
  static uint64_t syncClient(int inputFd, int outputFd, uint64_t iters) {
    uint64_t throughput = 0;

    typename Compression::OutputStream output(outputFd);
    typename Compression::InputStream input(inputFd);

    REUSABLE(Request) reusableRequest;
    REUSABLE(Response) reusableResponse;

    for (; iters > 0; --iters) {
      SINGLE_USE(Request) request(reusableRequest);
      typename TestCase::Expectation expected = TestCase::setupRequest(&request);
      throughput += Compression::write(request, &output);
      Compression::flush(&output);
      ReuseStrategy::doneWith(request);

      SINGLE_USE(Response) response(reusableResponse);
      Compression::read(&input, &response);
      if (!TestCase::checkResponse(response, expected)) {
        throw std::logic_error("Incorrect response.");
      }
      ReuseStrategy::doneWith(response);
    }

    return throughput;
  }

  static uint64_t asyncClientSender(
      int outputFd, ProducerConsumerQueue<typename TestCase::Expectation>* expectations,
      uint64_t iters) {
    uint64_t throughput = 0;

    typename Compression::OutputStream output(outputFd);
    REUSABLE(Request) reusableRequest;

    for (; iters > 0; --iters) {
      SINGLE_USE(Request) request(reusableRequest);
      expectations->post(TestCase::setupRequest(&request));
      throughput += Compression::write(request, &output);
      Compression::flush(&output);
      ReuseStrategy::doneWith(request);
    }

    return throughput;
  }

  static void asyncClientReceiver(
      int inputFd, ProducerConsumerQueue<typename TestCase::Expectation>* expectations,
      uint64_t iters) {
    typename Compression::InputStream input(inputFd);
    REUSABLE(Response) reusableResponse;

    for (; iters > 0; --iters) {
      typename TestCase::Expectation expected = expectations->next();
      SINGLE_USE(Response) response(reusableResponse);
      Compression::read(&input, &response);
      if (!TestCase::checkResponse(response, expected)) {
        throw std::logic_error("Incorrect response.");
      }
      ReuseStrategy::doneWith(response);
    }
  }

  static uint64_t asyncClient(int inputFd, int outputFd, uint64_t iters) {
    ProducerConsumerQueue<typename TestCase::Expectation> expectations;
    std::thread receiverThread(asyncClientReceiver, inputFd, &expectations, iters);
    uint64_t throughput = asyncClientSender(outputFd, &expectations, iters);
    receiverThread.join();

    return throughput;
  }

  static uint64_t server(int inputFd, int outputFd, uint64_t iters) {
    uint64_t throughput = 0;

    typename Compression::OutputStream output(outputFd);
    typename Compression::InputStream input(inputFd);

    REUSABLE(Request) reusableRequest;
    REUSABLE(Response) reusableResponse;

    for (; iters > 0; --iters) {
      SINGLE_USE(Request) request(reusableRequest);
      Compression::read(&input, &request);

      SINGLE_USE(Response) response(reusableResponse);
      TestCase::handleRequest(request, &response);
      ReuseStrategy::doneWith(request);

      throughput += Compression::write(response, &output);
      Compression::flush(&output);
      ReuseStrategy::doneWith(response);
    }

    return throughput;
  }

  static uint64_t passByObject(uint64_t iters, bool countObjectSize) {
    uint64_t throughput = 0;

    REUSABLE(Request) reusableRequest;
    REUSABLE(Response) reusableResponse;

    for (; iters > 0; --iters) {
      SINGLE_USE(Request) request(reusableRequest);
      typename TestCase::Expectation expected = TestCase::setupRequest(&request);

      SINGLE_USE(Response) response(reusableResponse);
      TestCase::handleRequest(request, &response);
      ReuseStrategy::doneWith(request);
      if (!TestCase::checkResponse(response, expected)) {
        throw std::logic_error("Incorrect response.");
      }
      ReuseStrategy::doneWith(response);

      if (countObjectSize) {
        throughput += request.SpaceUsed();
        throughput += response.SpaceUsed();
      }
    }

    return throughput;
  }

  static uint64_t passByBytes(uint64_t iters) {
    uint64_t throughput = 0;

    REUSABLE(Request) reusableClientRequest;
    REUSABLE(Request) reusableServerRequest;
    REUSABLE(Response) reusableServerResponse;
    REUSABLE(Response) reusableClientResponse;
    typename ReuseStrategy::ReusableString reusableRequestString, reusableResponseString;

    for (; iters > 0; --iters) {
      SINGLE_USE(Request) clientRequest(reusableClientRequest);
      typename TestCase::Expectation expected = TestCase::setupRequest(&clientRequest);

      typename ReuseStrategy::SingleUseString requestString(reusableRequestString);
      clientRequest.SerializePartialToString(&requestString);
      throughput += requestString.size();
      ReuseStrategy::doneWith(clientRequest);

      SINGLE_USE(Request) serverRequest(reusableServerRequest);
      serverRequest.ParsePartialFromString(requestString);

      SINGLE_USE(Response) serverResponse(reusableServerResponse);
      TestCase::handleRequest(serverRequest, &serverResponse);
      ReuseStrategy::doneWith(serverRequest);

      typename ReuseStrategy::SingleUseString responseString(reusableResponseString);
      serverResponse.SerializePartialToString(&responseString);
      throughput += responseString.size();
      ReuseStrategy::doneWith(serverResponse);

      SINGLE_USE(Response) clientResponse(reusableClientResponse);
      clientResponse.ParsePartialFromString(responseString);

      if (!TestCase::checkResponse(clientResponse, expected)) {
        throw std::logic_error("Incorrect response.");
      }
      ReuseStrategy::doneWith(clientResponse);
    }

    return throughput;
  }
};

struct BenchmarkTypes {
  typedef protobuf::Uncompressed Uncompressed;
  typedef protobuf::Uncompressed Packed;
#if HAVE_SNAPPY
  typedef protobuf::SnappyCompressed SnappyCompressed;
#endif  // HAVE_SNAPPY

  typedef protobuf::ReusableMessages ReusableResources;
  typedef protobuf::SingleUseMessages SingleUseResources;

  template <typename TestCase, typename ReuseStrategy, typename Compression>
  struct BenchmarkMethods
      : public protobuf::BenchmarkMethods<TestCase, ReuseStrategy, Compression> {};
};

}  // namespace protobuf
}  // namespace benchmark
}  // namespace capnp
