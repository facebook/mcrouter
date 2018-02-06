/*
 *  Copyright (c) 2015-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "ConnectionTracker.h"

namespace facebook {
namespace memcache {

ConnectionTracker::ConnectionTracker(size_t maxConns) : maxConns_(maxConns) {}

McServerSession& ConnectionTracker::add(
    folly::AsyncTransportWrapper::UniquePtr transport,
    std::shared_ptr<McServerOnRequest> cb,
    const AsyncMcServerWorkerOptions& options,
    void* userCtxt,
    const CompressionCodecMap* compressionCodecMap) {
  if (maxConns_ != 0 && sessions_.size() >= maxConns_) {
    evict();
  }

  auto& session = McServerSession::create(
      std::move(transport),
      std::move(cb),
      *this,
      options,
      userCtxt,
      compressionCodecMap);

  sessions_.push_front(session);

  return session;
}

void ConnectionTracker::closeAll() {
  // Closing a session might cause it to remove itself from sessions_,
  // so we should be careful with the iterator
  auto it = sessions_.begin();
  while (it != sessions_.end()) {
    auto& session = *it;
    ++it;
    session.beginClose("Shutting down");
  }
}

bool ConnectionTracker::writesPending() const {
  for (const auto& session : sessions_) {
    if (session.writesPending()) {
      return true;
    }
  }
  return false;
}

void ConnectionTracker::touch(McServerSession& session) {
  static uint64_t numCalls = 0;
  // Find the connection and bring it to the front of the LRU.
  // Do it only once in 16 requests because it's still expensive.
  if (((numCalls++) & 15) || !session.isLinked()) {
    return;
  }
  sessions_.erase(sessions_.iterator_to(session));
  sessions_.push_front(session);
}

void ConnectionTracker::evict() {
  if (sessions_.empty()) {
    return;
  }
  auto& session = sessions_.back();
  session.close();
}

void ConnectionTracker::onWriteQuiescence(McServerSession& session) {
  touch(session);
  if (onWriteQuiescence_) {
    onWriteQuiescence_(session);
  }
}

void ConnectionTracker::onCloseStart(McServerSession& session) {
  if (onCloseStart_) {
    onCloseStart_(session);
  }
}

void ConnectionTracker::onCloseFinish(McServerSession& session) {
  if (onCloseFinish_) {
    onCloseFinish_(session);
  }
  if (session.isLinked()) {
    sessions_.erase(sessions_.iterator_to(session));
  }
}

void ConnectionTracker::onShutdown() {
  if (onShutdown_) {
    onShutdown_();
  }
}

} // memcache
} // facebook
