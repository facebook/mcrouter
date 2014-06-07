/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include "folly/dynamic.h"
#include "mcrouter/lib/config/RouteHandleProviderIf.h"
#include "mcrouter/lib/fbi/cpp/util.h"

namespace facebook { namespace memcache {

template <class RouteHandleIf>
RouteHandleFactory<RouteHandleIf>::RouteHandleFactory(
    RouteHandleProviderIf<RouteHandleIf>& provider,
    OnCreateCallback onCreateRoot)
    : provider_(provider),
      onCreateRoot_(std::move(onCreateRoot)) {
}

template <class RouteHandleIf>
std::shared_ptr<RouteHandleIf>
RouteHandleFactory<RouteHandleIf>::createRoot(const folly::dynamic& json) {
  if (onCreateRoot_) {
    return onCreateRoot_(create(json));
  }
  return create(json);
}

template <class RouteHandleIf>
std::shared_ptr<RouteHandleIf>
RouteHandleFactory<RouteHandleIf>::create(const folly::dynamic& json) {
  auto result = createList(json);

  checkLogic(result.size() == 1, "{} RouteHandles in list, expected 1",
             result.size());

  return result.back();
}

template <class RouteHandleIf>
std::vector<std::shared_ptr<RouteHandleIf>>
RouteHandleFactory<RouteHandleIf>::createList(const folly::dynamic& json) {
  if (json.isArray()) {
    std::vector<std::shared_ptr<RouteHandleIf>> ret;
    // merge all inner lists into result
    for (const auto& it : json) {
      auto list = createList(it);
      ret.insert(ret.end(), list.begin(), list.end());
    }
    return ret;
  } else if (json.isObject()) {
    checkLogic(json.count("type"), "No type field in RouteHandle json object");
    checkLogic(json["type"].isString(),
               "Type field in RouteHandle is not string");

    auto type = json["type"].asString().toStdString();
    auto name = (json.count("name") && json["name"].isString())
      ? json["name"].asString().toStdString()
      : "";

    if (!name.empty()) {
      // got named handle
      auto it = seen_.find(name);
      if (it != seen_.end()) {
        // we had same named handle already. Reuse it.
        return it->second;
      }

      auto ret = provider_.create(*this, type, json);
      seen_.emplace(name, ret);
      return ret;
    } else {
      return provider_.create(*this, type, json);
    }
  } else if (json.isString()) {
    auto handleString = json.asString().toStdString();

    if (handleString.empty()) {
      // useful for routes with optional children
      return {};
    }

    // check if we already parsed same string. It can be named handle or short
    // form of handle.
    auto it = seen_.find(handleString);
    if (it != seen_.end()) {
      return it->second;
    }

    std::vector<std::shared_ptr<RouteHandleIf>> ret;
    auto pipeId = handleString.find("|");
    if (pipeId != -1) { // short form (e.g. HashRoute|ErrorRoute)
      auto type = handleString.substr(0, pipeId); // split by first '|'
      auto def = handleString.substr(pipeId + 1);
      ret = provider_.create(*this, type, def);
    } else {
      // assume it is short form of route without children (e.g. ErrorRoute)
      ret = provider_.create(*this, handleString, nullptr);
    }

    seen_.emplace(handleString, ret);
    return ret;
  }
  throw std::logic_error("RouteHandle should be object, array or string");
}

}}  // facebook::memcache
