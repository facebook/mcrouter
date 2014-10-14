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

template <class OnRequest>
class McServerOnRequestWrapper;
class McServerTransaction;

/**
 * API for users of McServer to send back a reply for a request.
 *
 * Each onRequest callback is provided a context object,
 * which must eventually be surrendered back via a reply() call.
 */
class McServerRequestContext {
 public:
  /**
   * Notify the server that the request-reply exchange is complete.
   *
   * @param ctx  The original context.
   * @param msg  Reply to hand off.
   */
  static void reply(McServerRequestContext&& ctx, McReply&& reply);

  ~McServerRequestContext() {
    /* Check that a reply was returned */
    assert(!transaction_);
  }

  McServerRequestContext(McServerRequestContext&& other) noexcept
      : transaction_(other.transaction_) {
    other.transaction_ = nullptr;
  }

 private:
  McServerTransaction* transaction_{nullptr};

  McServerRequestContext(const McServerRequestContext&) = delete;
  const McServerRequestContext& operator=(const McServerRequestContext&)
    = delete;
  const McServerRequestContext& operator=(
    McServerRequestContext&& other) = delete;

  /* Only McServerTransaction can create these */
  friend class McServerTransaction;
  explicit McServerRequestContext(McServerTransaction& t)
      : transaction_(&t) {}
};

/**
 * OnRequest callback interface. This is an implementation detail.
 */
class McServerOnRequest {
public:
  virtual ~McServerOnRequest() {}

private:
  McServerOnRequest() {}
  virtual void requestReady(McServerRequestContext&& ctx,
                            McRequest&& req,
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

  void requestReady(McServerRequestContext&& ctx, McRequest&& req,
                    mc_op_t operation);
  friend class McServerTransaction;
};

}}  // facebook::memcache

#include "McServerRequestContext-inl.h"
