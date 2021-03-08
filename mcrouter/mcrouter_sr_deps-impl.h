/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

namespace facebook {
namespace memcache {

struct SRHost {
 public:
  SRHost(){};
  const std::string& getIp() const {
    return "127.0.0.1";
  }
  uint16_t getPort() const {
    return 0;
  }
  const std::optional<uint16_t> getTwTaskId() const {
    return std::nullopt;
  }
};

} // namespace memcache
} // namespace facebook
