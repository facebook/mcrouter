/*
 *  Copyright (c) 2016-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <chrono>
#include <iostream>

#include <folly/io/async/EventBase.h>
#include <folly/json.h>

#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/carbon/CarbonMessageConversionUtils.h"
#include "mcrouter/lib/network/AsyncMcClient.h"

namespace carbon {
namespace debug {

template <class Request>
bool JsonClient::sendRequest(
    const folly::dynamic& requestJson,
    folly::dynamic& replyJson) {
  bool hasErrors = false;
  auto onError = [this, &hasErrors](
      folly::StringPiece field, folly::StringPiece msg) {
    if (!hasErrors) {
      std::cerr << "Errors found when parsing json into " << Request::name
                << ":" << std::endl;
      hasErrors = true;
    }
    std::cerr << "\t" << field << ": " << msg << std::endl;
  };
  Request request;
  convertFromFollyDynamic(requestJson, request, std::move(onError));

  if (!options().ignoreParsingErrors && hasErrors) {
    std::cerr << "Found errors parsing json. Aborting." << std::endl;
    return false;
  }

  facebook::memcache::ReplyT<Request> reply;
  bool replyReceived = false;
  fiberManager_.addTask(
      [ this, request = std::move(request), &replyReceived, &reply ]() {
        reply = client_.sendSync(
            std::move(request),
            std::chrono::milliseconds(options().serverTimeoutMs));
        replyReceived = true;
      });

  while (!replyReceived) {
    evb_.loopOnce();
  }

  replyJson = convertToFollyDynamic(reply);
  return true;
}

} // debug
} // carbon
