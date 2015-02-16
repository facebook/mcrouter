/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#ifndef MCROUTER_TEST_CLIENT_H
#define MCROUTER_TEST_CLIENT_H 1

#include <folly/dynamic.h>

#include "mcrouter/McrouterClient.h"

/**
 * This class exposes a simple MCRouterTestClient
 * that can be used for unit tests. The implementation
 * is not fast enough for general purpose use. It currently does
 * not support async operations nor does it support proper error handling
 * thought it can be easily extended to do so.
 */
namespace facebook { namespace memcache {

class McrouterOptions;

namespace test {

class ResultsSet;
class MCRouterTestClient {
public:
  /**
   * Create a new MCrouterTestClient
   *
   * @param: name The name of the mcrouter process
   * @param: opts A list of options to construct mcrouter
   */
  MCRouterTestClient(const std::string& name,
                     const McrouterOptions& opts);
  ~MCRouterTestClient();

  /**
   * Get keys from memcache via libmcrouter
   *
   * @param: keys a folly::dynamic array that contains a list of keys to fetch
   * @param: results a folly::dynamic::object that will be populated with the
   *                 the values that were found in the cache
   *
   * @return the number of cache hits
   */
  int get(const folly::dynamic &keys, folly::dynamic &results);

  /**
   * set keys into memcache via libmcrouter
   *
   * @param: kv_pairs a folly::dynamic::object that contains a map of
   *                  key => value to store into memcache
   * @param: results a folly::dynamic::object that will be populated with the
   *                 the key => true if stored / false otherwise
   *
   * @return the number of successful cache sets
   */
  int set(const folly::dynamic &kv_pairs,
          folly::dynamic &results);

  /**
   * delete keys from memcache via libmcrouter
   *
   * @param: keys a folly::dynamic array that contains a list of
   *                  keys to delete from  memcache
   * @param: local set to true if you want to delete from the local cluster only
   * @param: results a folly::dynamic::object that will be populated with the
   *                 the key => true if deleted / false otherwise
   *
   * @return the number of successful cache deletes
   */
  int del(const folly::dynamic &keys, bool local,
          folly::dynamic &results);

private:
  facebook::memcache::mcrouter::McrouterClient::Pointer client_;
  facebook::memcache::mcrouter::McrouterInstance* router_;
  std::unique_ptr<ResultsSet> rs_;

  bool issueRequests(const facebook::memcache::mcrouter::mcrouter_msg_t* msgs,
                     size_t nreqs,
                     folly::dynamic &results);
};

}}}

#endif
