/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "FailoverWithExptimeRouteIf.h"

#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/lib/mc/msg.h"

namespace facebook { namespace memcache { namespace mcrouter {

namespace {

void appendErrors(const std::vector<mc_res_t>& errors,
                  std::vector<std::string>& vec) {
  for (const auto error : errors) {
    folly::StringPiece name(mc_res_to_string(error));
    name.removePrefix("mc_res_");
    vec.push_back(name.str());
  }
}

void appendErrors(const FailoverWithExptimeSettings::OperationSettings& config,
                  std::vector<mc_res_t> errors,
                  std::vector<std::string>& gets,
                  std::vector<std::string>& updates,
                  std::vector<std::string>& deletes) {
  if (config.gets) {
    appendErrors(errors, gets);
  }
  if (config.updates) {
    appendErrors(errors, updates);
  }
  if (config.deletes) {
    appendErrors(errors, deletes);
  }
}

} // anonymous namespace

void FailoverWithExptimeSettings::OperationSettings::override(
      const folly::dynamic& json) {

  checkLogic(json.isBool() || json.isObject(),
             "FailoverWithExptime::OperationSettings is not bool/object");
  if (json.isBool()) {
    gets = updates = deletes = json.asBool();
    return;
  }

  if (json.count("gets")) {
    checkLogic(json["gets"].isBool(),
               "FailoverWithExptime::OperationSettings::gets is not bool");
    gets = json["gets"].asBool();
  }

  if (json.count("updates")) {
    checkLogic(json["updates"].isBool(),
               "FailoverWithExptime::OperationSettings::updates is not bool");
    updates = json["updates"].asBool();
  }

  if (json.count("deletes")) {
    checkLogic(json["deletes"].isBool(),
               "FailoverWithExptime::OperationSettings::deletes is not bool");
    deletes = json["deletes"].asBool();
  }
}

void FailoverWithExptimeSettings::OperationSettings::disable() {
  gets = false;
  updates = false;
  deletes = false;
}

FailoverWithExptimeSettings::FailoverWithExptimeSettings(
      const folly::dynamic& json) {

  checkLogic(json.isObject(), "FailoverWithExptimeSettings is not object");

  // assume json itself contains default operations (gets, updates, etc.)
  tko.override(json);
  connectTimeout.override(json);
  dataTimeout.override(json);

  if (json.count("tko")) {
    tko.override(json["tko"]);
  }

  if (json.count("connectTimeout")) {
    connectTimeout.override(json["connectTimeout"]);
  }

  if (json.count("dataTimeout")) {
    dataTimeout.override(json["dataTimeout"]);
  }

  if (json.count("failover_tag")) {
    checkLogic(json["failover_tag"].isBool(),
               "FailoverWithExptime: failover_tag is not bool");
    failoverTagging = json["failover_tag"].asBool();
  }
}

FailoverErrorsSettings FailoverWithExptimeSettings::getFailoverErrors() const {
  std::vector<std::string> gets, updates, deletes;

  appendErrors(tko,
      { mc_res_tko, mc_res_connect_error, mc_res_busy, mc_res_try_again },
      gets, updates, deletes);

  appendErrors(connectTimeout, { mc_res_connect_timeout },
      gets, updates, deletes);

  appendErrors(dataTimeout, { mc_res_timeout, mc_res_remote_error },
      gets, updates, deletes);

  // Append all other failover errors.
  appendErrors(FailoverWithExptimeSettings::OperationSettings(),
      { mc_res_shutdown, mc_res_local_error },
      gets, updates, deletes);

  return FailoverErrorsSettings(std::move(gets),
                                std::move(updates),
                                std::move(deletes));
}

}}}  // facebook::memcache::mcrouter
