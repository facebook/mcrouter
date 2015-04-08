/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "FailoverWithExptimeRoute.h"

namespace facebook { namespace memcache { namespace mcrouter {

namespace {

const char kFailoverHostPortSeparator = '@';
const std::string kFailoverTagStart = ":failover=";

}  // anonymous namespace

std::string FailoverWithExptimeRoute::keyWithFailoverTag(
    folly::StringPiece fullKey,
    const AccessPoint& ap) {
  const size_t tagLength =
    kFailoverTagStart.size() +
    ap.getHost().size() +
    6; // 1 for kFailoverHostPortSeparator + 5 for port.
  std::string failoverTag;
  failoverTag.reserve(tagLength);
  failoverTag = kFailoverTagStart;
  failoverTag += ap.getHost().str();
  if (ap.getPort() != 0) {
    failoverTag += kFailoverHostPortSeparator;
    failoverTag += folly::to<std::string>(ap.getPort());
  }

  // Safety check: scrub the host and port for ':' to avoid appending
  // more than one field to the key.
  // Note: we start after the ':failover=' part of the string,
  // since we need the initial ':' and we know the remainder is safe.
  for (size_t i = kFailoverTagStart.size(); i < failoverTag.size(); i++) {
    if (failoverTag[i] == ':') {
      failoverTag[i] = '$';
    }
  }

  return fullKey.str() + failoverTag;
}


FailoverWithExptimeRoute::FailoverWithExptimeRoute(
  RouteHandleFactory<McrouterRouteHandleIf>& factory,
  const folly::dynamic& json)
    : failoverExptime_(60) {

  checkLogic(json.isObject(), "FailoverWithExptimeRoute is not object");

  std::vector<McrouterRouteHandlePtr> failoverTargets;

  if (json.count("failover")) {
    failoverTargets = factory.createList(json["failover"]);
  }

  failover_ = FailoverRoute<McrouterRouteHandleIf>(std::move(failoverTargets));

  if (json.count("normal")) {
    normal_ = factory.create(json["normal"]);
  }

  if (json.count("failover_exptime")) {
    checkLogic(json["failover_exptime"].isInt(),
               "failover_exptime is not integer");
    failoverExptime_ = json["failover_exptime"].asInt();
  }

  if (json.count("settings")) {
    settings_ = FailoverWithExptimeSettings(json["settings"]);
  }
}

McrouterRouteHandlePtr makeFailoverWithExptimeRoute(
  McrouterRouteHandlePtr normalTarget,
  std::vector<McrouterRouteHandlePtr> failoverTargets,
  uint32_t failoverExptime,
  FailoverWithExptimeSettings settings) {

  return std::make_shared<McrouterRouteHandle<FailoverWithExptimeRoute>>(
    std::move(normalTarget),
    std::move(failoverTargets),
    failoverExptime,
    std::move(settings));
}

McrouterRouteHandlePtr makeFailoverWithExptimeRoute(
  RouteHandleFactory<McrouterRouteHandleIf>& factory,
  const folly::dynamic& json) {

  return std::make_shared<McrouterRouteHandle<FailoverWithExptimeRoute>>(
    factory, json);
}

}}}  // facebook::memcache::mcrouter
