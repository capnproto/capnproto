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

#if !CAPNP_NO_PROTOBUF_BENCHMARK

#include "catrank.pb.h"
#include "protobuf-common.h"

namespace capnp {
namespace benchmark {
namespace protobuf {

struct ScoredResult {
  double score;
  const SearchResult* result;

  ScoredResult() = default;
  ScoredResult(double score, const SearchResult* result): score(score), result(result) {}

  inline bool operator<(const ScoredResult& other) const { return score > other.score; }
};

class CatRankTestCase {
public:
  typedef SearchResultList Request;
  typedef SearchResultList Response;
  typedef int Expectation;

  static int setupRequest(SearchResultList* request) {
    int count = fastRand(1000);
    int goodCount = 0;

    for (int i = 0; i < count; i++) {
      SearchResult* result = request->add_result();
      result->set_score(1000 - i);
      result->set_url("http://example.com/");
      std::string* url = result->mutable_url();
      int urlSize = fastRand(100);
      for (int j = 0; j < urlSize; j++) {
        url->push_back('a' + fastRand(26));
      }

      bool isCat = fastRand(8) == 0;
      bool isDog = fastRand(8) == 0;
      goodCount += isCat && !isDog;

      std::string* snippet = result->mutable_snippet();
      snippet->reserve(7 * 22);
      snippet->push_back(' ');

      int prefix = fastRand(20);
      for (int j = 0; j < prefix; j++) {
        snippet->append(WORDS[fastRand(WORDS_COUNT)]);
      }

      if (isCat) snippet->append("cat ");
      if (isDog) snippet->append("dog ");

      int suffix = fastRand(20);
      for (int j = 0; j < suffix; j++) {
        snippet->append(WORDS[fastRand(WORDS_COUNT)]);
      }
    }

    return goodCount;
  }

  static void handleRequest(const SearchResultList& request, SearchResultList* response) {
    std::vector<ScoredResult> scoredResults;

    for (auto& result: request.result()) {
      double score = result.score();
      if (result.snippet().find(" cat ") != std::string::npos) {
        score *= 10000;
      }
      if (result.snippet().find(" dog ") != std::string::npos) {
        score /= 10000;
      }
      scoredResults.emplace_back(score, &result);
    }

    std::sort(scoredResults.begin(), scoredResults.end());

    for (auto& result: scoredResults) {
      SearchResult* out = response->add_result();
      out->set_score(result.score);
      out->set_url(result.result->url());
      out->set_snippet(result.result->snippet());
    }
  }

  static bool checkResponse(const SearchResultList& response, int expectedGoodCount) {
    int goodCount = 0;
    for (auto& result: response.result()) {
      if (result.score() > 1001) {
        ++goodCount;
      } else {
        break;
      }
    }

    return goodCount == expectedGoodCount;
  }
};

}  // namespace protobuf
}  // namespace benchmark
}  // namespace capnp

int main(int argc, char* argv[]) {
  return capnp::benchmark::benchmarkMain<
      capnp::benchmark::protobuf::BenchmarkTypes,
      capnp::benchmark::protobuf::CatRankTestCase>(argc, argv);
}

#endif  // !CAPNP_NO_PROTOBUF_BENCHMARK
