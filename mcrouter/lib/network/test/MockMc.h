/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include <folly/Range.h>

namespace folly {
class IOBuf;
}

namespace facebook { namespace memcache {

class McRequest;

/**
 * Mock Memcached hash table implementation.
 * Not thread-safe.
 */
class MockMc {
 public:
  struct Item {
    std::unique_ptr<folly::IOBuf> value;
    int32_t exptime{0};
    uint64_t flags{0};

    explicit Item(std::unique_ptr<folly::IOBuf> v);
    explicit Item(const McRequest& req);

    Item(const folly::IOBuf& v, int32_t t, uint64_t f);
  };

  /**
   * @return  nullptr if the item doesn't exist in the cache
   *          (expired/evicted/was never set); pointer to the item otherwise.
   */
  const Item* get(folly::StringPiece key);

  /**
   * Store item with the given key.
   */
  void set(folly::StringPiece key, Item item);

  /**
   * Store item with the given key only if no item with that key exists
   */
  bool add(folly::StringPiece key, Item item);

  /**
   * Store item with the given key only if the item with that key exists
   */
  bool replace(folly::StringPiece key, Item item);

  /**
   * Increment the value at key by delta (positive or negative)
   *
   * @return  Pair (exists, old_value).  Exists is true iff the item
   *          exists in the cache.  old_value is the item's value before
   *          the increment.
   */
  std::pair<bool, int64_t> arith(folly::StringPiece key, int64_t delta);

  /**
   * Delete the item with the given key.
   * Note that the item value still might be accessible through leaseGet.
   */
  bool del(folly::StringPiece key);

  /**
   * Leases
   */

  /**
   * Get the item or a token that permits storing the item with that key.
   *
   * @return
   *   (item, 0)                Item exists in the cache.
   *   (stale_item, token > 1)  Item was deleted, caller may set the item
   *   (stale_item, 1)          Item was deleted, caller may not set the item
   *                            because another caller already got a token
   *                            (stale value might still be useful though).
   */
  std::pair<const Item*, uint64_t> leaseGet(folly::StringPiece key);

  enum class LeaseSetResult {
    NOT_STORED,
    STORED,
    STALE_STORED,
  };

  /**
   * Attempt to store the item
   *
   * @return
   *   NOT_STORED    Token is expired/invalid, and the stale item was evicted.
   *   STORED        Token is valid and the item is stored normally.
   *   STALE_STORED  Token is expired/invalid, but the stale item still exists.
   *                 The stale item is updated and can be retrieved with
   *                 leaseGet calls.
   */
  LeaseSetResult leaseSet(folly::StringPiece key, Item item, uint64_t token);

  std::pair<const Item*, uint64_t> gets(folly::StringPiece key);

  enum class CasResult {
    NOT_FOUND,
    STORED,
    EXISTS
  };

  CasResult cas(folly::StringPiece key, Item item, uint64_t token);

  /**
   * clear all items
   */
  void flushAll();

 private:
  struct CacheItem {
    Item item;

    enum TLRUState {
      CACHE,
      TLRU,
      TLRU_HOT,
    };
    TLRUState state{CACHE};
    uint64_t leaseToken{0};
    uint64_t casToken{0};

    explicit CacheItem(Item it)
        : item(std::move(it)) {
      updateCasToken();
    }

    void updateLeaseToken();
    void updateCasToken();
  };
  std::unordered_map<std::string, CacheItem> citems_;

  std::unordered_map<std::string, CacheItem>::iterator
  findUnexpired(folly::StringPiece key);
};

}}  // facebook::memcache
