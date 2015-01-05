/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#ifndef FBI_CPP_TRIE_INL_H
#define FBI_CPP_TRIE_INL_H

#include <boost/iterator/iterator_facade.hpp>

#include <glog/logging.h>

#include <folly/Memory.h>

namespace facebook { namespace memcache {

template<class Value, char MinChar, char MaxChar>
Trie<Value, MinChar, MaxChar>::Trie()
  : parent_(nullptr),
    c_(0) {
}

template<class Value, char MinChar, char MaxChar>
Trie<Value, MinChar, MaxChar>::Trie(const Trie& other)
  : value_(other.value_),
    parent_(other.parent_),
    c_(other.c_) {

  for (auto edge = 0; edge < kNumChars; ++edge) {
    if (other.next_[edge]) {
      next_[edge] = folly::make_unique<Trie>(*other.next_[edge]);
      next_[edge]->parent_ = this;
    }
  }
}

template<class Value, char MinChar, char MaxChar>
Trie<Value, MinChar, MaxChar>::Trie(Trie&& other)
    : value_(std::move(other.value_)),
      next_(std::move(other.next_)),
      parent_(other.parent_),
      c_(other.c_) {
}

template<class Value, char MinChar, char MaxChar>
Trie<Value, MinChar, MaxChar>&
Trie<Value, MinChar, MaxChar>::operator=(const Trie& other) {
  return *this = Trie(other);
}

template<class Value, char MinChar, char MaxChar>
Trie<Value, MinChar, MaxChar>&
Trie<Value, MinChar, MaxChar>::operator=(Trie&& other) {
  assert(this != &other);
  // can not use operator=, because pair.first is const
  if (other.value_) {
    value_.emplace(std::move(other.value_->first),
                   std::move(other.value_->second));
  } else {
    value_.clear();
  }
  next_ = std::move(other.next_);
  parent_ = other.parent_;
  c_ = other.c_;

  return *this;
}

template<class Value, char MinChar, char MaxChar>
typename Trie<Value, MinChar, MaxChar>::const_iterator
Trie<Value, MinChar, MaxChar>::find(folly::StringPiece key) const {
  return const_iterator(findImpl(key));
}

template<class Value, char MinChar, char MaxChar>
typename Trie<Value, MinChar, MaxChar>::iterator
Trie<Value, MinChar, MaxChar>::find(folly::StringPiece key) {
  return iterator(const_cast<Trie*>(findImpl(key)));
}

template<class Value, char MinChar, char MaxChar>
const Trie<Value, MinChar, MaxChar>*
Trie<Value, MinChar, MaxChar>::findImpl(folly::StringPiece key) const {
  auto node = this;
  for (const auto c : key) {
    node = node->getEdge(c);
    if (node == nullptr) {
      return nullptr;
    }
  }
  return node->value_ ? node : nullptr;
}

template<class Value, char MinChar, char MaxChar>
void Trie<Value, MinChar, MaxChar>::emplace(folly::StringPiece key, Value val) {
  auto node = this;
  for (auto c : key) {
    auto& nx = node->next_[toEdge(c)];
    if (!nx) {
      nx = folly::make_unique<Trie>();
      nx->parent_ = node;
      nx->c_ = c;
    }
    node = nx.get();
  }

  node->value_.emplace(key.str(), std::move(val));
}

template<class Value, char MinChar, char MaxChar>
typename Trie<Value, MinChar, MaxChar>::const_iterator
Trie<Value, MinChar, MaxChar>::findPrefix(folly::StringPiece key) const {
  return const_iterator(findPrefixImpl(key));
}

template<class Value, char MinChar, char MaxChar>
typename Trie<Value, MinChar, MaxChar>::iterator
Trie<Value, MinChar, MaxChar>::findPrefix(folly::StringPiece key) {
  return iterator(const_cast<Trie*>(findPrefixImpl(key)));
}

template<class Value, char MinChar, char MaxChar>
const Trie<Value, MinChar, MaxChar>*
Trie<Value, MinChar, MaxChar>::findPrefixImpl(folly::StringPiece key) const {
  auto node = this;
  const Trie* result = nullptr;
  for (const auto c : key) {
    if (node->value_) {
      result = node;
    }
    node = node->getEdge(c);
    if (node == nullptr) {
      return result;
    }
  }
  return node->value_ ? node : result;
}

template<class Value, char MinChar, char MaxChar>
typename Trie<Value, MinChar, MaxChar>::const_iterator
Trie<Value, MinChar, MaxChar>::begin() const {
  return cbegin();
}

template<class Value, char MinChar, char MaxChar>
typename Trie<Value, MinChar, MaxChar>::iterator
Trie<Value, MinChar, MaxChar>::begin() {
  return iterator(this);
}

template<class Value, char MinChar, char MaxChar>
typename Trie<Value, MinChar, MaxChar>::const_iterator
Trie<Value, MinChar, MaxChar>::end() const {
  return cend();
}

template<class Value, char MinChar, char MaxChar>
typename Trie<Value, MinChar, MaxChar>::iterator
Trie<Value, MinChar, MaxChar>::end() {
  return iterator();
}

template<class Value, char MinChar, char MaxChar>
typename Trie<Value, MinChar, MaxChar>::const_iterator
Trie<Value, MinChar, MaxChar>::cbegin() const {
  return const_iterator(this);
}

template<class Value, char MinChar, char MaxChar>
typename Trie<Value, MinChar, MaxChar>::const_iterator
Trie<Value, MinChar, MaxChar>::cend() const {
  return const_iterator();
}

template<class Value, char MinChar, char MaxChar>
Trie<Value, MinChar, MaxChar>*
Trie<Value, MinChar, MaxChar>::getEdge(char c) const {
  if (LIKELY(MinChar <= c && c <= MaxChar)) {
    return next_[c - MinChar].get();
  }
  return nullptr;
}

template<class Value, char MinChar, char MaxChar>
int Trie<Value, MinChar, MaxChar>::toEdge(char c) {
  CHECK(MinChar <= c && c <= MaxChar) << "Out of bounds character";
  return c - MinChar;
}

template<class Value, char MinChar, char MaxChar>
void Trie<Value, MinChar, MaxChar>::clear() {
  value_.clear();
  for (auto& edge : next_) {
    edge.reset();
  }
}

/* iterator_base */

template <class Value, char MinChar, char MaxChar>
template <class T, class V>
class Trie<Value, MinChar, MaxChar>::iterator_base
  : public boost::iterator_facade<
      /* Derived */ iterator_base<T, V>,
      /* Value */ V,
      /* CategoryOrTraversal */ boost::forward_traversal_tag>
{
 public:
  iterator_base()
    : t_(nullptr) {
  }

  explicit iterator_base(T* trie)
    : t_(trie) {
    if (t_ != nullptr && !t_->value_) {
      increment();
    }
  }
 private:
  friend class boost::iterator_core_access;
  T* t_;

  void increment() {
    // nonrecursive DFS over Trie.
    size_t edge = 0;
    while (t_ != nullptr) {
      // try to go "deeper"
      while (edge < kNumChars) {
        // found child, so make key longer
        if (t_->next_[edge]) {
          t_ = t_->next_[edge].get();
          // found child with value
          if (t_->value_) {
            return;
          }
          edge = 0;
        } else {
          ++edge;
        }
      }
      // nothing in this subtree, go back
      do {
        edge = (t_->c_ - MinChar) + 1;
        t_ = t_->parent_;
      } while (t_ != nullptr && edge == kNumChars);
    }
    // here we have t_ == nullptr. It is equal to end() iterator of Trie.
  }

  bool equal(const iterator_base& other) const {
    return this->t_ == other.t_;
  }

  V& dereference() const {
    return *t_->value_;
  }
};

}} // facebook::memcache

#endif
