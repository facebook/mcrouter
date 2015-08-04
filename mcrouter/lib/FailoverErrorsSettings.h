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
  FailoverErrorsSettings() = default;
  explicit FailoverErrorsSettings(std::vector<std::string> errors);
  FailoverErrorsSettings(std::vector<std::string> errorsGet,
                         std::vector<std::string> errorsUpdate,
                         std::vector<std::string> errorsDelete);
  explicit FailoverErrorsSettings(const folly::dynamic& json);

  template <class Operation>
  bool shouldFailover(const McReply& reply, Operation,
                      typename DeleteLike<Operation>::Type = 0) const {
    return deletes_.shouldFailover(reply);
  }

  template <class Operation>
  bool shouldFailover(const McReply& reply, Operation,
                      typename GetLike<Operation>::Type = 0) const {
    return gets_.shouldFailover(reply);
  }

  template <class Operation>
  bool shouldFailover(const McReply& reply, Operation,
                      typename UpdateLike<Operation>::Type = 0) const {
    return updates_.shouldFailover(reply);
  }

  template <class Operation>
  bool shouldFailover(const McReply& reply, Operation,
                      OtherThanT(Operation,
                                 DeleteLike<>,
                                 GetLike<>,
                                 UpdateLike<>) = 0) const {
    return reply.isFailoverError();
  }

  class List {
   public:
    List() = default;
    explicit List(std::vector<std::string> errors);
    explicit List(const folly::dynamic& json);

    bool shouldFailover(const McReply& reply) const;

   private:
    std::unique_ptr<std::array<bool, mc_nres>> failover_;

    void init(std::vector<std::string> errors);
  };

 private:
  FailoverErrorsSettings::List gets_;
  FailoverErrorsSettings::List updates_;
  FailoverErrorsSettings::List deletes_;
};

}} // facebook::memcache
