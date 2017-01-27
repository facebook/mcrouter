/*
 *  Copyright (c) 2017, Facebook, Inc.
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

#include "mcrouter/lib/McResUtil.h"
#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/carbon/RoutingGroups.h"
#include "mcrouter/lib/mc/msg.h"

namespace folly {
struct dynamic;
} // folly

namespace facebook {
namespace memcache {

class FailoverErrorsSettings {
 public:
  FailoverErrorsSettings() = default;
  explicit FailoverErrorsSettings(std::vector<std::string> errors);
  FailoverErrorsSettings(
      std::vector<std::string> errorsGet,
      std::vector<std::string> errorsUpdate,
      std::vector<std::string> errorsDelete);
  explicit FailoverErrorsSettings(const folly::dynamic& json);

  template <class Request>
  bool shouldFailover(
      const ReplyT<Request>& reply,
      const Request&,
      carbon::DeleteLikeT<Request> = 0) const {
    return deletes_.shouldFailover(reply.result());
  }

  template <class Request>
  bool shouldFailover(
      const ReplyT<Request>& reply,
      const Request&,
      carbon::GetLikeT<Request> = 0) const {
    return gets_.shouldFailover(reply.result());
  }

  template <class Request>
  bool shouldFailover(
      const ReplyT<Request>& reply,
      const Request&,
      carbon::UpdateLikeT<Request> = 0) const {
    return updates_.shouldFailover(reply.result());
  }

  template <class Request>
  bool shouldFailover(
      const ReplyT<Request>& reply,
      const Request&,
      carbon::OtherThanT<
          Request,
          carbon::DeleteLike<>,
          carbon::GetLike<>,
          carbon::UpdateLike<>> = 0) const {
    return isFailoverErrorResult(reply.result());
  }

  class List {
   public:
    List() = default;
    explicit List(std::vector<std::string> errors);
    explicit List(const folly::dynamic& json);

    bool shouldFailover(const mc_res_t result) const;

   private:
    std::unique_ptr<std::array<bool, mc_nres>> failover_;

    void init(std::vector<std::string> errors);
  };

 private:
  FailoverErrorsSettings::List gets_;
  FailoverErrorsSettings::List updates_;
  FailoverErrorsSettings::List deletes_;
};
}
} // facebook::memcache
