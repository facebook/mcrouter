/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <folly/Memory.h>

namespace facebook { namespace memcache {

template <typename T>
ConnectionLRU<T>::ConnectionLRU(ConnectionLRUOptions opts)
  : opts_{std::move(opts)},
  map_(opts.maxConns + 1) {
}

template <typename T>
bool ConnectionLRU<T>::addConnection(int fd, T&& conn) {
  if (!canAddConnection()) {
    return false;
  }

  // Check if we are exceeding the limit by inserting a new connection.
  if (list_.size() >= opts_.maxConns) {
    auto& oldNode = list_.front();
    removeConnection(oldNode.fd());
  }

  // Create the node to hold the connection and store the connection in the LRU.
  auto& node =
    list_.pushBack(folly::make_unique<ConnectionLRUNode>(fd, std::move(conn),
                                                         opts_.updateThreshold,
                                                         opts_.unreapableTime));
  // Store a reference to the connection in a map.
  std::pair<int64_t, ConnectionLRUNode&> map_pair(fd, node);
  map_.insert(map_pair);

  return true;
}

template <typename T>
void ConnectionLRU<T>::touchConnection(int fd) {
  // Find the connection and bring it to the front of the LRU.
  auto search = map_.find(fd);
  if (search != map_.end()) {
    auto& node = search->second;
    if (node.shouldUpdate()) {
      list_.pushBack(list_.extract(list_.iterator_to(node)));
      node.touch();
    }
  }
}

template <typename T>
void ConnectionLRU<T>::removeConnection(int fd) {
  // Remove the connection from the LRU.
  auto search = map_.find(fd);
  if (search != map_.end()) {
    auto& node = search->second;
    map_.erase(fd);
    list_.extract(list_.iterator_to(node));
  }
}

template <typename T>
bool ConnectionLRU<T>::canAddConnection() const {
  if (!opts_.maxConns) {
    return false;
  }

  if (list_.size() < opts_.maxConns) {
    return true;
  }

  auto& oldestNode = list_.front();
  return oldestNode.canEvict();
}

}}  // facebook::memcache
