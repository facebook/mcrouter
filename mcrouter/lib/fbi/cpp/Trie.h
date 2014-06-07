/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#ifndef FBI_CPP_TRIE_H
#define FBI_CPP_TRIE_H

#include <array>
#include <memory>
#include <stack>
#include <utility>

#include "folly/Optional.h"
#include "folly/Range.h"

namespace facebook { namespace memcache {

/**
 * Simple Trie implementation that assumes there are lots of gets and
 * small amount of sets. Example: Trie<int, 'a', 'z'> t;
 *
 * @param Value type of stored value. It should be possible to wrap this type
 *              with folly::Optional
 * @param MinChar minimum character that could be in key (inclusive)
 * @param MaxChar maximum characted that could be in key (inclusive)
 */
template<class Value, char MinChar = '!', char MaxChar = '~'>
class Trie {
  static_assert(MinChar <= MaxChar,
                "MinChar should be less or equal than MaxChar");
  /**
   * iterator implementation
   * @param T underlying container type
   * @param V value type
   */
  template<class T, class V>
  class iterator_base;
 public:
  typedef std::pair<const std::string, Value> value_type;
  typedef Value mapped_type;

  /**
   * Forward const iterator for Trie. Enumerates values stored in Trie in
   * lexicographic order of keys.
   */
  typedef iterator_base<const Trie, const value_type> const_iterator;
  /**
   * Forward iterator for Trie. Enumerates values stored in Trie in
   * lexicographic order of keys.
   */
  typedef iterator_base<Trie, value_type> iterator;

  Trie();

  Trie(const Trie& other);

  Trie(Trie&& other);

  Trie& operator=(const Trie& other);

  Trie& operator=(Trie&& other);

  /**
   * Return iterator for given key
   *
   * @return end() if no key found, iterator for given key otherwise
   */
  inline const_iterator find(folly::StringPiece key) const;

  inline iterator find(folly::StringPiece key);

  /**
   * Set value for key
   *
   * @param key string with each character from [MinChar, MaxChar]
   * @param value value to store
   */
  void emplace(folly::StringPiece key, Value value);

  /**
   * Get value of longest prefix stored in Trie
   *
   * @param key string with any characters
   * @return nullptr if no prefix found, pointer to value of the longest prefix
             otherwise
   */
  iterator findPrefix(folly::StringPiece key);

  const_iterator findPrefix(folly::StringPiece key) const;

  inline const_iterator begin() const;

  inline iterator begin();

  inline const_iterator end() const;

  inline iterator end();

  inline const_iterator cbegin() const;

  inline const_iterator cend() const;

  void clear();

 private:
  // total number of characters in one node
  static size_t const kNumChars = (size_t)(MaxChar - MinChar) + 1;

  // value stored in node
  folly::Optional<value_type> value_;

  // edges of this node (points to nodes with keys longer by one character)
  std::array<std::unique_ptr<Trie>, kNumChars> next_;

  Trie* parent_;
  char c_;

  // returns edge from next_ if c is in [MinChar, MaxChar], nullptr otherwise
  inline Trie* getEdge(char c) const;

  // converts character to index in 'next' array
  static inline int toEdge(char c);

  const Trie* findImpl(folly::StringPiece key) const;

  const Trie* findPrefixImpl(folly::StringPiece key) const;
};

}} // facebook::memcache

#include "Trie-inl.h"

#endif
