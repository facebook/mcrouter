/*
 *  Copyright (c) 2017-present, Facebook, Inc.
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
} // namespace folly

namespace facebook {
namespace memcache {

class FailoverErrorsSettingsBase {
 public:
  FailoverErrorsSettingsBase() = default;
  explicit FailoverErrorsSettingsBase(std::vector<std::string> errors);
  FailoverErrorsSettingsBase(
      std::vector<std::string> errorsGet,
      std::vector<std::string> errorsUpdate,
      std::vector<std::string> errorsDelete);
  explicit FailoverErrorsSettingsBase(const folly::dynamic& json);

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

  enum class FailoverType {
    NONE,
    NORMAL,
    CONDITIONAL,
  };

 protected:
  FailoverErrorsSettingsBase::List gets_;
  FailoverErrorsSettingsBase::List updates_;
  FailoverErrorsSettingsBase::List deletes_;
};
} // namespace memcache
} // namespace facebook
