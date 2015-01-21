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
