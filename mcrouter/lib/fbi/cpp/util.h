/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#ifndef FBI_CPP_UTIL_H
#define FBI_CPP_UTIL_H

#include <chrono>
#include <string>

#include <folly/Format.h>
#include <folly/Likely.h>
#include <folly/Range.h>

#include "mcrouter/lib/fbi/nstring.h"

namespace folly {
class dynamic;
}

namespace facebook { namespace memcache {

/**
 * If `condition` is false, throws std::logic_error.
 * `format` and `args` are passed to folly::format().
 */
template<typename... Args>
void checkLogic(bool condition, folly::StringPiece format, Args&&... args) {
  if (UNLIKELY(!condition)) {
    throw std::logic_error(folly::sformat(format, std::forward<Args>(args)...));
  }
}

/**
 * throws std::logic_error with formatted string.
 * `format` and `args` are passed to folly::format().
 */
template<typename... Args>
[[ noreturn ]] void throwLogic(folly::StringPiece format, Args&&... args) {
  throw std::logic_error(folly::sformat(format, std::forward<Args>(args)...));
}

/** folly::to style conversion routines */

template <typename T, typename F> T to(const F& x);

template <>
inline nstring_t to<nstring_t>(const folly::StringPiece& sp) {
  nstring_t ns;
  ns.str = (sp.empty() ? nullptr : (char*)sp.begin());
  ns.len = sp.size();
  return ns;
}

template <>
inline nstring_t to<nstring_t>(const std::string& s) {
  nstring_t ns;
  ns.str = (s.empty() ? nullptr : (char*)s.data());
  ns.len = s.size();
  return ns;
}

template <>
inline folly::StringPiece to<folly::StringPiece>(const nstring_t& ns) {
  return folly::StringPiece(ns.str, ns.len);
}

template <>
inline std::string to<std::string>(const nstring_t& ns) {
  return std::string(ns.str, ns.len);
}

template <>
inline std::string to<std::string>(nstring_t* const& ns) {
  return std::string(ns->str, ns->len);
}

/** milliseconds to timeval_t */
template <>
inline timeval_t to<timeval_t>(const unsigned int& ms) {
  timeval_t r;
  r.tv_sec = ms / 1000;
  r.tv_usec = ms % 1000 * 1000;
  return r;
}

/** milliseconds to timeval_t */
template <>
inline timeval_t to<timeval_t>(const std::chrono::milliseconds& ms) {
  timeval_t r;
  r.tv_sec = ms.count() / 1000;
  r.tv_usec = ms.count() % 1000 * 1000;
  return r;
}

/** timeval_t to milliseconds */
template <>
inline std::chrono::milliseconds
to<std::chrono::milliseconds>(const timeval_t& t) {
  using namespace std::chrono;
  return duration_cast<milliseconds>(
    seconds(t.tv_sec) + microseconds(t.tv_usec));
}

/**
 * True iff a and b point to the same region in memory
 */
inline bool sameMemoryRegion(folly::StringPiece a, folly::StringPiece b) {
  return (a.empty() && b.empty()) ||
    (a.size() == b.size() && a.begin() == b.begin());
}

/**
 * Returns value from map or default, if there is no key in map.
 */
template <class Map>
inline typename Map::mapped_type tryGet(
    const Map& map,
    const typename Map::key_type& key,
    const typename Map::mapped_type def = typename Map::mapped_type()) {

  auto it = map.find(key);
  return it == map.end() ? def : it->second;
}

/**
 * Returns string with length in [minLen, maxLen] and random characters
 * from range.
 */
std::string randomString(size_t minLen = 1, size_t maxLen = 20,
    folly::StringPiece range = "abcdefghijklmnopqrstuvwxyz"
                               "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");

/**
 * Returns hash value for a given key.
 * To use for probabilistic sampling, e.g. for stats.
 */
uint32_t getMemcacheKeyHashValue(folly::StringPiece key);

/**
 * Checks if the given hash is within a range.
 * The range is from 0 to (MAX(uint32_t)/sample_rate)
 * Used for probabilistic decisions, like stats sampling.
 */
bool determineIfSampleKeyForViolet(uint32_t routingKeyHash,
                                   uint32_t sample_period);

/**
 * @return MD5 hash of a string
 */
std::string Md5Hash(folly::StringPiece input);

/**
 * @param Cmp  comparator used to compare characters
 *
 * @return  true if two strings are equal
 */
template <class Cmp>
bool equalStr(folly::StringPiece A, folly::StringPiece B, Cmp&& cmp) {
  if (A.size() != B.size()) {
    return false;
  }
  return std::equal(A.begin(), A.end(), B.begin(), std::forward<Cmp>(cmp));
}

/**
 * Writes 'contents' to the file, overwriting any existing contents
 * (will create if file doesn't exist)
 *
 * @return true on success, false otherwise
 */
bool writeStringToFile(folly::StringPiece contents, const std::string& path);

/**
 * Append 'contents' to the file (will create if file doesn't exist)
 *
 * @return true on success, false otherwise
 */
bool appendStringToFile(folly::StringPiece contents, const std::string& path);

/**
 * Write the given 'contents' to 'absFilename' atomically. This first writes
 * the contents to a temp file to in the absFilename's parent directory
 * and then calls 'rename()', which is atomic.
 *
 * @return true on success, false otherwise
*/
bool atomicallyWriteFileToDisk(folly::StringPiece contents,
                               const std::string& absFilename);

/**
 * Analogue of UNIX touch: changes file access and modification time, if file
 * doesn't exist creates it.
 *
 * @return true on success, false otherwise
*/
bool touchFile(const std::string& path);

/**
 * Make uint64 random number out of uint32. Especially useful for mt19937.
 */
template <class RNG>
typename std::enable_if<
  std::is_same<typename RNG::result_type, uint32_t>::value,
  uint64_t
>::type
randomInt64(RNG& rng) {
  return ((uint64_t)rng() << 32) | rng();
}

/**
 * Specialization for random generator with uint64 result type.
 */
template <class RNG>
typename std::enable_if<
  std::is_same<typename RNG::result_type, uint64_t>::value,
  uint64_t
>::type
randomInt64(RNG& rng) {
  return rng();
}

/**
 * @return name of current thread
 */
std::string getThreadName();

/**
 * Parse json string with `allow_trailing_comma` enabled by default
 */
folly::dynamic parseJsonString(folly::StringPiece s,
                               bool allow_trailing_comma = true);

/**
 * @return returns a prefix of `s` with '...' appended if s is longer than
 *         `maxLength`.
 */
std::string shorten(folly::StringPiece s, size_t maxLength);

/**
 * @return `s` where all occurences of `from` are replaced with `to`
 */
std::string replaceAll(std::string s, const std::string& from,
                       const std::string& to);

/**
 * Same as folly::toPrettyJson but also sorts keys in dictionaries and
 * converts fbstring to std::string
 */
std::string toPrettySortedJson(const folly::dynamic& json);

/**
 * Makes sure a directory exists and is writable (e.g. create if not found, etc)
 */
bool ensureDirExistsAndWritable(const std::string& path);

}}  // facebook::memcache

#endif
