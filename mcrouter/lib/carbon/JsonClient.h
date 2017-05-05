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

#include <folly/dynamic.h>
#include <folly/fibers/FiberManager.h>
#include <folly/io/async/EventBase.h>

#include "mcrouter/lib/network/AsyncMcClient.h"

namespace carbon {
namespace debug {

/**
 * Virtual class responsible for sending json requests to carbon servers.
 *
 * NOTE: This is not intented to be used in production. Is does slow json
 *       parsing/serialization. It's main purpose is to be used as a simple
 *       client tool, for testing/debugging purposes.
 */
class JsonClient {
 public:
  struct Options {
    /**
     * Hostname of the carbon server.
     */
    std::string host{"localhost"};

    /**
     * Port of the carbon server.
     */
    uint16_t port;

    /**
     * Server timout in milliseconds.
     */
    size_t serverTimeoutMs{200};

    /**
     * Whether of not parsing errors should be ignored (they will be displayed).
     */
    bool ignoreParsingErrors{true};
  };

  /**
   * Creates JsonClient object.
   * May throw std::bad_alloc if some allocation fails.
   */
  explicit JsonClient(Options options);
  virtual ~JsonClient() = default;

  /**
   * Send requests and return replies synchronously.
   *
   * @param requestName   The name of the request.
   * @param requests      Folly::dynamic containing either a single request, or
   *                      a list of requests.
   * @param replies       Output argument. Folly::dynamic containing the replies
   *                      to the requests present in "requests". If "requests"
   *                      contains multiple requests, the replies will contain a
   *                      json with all the replies, where the reply index will
   *                      match the corresponsing request index.
   *
   * @return              True if no errors are found.
   */
  bool sendRequests(
      const std::string& requestName,
      const folly::dynamic& requests,
      folly::dynamic& replies);

 protected:
  /**
   * Sends a single request synchronously
   *
   * @param requestName   Name of the request.
   * @param requestJson   Folly::dynamic containing a sinlge request to be sent.
   * @param replyJson     Output argument. Folly::dynmaic containing the reply
   *                      for the given request.
   *
   * @return              True if no errors are found.
   */
  virtual bool sendRequestByName(
      const std::string& requestName,
      const folly::dynamic& requestJson,
      folly::dynamic& replyJson) = 0;

  /**
   * Sends a request to the specified carbon server.
   *
   * @param requestJson   Json containing the request to be sent.
   * @param replyJson     Output parameter with the json containing the reply.
   *
   * @return              True if the request was sent and the reply received
   *                      successfully.
   */
  template <class Request>
  bool sendRequest(
      const folly::dynamic& requestJson,
      folly::dynamic& replyJson);

  const Options& options() const {
    return options_;
  }

 private:
  const Options options_;

  folly::EventBase evb_;
  facebook::memcache::AsyncMcClient client_;
  folly::fibers::FiberManager& fiberManager_;
};

} // debug
} // carbon

#include "JsonClient-inl.h"
