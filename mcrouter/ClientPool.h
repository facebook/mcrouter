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
#include <vector>

#include "mcrouter/ProxyClientCommon.h"

namespace folly {
class dynamic;
}

namespace facebook { namespace memcache { namespace mcrouter {

/**
 * Stores list of clients and additional pool information
 */
class ClientPool {
 public:
  explicit ClientPool(std::string name);

  /**
   * Creates and adds new client to list of clients
   *
   * @param args  Arguments for ProxyClientCommon constructor
   *
   * @return  Created client
   */
  template <typename... Args>
  std::shared_ptr<ProxyClientCommon> emplaceClient(Args&&... args) {
    auto client = std::shared_ptr<ProxyClientCommon>(
      new ProxyClientCommon(*this, std::forward<Args>(args)...));
    clients_.push_back(client);
    return client;
  }

  const std::string& getName() const {
    return name_;
  }

  const std::vector<std::shared_ptr<ProxyClientCommon>>& getClients() const {
    return clients_;
  }

  void setWeights(folly::dynamic weights);

  folly::dynamic* getWeights() const;
 private:
  std::vector<std::shared_ptr<ProxyClientCommon>> clients_;
  std::unique_ptr<folly::dynamic> weights_;
  std::string name_;
};

}}}  // facebook::memcache::mcrouter
