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

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "mcrouter/lib/McReply.h"
#include "mcrouter/lib/mc/msg.h"
#include "mcrouter/lib/OperationTraits.h"

namespace folly {
class dynamic;
}

namespace facebook { namespace memcache {

class FailoverErrorsSettings {
 public:
  class List;
  FailoverErrorsSettings() = default;
  explicit FailoverErrorsSettings(std::vector<std::string> errors);
  FailoverErrorsSettings(std::vector<std::string> errorsGet,
                         std::vector<std::string> errorsUpdate,
                         std::vector<std::string> errorsDelete);
  FailoverErrorsSettings(List&& listGet,
                         List&& listUpdate,
                         List&& listDelete);
  explicit FailoverErrorsSettings(const folly::dynamic& json);

  template <class Operation>
  bool shouldFailover(const McReply& reply, Operation) const {
    if (GetLike<Operation>::value) {
      return gets_.shouldFailover(reply);
    } else if (UpdateLike<Operation>::value) {
      return updates_.shouldFailover(reply);
    } else if (DeleteLike<Operation>::value) {
      return deletes_.shouldFailover(reply);
    }
    return reply.isFailoverError();
  }

  template <class Operation>
  uint32_t failbackDelay(Operation) const {
    if (GetLike<Operation>::value) {
      return gets_.failbackDelay();
    } else if (UpdateLike<Operation>::value) {
      return updates_.failbackDelay();
    } else if (DeleteLike<Operation>::value) {
      return deletes_.failbackDelay();
    }
    return 0;
  }

  class List {
   public:
    List() = default;
    explicit List(std::vector<std::string> errors);
    explicit List(std::vector<std::string> errors,
                  uint32_t failback_delay);
    explicit List(const folly::dynamic& json);

    bool shouldFailover(const McReply& reply) const;
    uint32_t failbackDelay() const;

   private:
    std::unique_ptr<std::array<bool, mc_nres>> failover_;
    uint32_t failback_delay_ = 0;

    void init(std::vector<std::string> errors);
  };

 private:
  FailoverErrorsSettings::List gets_;
  FailoverErrorsSettings::List updates_;
  FailoverErrorsSettings::List deletes_;
};

}} // facebook::memcache
