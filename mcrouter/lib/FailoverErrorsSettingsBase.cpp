/*
 *  Copyright (c) 2017-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "FailoverErrorsSettingsBase.h"

#include <memory>
#include <vector>

#include <folly/dynamic.h>

#include "mcrouter/lib/McResUtil.h"
#include "mcrouter/lib/fbi/cpp/util.h"

namespace facebook {
namespace memcache {

FailoverErrorsSettingsBase::List::List(std::vector<std::string> errors) {
  init(std::move(errors));
}

FailoverErrorsSettingsBase::List::List(const folly::dynamic& json) {
  checkLogic(json.isArray(), "List of failover errors is not an array.");

  std::vector<std::string> errors;
  errors.reserve(json.size());
  for (const auto& elem : json) {
    checkLogic(elem.isString(), "Failover error {} is not a string", elem);
    errors.push_back(elem.getString());
  }

  init(std::move(errors));
}

bool FailoverErrorsSettingsBase::List::shouldFailover(
    const mc_res_t result) const {
  if (failover_ != nullptr) {
    return (*failover_)[result];
  }
  return isFailoverErrorResult(result);
}

void FailoverErrorsSettingsBase::List::init(std::vector<std::string> errors) {
  failover_ = std::make_unique<std::array<bool, mc_nres>>();

  for (const auto& error : errors) {
    int i;
    for (i = 0; i < mc_nres; ++i) {
      mc_res_t errorType = static_cast<mc_res_t>(i);
      folly::StringPiece errorName(mc_res_to_string(errorType));
      errorName.removePrefix("mc_res_");

      if (mc_res_is_err(errorType) && error == errorName) {
        (*failover_)[i] = true;
        break;
      }
    }

    checkLogic(
        i < mc_nres, "Failover error '{}' is not a valid error type.", error);
  }
}

FailoverErrorsSettingsBase::FailoverErrorsSettingsBase(
    std::vector<std::string> errors)
    : gets_(errors), updates_(errors), deletes_(std::move(errors)) {}

FailoverErrorsSettingsBase::FailoverErrorsSettingsBase(
    std::vector<std::string> errorsGet,
    std::vector<std::string> errorsUpdate,
    std::vector<std::string> errorsDelete)
    : gets_(std::move(errorsGet)),
      updates_(std::move(errorsUpdate)),
      deletes_(std::move(errorsDelete)) {}

FailoverErrorsSettingsBase::FailoverErrorsSettingsBase(
    const folly::dynamic& json) {
  checkLogic(
      json.isObject() || json.isArray(),
      "Failover errors must be either an array or an object.");

  if (json.isObject()) {
    if (auto jsonGets = json.get_ptr("gets")) {
      gets_ = FailoverErrorsSettingsBase::List(*jsonGets);
    }
    if (auto jsonUpdates = json.get_ptr("updates")) {
      updates_ = FailoverErrorsSettingsBase::List(*jsonUpdates);
    }
    if (auto jsonDeletes = json.get_ptr("deletes")) {
      deletes_ = FailoverErrorsSettingsBase::List(*jsonDeletes);
    }
  } else if (json.isArray()) {
    gets_ = FailoverErrorsSettingsBase::List(json);
    updates_ = FailoverErrorsSettingsBase::List(json);
    deletes_ = FailoverErrorsSettingsBase::List(json);
  }
}

} // namespace memcache
} // namespace facebook
