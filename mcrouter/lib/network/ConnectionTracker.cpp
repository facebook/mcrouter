/*
 *  Copyright (c) 2016, Facebook, Inc.
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

ConnectionTracker::ConnectionTracker(size_t maxConns)
  : maxConns_(maxConns) {
}

void ConnectionTracker::add(
    folly::AsyncTransportWrapper::UniquePtr transport,
    std::shared_ptr<McServerOnRequest> cb,
    std::function<void(McServerSession&)> onWriteQuiescence,
    std::function<void(McServerSession&)> onCloseStart,
    std::function<void(McServerSession&)> onCloseFinish,
    std::function<void()> onShutdown,
    AsyncMcServerWorkerOptions options,
    void* userCtxt,
    std::shared_ptr<Fifo> debugFifo) {
  if (maxConns_ != 0 && sessions_.size() >= maxConns_) {
    evict();
  }

  auto& session = McServerSession::create(
      std::move(transport),
      std::move(cb),
      [this, onWriteQuiescence = std::move(onWriteQuiescence)](
          McServerSession& session) {
        touch(session);
        if (onWriteQuiescence) {
          onWriteQuiescence(session);
        }
      },
      std::move(onCloseStart),
      [this, onCloseFinish = std::move(onCloseFinish)](
          McServerSession& session) {
        if (onCloseFinish) {
          onCloseFinish(session);
        }
        if (session.isLinked()) {
          sessions_.erase(sessions_.iterator_to(session));
        }
      },
      std::move(onShutdown),
      std::move(options),
      userCtxt,
      std::move(debugFifo));

  sessions_.push_front(session);
}

void ConnectionTracker::closeAll() {
  // Closing a session might cause it to remove itself from sessions_,
  // so we should be careful with the iterator
  auto it = sessions_.begin();
  while (it != sessions_.end()) {
    auto& session = *it;
    ++it;
    session.close();
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
  // Find the connection and bring it to the front of the LRU.
  if (!session.isLinked()) {
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

} // memcache
} // facebook
