/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

namespace facebook {
namespace memcache {

class HostInfo;
using HostInfoPtr = std::shared_ptr<HostInfo>;

class HostInfoLocation {
 public:
  HostInfoLocation(){};

  const std::string& getIp() const {
    return ip_;
  }
  uint16_t getPort() const {
    return 0;
  }
  const uint16_t* getTWTaskID() const {
    return nullptr;
  }

 private:
  const std::string ip_ = "127.0.0.1";
};

class HostInfo {
 public:
  HostInfo(){};

  const HostInfoLocation& location() {
    return location_;
  }

 private:
  const HostInfoLocation location_;
};

} // namespace memcache
} // namespace facebook
