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

#include "null-common.h"

namespace capnp {
namespace benchmark {
namespace null {

struct SearchResult {
  const char* url;
  double score;
  const char* snippet;
};

struct ScoredResult {
  double score;
  const SearchResult* result;

  ScoredResult() = default;
  ScoredResult(double score, const SearchResult* result): score(score), result(result) {}

  inline bool operator<(const ScoredResult& other) const { return score > other.score; }
};

class CatRankTestCase {
public:
  typedef List<SearchResult> Request;
  typedef List<SearchResult> Response;
  typedef int Expectation;

  static int setupRequest(List<SearchResult>* request) {
    int count = fastRand(1000);
    int goodCount = 0;

    request->init(count);
    for (int i = 0; i < count; i++) {
      SearchResult& result = request->items[i];
      result.score = 1000 - i;
      char* pos = reinterpret_cast<char*>(arenaPos);
      result.url = pos;

      strcpy(pos, "http://example.com/");
      pos += strlen("http://example.com/");
      int urlSize = fastRand(100);
      for (int j = 0; j < urlSize; j++) {
        *pos++ = 'a' + fastRand(26);
      }
      *pos++ = '\0';

      // Retroactively allocate the space we used.
      if (allocate<char>(pos - result.url) != result.url) {
        throw std::bad_alloc();
      }

      bool isCat = fastRand(8) == 0;
      bool isDog = fastRand(8) == 0;
      goodCount += isCat && !isDog;

      pos = reinterpret_cast<char*>(arenaPos);
      result.snippet = pos;

      *pos++ = ' ';

      int prefix = fastRand(20);
      for (int j = 0; j < prefix; j++) {
        const char* word = WORDS[fastRand(WORDS_COUNT)];
        size_t len = strlen(word);
        memcpy(pos, word, len);
        pos += len;
      }

      if (isCat) {
        strcpy(pos, "cat ");
        pos += 4;
      }
      if (isDog) {
        strcpy(pos, "dog ");
        pos += 4;
      }

      int suffix = fastRand(20);
      for (int j = 0; j < suffix; j++) {
        const char* word = WORDS[fastRand(WORDS_COUNT)];
        size_t len = strlen(word);
        memcpy(pos, word, len);
        pos += len;
      }
      *pos++ = '\0';

      // Retroactively allocate the space we used.
      if (allocate<char>(pos - result.snippet) != result.snippet) {
        throw std::bad_alloc();
      }
    }

    return goodCount;
  }

  static void handleRequest(const List<SearchResult>& request, List<SearchResult>* response) {
    std::vector<ScoredResult> scoredResults;
    scoredResults.reserve(request.size);

    for (auto& result: request) {
      double score = result.score;
      if (strstr(result.snippet, " cat ") != nullptr) {
        score *= 10000;
      }
      if (strstr(result.snippet, " dog ") != nullptr) {
        score /= 10000;
      }
      scoredResults.emplace_back(score, &result);
    }

    std::sort(scoredResults.begin(), scoredResults.end());

    response->init(scoredResults.size());
    SearchResult* dst = response->items;
    for (auto& result: scoredResults) {
      dst->url = copyString(result.result->url);
      dst->score = result.score;
      dst->snippet = copyString(result.result->snippet);
      ++dst;
    }
  }

  static bool checkResponse(const List<SearchResult>& response, int expectedGoodCount) {
    int goodCount = 0;
    for (auto& result: response) {
      if (result.score > 1001) {
        ++goodCount;
      } else {
        break;
      }
    }

    return goodCount == expectedGoodCount;
  }
};

}  // namespace null
}  // namespace benchmark
}  // namespace capnp

int main(int argc, char* argv[]) {
  return capnp::benchmark::benchmarkMain<
      capnp::benchmark::null::BenchmarkTypes,
      capnp::benchmark::null::CatRankTestCase>(argc, argv);
}
