/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <string>
#include <vector>

#include <glog/logging.h>
#include <gtest/gtest.h>

#include <folly/Benchmark.h>
#include <folly/experimental/StringKeyedUnorderedMap.h>

#include "mcrouter/lib/fbi/cpp/Trie.h"

using facebook::memcache::Trie;

namespace {

template <class Value>
class KeyPrefixMap : public folly::StringKeyedUnorderedMap<Value> {
  using Base = folly::StringKeyedUnorderedMap<Value>;
 public:

  using iterator = typename Base::iterator;

  std::pair<iterator, bool> emplace(folly::StringPiece key, Value v) {
    auto it = std::lower_bound(prefixLength_.begin(), prefixLength_.end(),
                               key.size());
    if (it == prefixLength_.end() || *it != key.size()) {
      prefixLength_.insert(it, key.size());
    }
    return Base::emplace(key, std::move(v));
  }

  iterator findPrefix(folly::StringPiece key) {
    auto result = end();
    for (auto len : prefixLength_) {
      if (len > key.size()) {
        return result;
      }
      auto it = find(key.subpiece(0, len));
      if (it != end()) {
        result = it;
      }
    }
    return result;
  }

  using Base::find;
  using Base::begin;
  using Base::end;
 private:
  std::vector<size_t> prefixLength_;
};

std::vector<std::string> keysToGet[3];

Trie<int> randTrie[3];
KeyPrefixMap<int> randMap[3];
int x = 0;

void prepareRand() {
  std::vector<std::string> keys[3] = {
    {
      "abacaba",
      "abacabadabacaba",
      "b123",
      "qwerty:qwerty:qwerty:123456",
    },
    {
      "AMC",
      "ayk",
      "brq",
      "bxj",
      "fgn",
      "fkr",
      "fm0",
      "gig",
      "gtg",
      "gtm",
      "iag",
      "kkb",
      "kki",
      "kkx",
      "kkz",
      "kqf",
      "kqg",
      "mbf",
      "mft",
      "mgg",
      "mgj",
      "mgr",
      "mhk",
      "mun",
      "rmg",
      "rak",
      "rdk",
      "rxg",
      "tm2",
      "tzb",
      "tzh",
      "zbg",
      "zgq",
      "zug",
    },
    {
      "hsdfbfda.ghu",
      "hsdfbfda.abc",
      "rbfdhkjs.abc",
      "rbjfyvbl.abc",
      "rbl.fsgjhdfb",
      "rbl.fdnolfbv",
      "rblkmvnf.abc",
      "rblplmbf.ghu",
      "rblplmbf.abc",
      "rubajvnr.ghu",
      "rubajvnr.abc",
    }
  };

  std::string missKeys[] = {
    "zahskjsdf",
    "aba",
    "",
    "z",
    "asdjl:dafnsjsdf"
  };

  for (int i = 0 ; i < 3; ++i) {
    for (int j = 0; j < keys[i].size(); ++j) {
      randTrie[i].emplace(keys[i][j], i + j + 1);
      randMap[i].emplace(keys[i][j], i + j + 1);
    }

    for (int j = 0; j < keys[i].size(); ++j) {
      keysToGet[i].push_back(keys[i][j] + ":hit");
    }

    for (int j = 0; j < 5; ++j) {
      keysToGet[i].push_back(missKeys[j]);
    }

    LOG(INFO) << "#" << i << " uses " << keysToGet[i].size() << " keys";
  }
}

template <class Container>
void runGet(Container& c, int id) {
  auto& keys = keysToGet[id];
  for (int i = 0; i < keys.size(); ++i) {
    auto r = c.find(keys[i]);
    x += r == c.end() ? 0 : r->second;
  }
}

template <class Container>
void runGetPrefix(Container& c, int id) {
  auto& keys = keysToGet[id];
  for (int i = 0; i < keys.size(); ++i) {
    auto r = c.findPrefix(keys[i]);
    x += r == c.end() ? 0 : r->second;
  }
}

}  // anonymous namespace

BENCHMARK(Trie_get0) {
  runGet(randTrie[0], 0);
}

BENCHMARK_RELATIVE(Map_get0) {
  runGet(randMap[0], 0);
}

BENCHMARK(Trie_get1) {
  runGet(randTrie[1], 1);
}

BENCHMARK_RELATIVE(Map_get1) {
  runGet(randMap[1], 1);
}

BENCHMARK(Trie_get2) {
  runGet(randTrie[2], 2);
}

BENCHMARK_RELATIVE(Map_get2) {
  runGet(randMap[2], 2);
}

BENCHMARK(Trie_get_prefix0) {
  runGetPrefix(randTrie[0], 0);
}

BENCHMARK_RELATIVE(Map_get_prefix0) {
  runGet(randMap[0], 0);
}

BENCHMARK(Trie_get_prefix1) {
  runGetPrefix(randTrie[1], 1);
}

BENCHMARK_RELATIVE(Map_get_prefix1) {
  runGet(randMap[1], 1);
}

BENCHMARK(Trie_get_prefix2) {
  runGetPrefix(randTrie[2], 2);
}

BENCHMARK_RELATIVE(Map_get_prefix2) {
  runGet(randMap[2], 2);
}

int main(int argc, char **argv){
  google::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);
  prepareRand();
  folly::runBenchmarks();
  LOG(INFO) << "check: " << x;
  return 0;
}

/*
 * ============================================================================
 * mcrouter/lib/fbi/cpp/test/TrieBenchmarks.cpp    relative  time/iter  iters/s
 * ============================================================================
 * Trie_get0                                                  123.68ns    8.09M
 * Map_get0                                          41.39%   298.80ns    3.35M
 * Trie_get1                                                  340.15ns    2.94M
 * Map_get1                                          28.16%     1.21us  827.88K
 * Trie_get2                                                  274.89ns    3.64M
 * Map_get2                                          46.64%   589.39ns    1.70M
 * Trie_get_prefix0                                           145.53ns    6.87M
 * Map_get_prefix0                                   48.55%   299.78ns    3.34M
 * Trie_get_prefix1                                           405.26ns    2.47M
 * Map_get_prefix1                                   33.60%     1.21us  829.21K
 * Trie_get_prefix2                                           342.24ns    2.92M
 * Map_get_prefix2                                   58.28%   587.22ns    1.70M
 * ============================================================================
 */
