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

#include "catrank.capnp.h"
#include "capnproto-common.h"

namespace capnp {
namespace benchmark {
namespace capnp {

struct ScoredResult {
  double score;
  SearchResult::Reader result;

  ScoredResult() = default;
  ScoredResult(double score, SearchResult::Reader result): score(score), result(result) {}

  inline bool operator<(const ScoredResult& other) const { return score > other.score; }
};

class CatRankTestCase {
public:
  typedef SearchResultList Request;
  typedef SearchResultList Response;
  typedef int Expectation;

  static int setupRequest(SearchResultList::Builder request) {
    int count = fastRand(1000);
    int goodCount = 0;

    auto list = request.initResults(count);

    for (int i = 0; i < count; i++) {
      SearchResult::Builder result = list[i];
      result.setScore(1000 - i);
      int urlSize = fastRand(100);

      static const char URL_PREFIX[] = "http://example.com/";
      size_t urlPrefixLength = strlen(URL_PREFIX);
      auto url = result.initUrl(urlSize + urlPrefixLength);

      strcpy(url.begin(), URL_PREFIX);
      char* pos = url.begin() + urlPrefixLength;
      for (int j = 0; j < urlSize; j++) {
        *pos++ = 'a' + fastRand(26);
      }

      bool isCat = fastRand(8) == 0;
      bool isDog = fastRand(8) == 0;
      goodCount += isCat && !isDog;

      static std::string snippet;
      snippet.clear();
      snippet.push_back(' ');

      int prefix = fastRand(20);
      for (int j = 0; j < prefix; j++) {
        snippet.append(WORDS[fastRand(WORDS_COUNT)]);
      }

      if (isCat) snippet.append("cat ");
      if (isDog) snippet.append("dog ");

      int suffix = fastRand(20);
      for (int j = 0; j < suffix; j++) {
        snippet.append(WORDS[fastRand(WORDS_COUNT)]);
      }

      result.setSnippet(Text::Reader(snippet.c_str(), snippet.size()));
    }

    return goodCount;
  }

  static void handleRequest(SearchResultList::Reader request, SearchResultList::Builder response) {
    std::vector<ScoredResult> scoredResults;

    for (auto result: request.getResults()) {
      double score = result.getScore();
      if (strstr(result.getSnippet().cStr(), " cat ") != nullptr) {
        score *= 10000;
      }
      if (strstr(result.getSnippet().cStr(), " dog ") != nullptr) {
        score /= 10000;
      }
      scoredResults.emplace_back(score, result);
    }

    std::sort(scoredResults.begin(), scoredResults.end());

    auto list = response.initResults(scoredResults.size());
    auto iter = list.begin();
    for (auto result: scoredResults) {
      iter->setScore(result.score);
      iter->setUrl(result.result.getUrl());
      iter->setSnippet(result.result.getSnippet());
      ++iter;
    }
  }

  static bool checkResponse(SearchResultList::Reader response, int expectedGoodCount) {
    int goodCount = 0;
    for (auto result: response.getResults()) {
      if (result.getScore() > 1001) {
        ++goodCount;
      } else {
        break;
      }
    }

    return goodCount == expectedGoodCount;
  }
};

}  // namespace capnp
}  // namespace benchmark
}  // namespace capnp

int main(int argc, char* argv[]) {
  return capnp::benchmark::benchmarkMain<
      capnp::benchmark::capnp::BenchmarkTypes,
      capnp::benchmark::capnp::CatRankTestCase>(argc, argv);
}
