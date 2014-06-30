/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include <iostream>
#include <string>

#include <gtest/gtest.h>

#include "folly/Benchmark.h"
#include "mcrouter/lib/fbi/cpp/Trie.h"

using facebook::memcache::Trie;

static int const kNumGetKeys = 7;
static std::string keysToGet[kNumGetKeys] = {
  "abacaba",
  "abacabaddd",
  "b1",
  "",
  "abacabadabacab",
  "qwerty:qwerty:qwerty:123456",
  "abd",
};
static Trie<long> randTrie;
static long x = 0;

static void prepareRand() {
  static std::string keys[] = {
    "abacaba",
    "abacabadabacaba",
    "b123",
    "qwerty:qwerty:qwerty:123456",
  };
  auto numKeys = sizeof(keys) / sizeof(keys[0]);

  for (long i = 0; i < numKeys; ++i) {
    randTrie.emplace(keys[i], i + 1);
  }
}

BENCHMARK(Trie_get) {
  for (int i = 0; i < kNumGetKeys; ++i) {
    auto r = randTrie.find(keysToGet[i]);
    x += r == randTrie.end() ? 0 : r->second;
  }
}

BENCHMARK(Trie_get_prefix) {
  for (int i = 0; i < kNumGetKeys; ++i) {
    auto r = randTrie.findPrefix(keysToGet[i]);
    x += r == randTrie.end() ? 0 : r->second;
  }
}

int main(int argc, char **argv){
  prepareRand();
  google::ParseCommandLineFlags(&argc, &argv, true);
  folly::runBenchmarksOnFlag();
  std::cout << "check (should be num of iters*12): " << x << std::endl;
  return 0;
}
