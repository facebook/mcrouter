/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

#include <utility>

#include "mcrouter/lib/McReply.h"
#include "mcrouter/lib/McRequest.h"

namespace facebook { namespace memcache {

class McServerTransaction;

template <class OnRequest>
class McServerOnRequestWrapper;

/**
 * API for users of McServer to send back a reply for a request.
 *
 * Each onRequest callback is provided a reference to a context object,
 * which will stay alive until sendReply() is called on it.
 */
class McServerRequestContext {
 public:
  /**
   * Notify the server that the request-reply exchange is complete.
   *
   * @param msg  Reply to hand off.
   */
  void sendReply(McReply&& reply);

 private:
  McServerTransaction& transaction_;

  McServerRequestContext(const McServerRequestContext&) = delete;
  const McServerRequestContext& operator=(const McServerRequestContext&)
    = delete;
  McServerRequestContext(McServerRequestContext&&) = delete;
  const McServerRequestContext& operator=(McServerRequestContext&&) = delete;

  /* Only McServerTransaction can create these */
  friend class McServerTransaction;
  explicit McServerRequestContext(McServerTransaction& t)
      : transaction_(t) {}
};

/**
 * OnRequest callback interface. This is an implementation detail.
 */
class McServerOnRequest {
public:
  virtual ~McServerOnRequest() {}

private:
  McServerOnRequest() {}
  virtual void requestReady(McServerRequestContext& ctx,
                            const McRequest& req,
                            mc_op_t operation) = 0;

  friend class McServerTransaction;
  template <class OnRequest>
  friend class McServerOnRequestWrapper;
};

/**
 * Helper class to wrap user-defined callbacks in a correct virtual interface.
 * This is needed since we're mixing templates and virtual functions.
 */
template <class OnRequest>
class McServerOnRequestWrapper : public McServerOnRequest {
 public:
  template <typename... Args>
  explicit McServerOnRequestWrapper(Args&&... args)
      : onRequest_(std::forward<Args>(args)...) {
  }

 private:
  OnRequest onRequest_;

  void requestReady(McServerRequestContext& ctx, const McRequest& req,
                    mc_op_t operation);
  friend class McServerTransaction;
};

}}  // facebook::memcache

#include "McServerRequestContext-inl.h"
