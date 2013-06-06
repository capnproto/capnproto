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
