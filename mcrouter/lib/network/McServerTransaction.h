/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

#include "folly/IntrusiveList.h"
#include "folly/Optional.h"
#include "mcrouter/lib/mc/ascii_response.h"
#include "mcrouter/lib/mc/umbrella.h"
#include "mcrouter/lib/McRequest.h"
#include "mcrouter/lib/network/McServerRequestContext.h"
#include "mcrouter/lib/network/UniqueIntrusiveList.h"

namespace facebook { namespace memcache {

class McServerSession;

/**
 * An object that tracks a lifetime of a single request/reply pair.
 *
 * Needed for buffer storage.
 */
class McServerTransaction {
 private:
  UniqueIntrusiveListHook hook_;

 public:
  /**
   * A queue of transactions.
   */
  using Queue = UniqueIntrusiveList<McServerTransaction,
                                    &McServerTransaction::hook_>;

  /**
   * @param session
   * @param queue         Which queue to add the new transaction.
   * @param msg           Incoming request
   * @param reqid         Protocol level request id
   * @param isMultiget    If true, this is the container multiget request,
   *                      and shouldn't be forwarded to the application.
   * @param isSubRequest  If true, this is a subrequest contained within
   *                      another transaction and shouldn't be counted
   *                      towards an in flight total.
   *                      Note: a subrequest _must_ be written to socket
   *                      before its parent request.
   */
  McServerTransaction(std::shared_ptr<McServerSession> session,
                      McRequest&& request,
                      mc_op_t operation,
                      uint64_t reqid,
                      bool isMultiget = false,
                      bool isSubRequest = false);

  ~McServerTransaction();

  /**
   * Application-visible context
   */
  McServerRequestContext& appContext() {
    return appContext_;
  }

  /**
   * Start writing out the reply and clean up on completion.
   */
  void sendReply(McReply&& reply);

  /**
   * True if this request already has a reply ready to be sent.
   * Note: a multiget request might have a saved error reply that's not
   * yet ready to be sent, since it's waiting for all subrequests to complete.
   */
  bool replyReady() const {
    return replyReady_;
  }

  /* Multiget logic */

  /**
   * Add a multiget subrequest
   */
  void pushMultigetRequest(std::unique_ptr<McServerTransaction> req);

  /**
   * Call the callback on this request
   */
  void dispatchRequest(McServerOnRequest& cb);

  /**
   * Call the callback on all queued up subrequests
   */
  void dispatchSubRequests(McServerOnRequest& cb);

  /**
   * Write out any sub-replies (as in multiget) to the transport.
   */
  void queueSubRequestsWrites();

 private:
  std::shared_ptr<McServerSession> session_;
  McRequest request_;
  mc_op_t operation_;
  uint64_t reqid_;

  McServerRequestContext appContext_;

  folly::Optional<McReply> reply_;
  bool replyReady_{false};

  /* Multiget state */
  bool isMultiget_{false};  /**< True on the parent request only */
  bool isSubRequest_{false};  /**< True on the child requests only */
  McServerTransaction* multigetParent_{nullptr}; /**< For subrequests,
                                                      link to parent */
  size_t numMultiget_{0};  /**< Number of multiget subRequests not replied */
  McServerTransaction::Queue subRequests_;  /**< For parent, all subRequests */

  /* Write buffers */
  mc_msg_t replyMsg_;
  um_backing_msg_t umMsg_;
  mc_ascii_response_buf_t asciiResponse_;
  static const size_t kMaxIovs = 16;
  size_t niovs_;
  struct iovec iovs_[kMaxIovs];

  void onMultigetReply(McReply& reply);

  bool prepareWrite();
  void buildResponseText();
  void buildResponseUmbrella();

  McServerTransaction(const McServerTransaction&) = delete;
  McServerTransaction& operator=(const McServerTransaction&) = delete;

  friend class McServerSession;
};

}}  // facebook::memcache
