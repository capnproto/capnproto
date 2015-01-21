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
