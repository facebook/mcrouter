/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include "McSerializedRequest.h"

#include "mcrouter/lib/McMsgRef.h"
#include "mcrouter/lib/McRequest.h"

namespace facebook { namespace memcache {

McSerializedRequest::McSerializedRequest(const McRequest& req,
                                         mc_op_t operation,
                                         size_t reqId,
                                         mc_protocol_t protocol)
  : protocol_(protocol), result_(Result::OK) {

  auto msg = req.dependentMsg(operation);
  if (mc_client_req_check(msg.get()) != mc_req_err_valid ||
      (protocol_ == mc_ascii_protocol && msg->key.len > MC_KEY_MAX_LEN_ASCII)) {
    result_ = Result::BAD_KEY;
    return;
  }

  switch (protocol_) {
    case mc_ascii_protocol:
      serializeMcMsgAscii(msg);
      break;
    case mc_umbrella_protocol:
      um_backing_msg_init(&umBackingMsg_);
      serializeMcMsgUmbrella(msg, reqId);
      if (result_ != Result::OK) {
        um_backing_msg_cleanup(&umBackingMsg_);
      }
      break;
    case mc_unknown_protocol:
    case mc_binary_protocol:
    case mc_nprotocols:
      checkLogic(false, "Used unsupported protocol! Value: {}", (int)protocol_);
      result_ = Result::ERROR;
      iovsCount_ = 0;
  }
}

McSerializedRequest::~McSerializedRequest() {
  asciiBuffer_.reset();

  if (result_ == Result::OK && protocol_ == mc_umbrella_protocol) {
    um_backing_msg_cleanup(&umBackingMsg_);
  }
}

McSerializedRequest::Result McSerializedRequest::serializationResult() const {
  return result_;
}

void McSerializedRequest::serializeMcMsgAscii(const McMsgRef& req) {
  size_t hdrLength = mc_ascii_req_max_hdr_length(req.get());

  asciiBuffer_ = std::unique_ptr<char[]>(new char[hdrLength]);

  int r = mc_serialize_req_ascii(req.get(), asciiBuffer_.get(), hdrLength,
    iovs_, kMaxIovs);

  if (r <= 0) {
    result_ = Result::ERROR;
  } else {
    result_ = Result::OK;
    iovsCount_ = r;
  }
}

void McSerializedRequest::serializeMcMsgUmbrella(const McMsgRef& req,
                                                 size_t reqId) {
  // Umbrella serializer doesn't work with McMsgRef and it will need to
  // increment refcount.
  auto msg = const_cast<mc_msg_t*>(req.get());
  ssize_t r = um_write_iovs(&umBackingMsg_, reqId, msg, iovs_, kMaxIovs);

  if (r <= 0) {
    result_ = Result::ERROR;
  } else {
    result_ = Result::OK;
    iovsCount_ = r;
  }
}

}} // facebook::memcache
