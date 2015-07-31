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
#include <unordered_map>

#include "mcrouter/lib/network/ConnectionLRUOptions.h"
#include "mcrouter/lib/network/UniqueIntrusiveList.h"

namespace facebook { namespace memcache {

/**
 * @class ConectionLRU
 * @brief A single-threaded LRU for connections.
 */
template <typename T>
class ConnectionLRU {
 public:
  /**
   * Constructor
   *
   * @param maxConns The maximum number of connections to be maintained
   *        by the LRU.  Once the maximum is hit, connections will begin to
   *        be evicted.
   */
  explicit ConnectionLRU(ConnectionLRUOptions opts);

  /**
   * @brief Creates a new entry in the LRU and places the connection
   *        at the front.
   *
   * @param fd The file descriptor of the connection.
   * @param conn The connection to be stored.
   *
   * @return True if the connection was added, false otherwise.  Iff true,
   *         the connection will be moved into the LRU.
   */
  bool addConnection(int fd, T&& conn);

  /**
   * @brief Moves the connection to the front of the LRU.
   *
   * @param fd The file descriptor of the connection.
   * @return Void.
   */
  void touchConnection(int fd);

  /**
   * @brief Forcibly evicts the connection from the LRU.
   *
   * @param fd The file descriptor of the connection.
   * @return Void.
   */
  void removeConnection(int fd);

 private:
  /**
   * @brief Check if the oldest connection is old enough to evict.
   *
   * @retun True if we can insert a new connection, false otherwise.
   */
  bool canAddConnection() const;

  /**
   * @class ConnectionLRUNode
   * @brief A node within the LRU.
   *
   * Used to maintain data and a callback to destroy the connection.
   *
   */
  class ConnectionLRUNode {
   public:
    /**
     * Constructor
     *
     * @param unique_fd The file descriptor used by the connection.  Really it
     *        just needs to be a unique ID.
     * @param conn The connection to be stored.
     * @param updateThreshold Minimum amount of time to update the node's
     *        position in the LRU.
     * @param unreapableTime Minimum age of the potential connection to
     *        evict.
     */
    ConnectionLRUNode(int unique_fd, T conn,
                      std::chrono::milliseconds updateThreshold,
                      std::chrono::milliseconds unreapableTime)
      : conn_(std::move(conn)),
      fd_{unique_fd},
      updateThreshold_{updateThreshold},
      unreapableTime_{unreapableTime} {
        touch();
    }

    /**
     * @return The file descriptor of the node.
     */
    int64_t fd() const {
      return fd_;
    }

    /**
     * Determines if enough time has passed to update the connection.
     *
     * @return True if the LRU should be updated, false otherwise.
     */
    bool shouldUpdate() const {
      std::chrono::microseconds difference =
        std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now() - lastUpdated_);
      return updateThreshold_.count() < difference.count();
    }

    /**
     * Determines if enough time has passed to evict the connection.
     *
     * @return True if the connection should be evicted, false otherwise.
     */
    bool canEvict() const {
      std::chrono::microseconds difference =
        std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now() - lastUpdated_);
      return unreapableTime_.count() < difference.count();
    }

    /**
     * Updates node's internal structure to keep track of expiration.
     */
    void touch() {
      lastUpdated_ = std::chrono::steady_clock::now();
    }

    /**
     * Used by the intrusive list to maintain the LRU's order.
     */
    UniqueIntrusiveListHook hook_;

   private:
    /**
     * The connection.
     */
    T conn_;

    /**
     * A unique identifier for the connection.  Used to look up LRUNode
     * in hashtable.
     */
    int64_t fd_;

    /**
     * The last time this node was touched.
     */
     std::chrono::time_point<std::chrono::steady_clock> lastUpdated_;

    /**
     * Minimum amount of time to update the node's position in the LRU.
     */
     std::chrono::milliseconds updateThreshold_;

    /**
     * Minimum age of the potential connection to evict.
     */
    std::chrono::milliseconds unreapableTime_;
  };

  /*
   * Options to manage the connections.
   */
  ConnectionLRUOptions opts_;

  /**
   * A linked list for LRU order.
   */
  UniqueIntrusiveList<ConnectionLRUNode, &ConnectionLRUNode::hook_> list_;

  /**
   * A hashmap used to find an elements position in the LRU.
   */
  std::unordered_map<int64_t, ConnectionLRUNode&> map_;
};

}}  // facebook::memcache

#include "ConnectionLRU-inl.h"
