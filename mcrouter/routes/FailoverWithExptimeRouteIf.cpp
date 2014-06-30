/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include "FailoverWithExptimeRouteIf.h"

#include "mcrouter/lib/fbi/cpp/util.h"

namespace facebook { namespace memcache { namespace mcrouter {

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

}}}  // facebook::memcache::mcrouter
