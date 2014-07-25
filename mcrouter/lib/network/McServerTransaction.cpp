/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include "McServerTransaction.h"

#include "mcrouter/lib/network/McServerSession.h"

namespace facebook { namespace memcache {

McServerTransaction::McServerTransaction(
  std::shared_ptr<McServerSession> session,
  McRequest&& request,
  mc_op_t operation,
  uint64_t reqid,
  bool isMultiget,
  bool isSubRequest,
  bool noreply)
    : session_(std::move(session)),
      request_(std::move(request)),
      operation_(operation),
      reqid_(reqid),
      appContext_(*this),
      noReply_(noreply),
      isMultiget_(isMultiget),
      isSubRequest_(isSubRequest) {

  mc_msg_init_not_refcounted(&replyMsg_);

  um_backing_msg_init(&umMsg_);
  mc_ascii_response_buf_init(&asciiResponse_);

  assert(!(isMultiget_ && isSubRequest));
  session_->onTransactionStarted(isSubRequest_);
}

McServerTransaction::~McServerTransaction() {
  session_->onTransactionCompleted(isSubRequest_);

  um_backing_msg_cleanup(&umMsg_);
  mc_ascii_response_buf_cleanup(&asciiResponse_);
}

void McServerTransaction::onMultigetReply(McReply& subReply) {
  assert(isMultiget_);

  /* If not a hit or a miss, and we didn't store a reply yet,
     steal it */
  if (!(subReply.result() == mc_res_found ||
        subReply.result() == mc_res_notfound) &&
      (!reply_.hasValue() ||
       (subReply.result() > reply_->result()))) {
    reply_ = std::move(subReply);
  }

  assert(numMultiget_ > 0);
  --numMultiget_;

  if (!numMultiget_) {
    if (!reply_.hasValue()) {
      reply_ = McReply(mc_res_found);
    }
    replyReady_ = true;
    session_->onRequestReplied(*this);
  }
}

void McServerTransaction::sendReply(McReply&& reply) {
  assert(!replyReady_);
  assert(!reply_.hasValue());

  reply_ = std::move(reply);
  replyReady_ = true;

  if (multigetParent_) {
    multigetParent_->onMultigetReply(*reply_);
  } else {
    session_->onRequestReplied(*this);
  }
}

void McServerTransaction::pushMultigetRequest(
  std::unique_ptr<McServerTransaction> req) {
  assert(isMultiget_);

  req->multigetParent_ = this;
  ++numMultiget_;
  subRequests_.pushBack(std::move(req));
}

void McServerTransaction::dispatchRequest(McServerOnRequest& cb) {
  cb.requestReady(appContext_, request_, operation_);
}

void McServerTransaction::dispatchSubRequests(McServerOnRequest& cb) {
  assert(isMultiget_);

  auto it = subRequests_.begin();
  while (it != subRequests_.end()) {
    /* Watch out for destruction from requestReady() stack below */
    auto& req = *it;
    ++it;
    /* It's possible that we filled out the reply already,
       like for mc_res_bad_key */
    if (req.replyReady_) {
      onMultigetReply(*req.reply_);
    } else {
      cb.requestReady(req.appContext_, req.request_, req.operation_);
    }
  }
}

void McServerTransaction::queueSubRequestsWrites() {
  assert(replyReady_);

  if (!isMultiget_) {
    return;
  }

  assert(!numMultiget_);
  while (!subRequests_.empty()) {
    auto req = subRequests_.popFront();

    /* Only write if we didn't already save an error reply
       and the reply is a hit */

    if (reply_->result() == mc_res_found &&
        (req->reply_->result() == mc_res_found ||
         !(req->operation_ == mc_op_get ||
           req->operation_ == mc_op_metaget ||
           req->operation_ == mc_op_gets))) {
      session_->queueWrite(std::move(req));
    }
  }
}

bool McServerTransaction::prepareWrite() {
  assert(reply_.hasValue());

  reply_->dependentMsg(operation_, &replyMsg_);

  if (session_->parser_.protocol() == mc_ascii_protocol) {
    buildResponseText();
  } else if (session_->parser_.protocol() == mc_umbrella_protocol) {
    buildResponseUmbrella();
  } else {
    CHECK(false) << "Unknown protocol";
  }

  return niovs_ != 0;
}

void McServerTransaction::buildResponseText() {
  niovs_ = mc_ascii_response_write_iovs(&asciiResponse_,
                                        request_.dependentMsg(operation_).get(),
                                        &replyMsg_,
                                        iovs_,
                                        kMaxIovs);
}

void McServerTransaction::buildResponseUmbrella() {
  auto niovs = um_write_iovs(&umMsg_, reqid_,
                             &replyMsg_,
                             iovs_, kMaxIovs);
  niovs_ = (niovs == -1 ? 0 : niovs);
}

}}  // facebook::memcache
