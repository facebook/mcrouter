/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include "mcrouter/lib/network/McProtocolSerializer.h"

#include "folly/io/IOBuf.h"

namespace facebook { namespace memcache {

McProtocolSerializer::McProtocolSerializer(mc_protocol_t protocol,
  std::function<void(uint64_t, McMsgRef&&)> onReady,
  std::function<void(parser_error_t)> onError) :
    protocol_(protocol),
    onReady_(std::move(onReady)),
    onError_(std::move(onError)) {
  assert(protocol_ == mc_ascii_protocol || protocol_ == mc_umbrella_protocol);

  mc_parser_init(&parser_, reply_parser, &parserMsgReady, &parserParseError,
                 this);
}

McProtocolSerializer::~McProtocolSerializer() {
  mc_parser_reset(&parser_);
}

McProtocolSerializer& McProtocolSerializer::operator=(
    McProtocolSerializer&& other) {
  std::swap(protocol_, other.protocol_);
  std::swap(parser_, other.parser_);
  std::swap(onReady_, other.onReady_);
  std::swap(onError_, other.onError_);
  // We still need to retain correct context.
  std::swap(parser_.context, other.parser_.context);
  return *this;
}

McProtocolSerializer::Result McProtocolSerializer::serialize(
    const McMsgRef& req, size_t reqId, RequestContext& ctx) const {
  if (!mc_client_req_is_valid(req.get()) ||
      (protocol_ == mc_ascii_protocol && req->key.len > MC_KEY_MAX_LEN_ASCII)) {
    return Result::BAD_KEY;
  }

  switch (protocol_) {
    case mc_ascii_protocol:
      ctx.serializeMcMsgAscii(req);
      break;
    case mc_umbrella_protocol:
      ctx.serializeMcMsgUmbrella(req, reqId);
      break;
    case mc_unknown_protocol:
    case mc_binary_protocol:
    case mc_nprotocols:
      // We already assert for them in constructor.
      ctx.iovsCount_ = 0;
  }
  return ctx.iovsCount_ != 0 ? Result::OK : Result::ERROR;
}

void McProtocolSerializer::readData(std::unique_ptr<folly::IOBuf>&& data) {
  auto bytes = data->coalesce();
  mc_parser_parse(&parser_, bytes.begin(), bytes.size());
}

void McProtocolSerializer::parserMsgReady(void* context, uint64_t reqId,
                                          mc_msg_t* msg) {
  reinterpret_cast<McProtocolSerializer*>(context)->onReady_(
    reqId, McMsgRef::moveRef(msg));
}

void McProtocolSerializer::parserParseError(void* context,
    parser_error_t error) {
  auto serializer_ = reinterpret_cast<McProtocolSerializer*>(context);
  serializer_->onError_(error);
  // Prevent parser from calling onError twice.
  serializer_->parser_.parse_error = &parserParseErrorSilent;
}

void McProtocolSerializer::parserParseErrorSilent(void* context,
    parser_error_t error) {
}

McProtocolSerializer::RequestContext::RequestContext() {
  um_backing_msg_init(&umBackingMsg_);
}

McProtocolSerializer::RequestContext::~RequestContext() {
  um_backing_msg_cleanup(&umBackingMsg_);
}

void McProtocolSerializer::RequestContext::serializeMcMsgAscii(
    const McMsgRef& req) {
  // We need to ensure that this message lives as long as we have iovs.
  msg_ = req.clone();

  size_t hdrLength = mc_ascii_req_max_hdr_length(msg_.get());

  asciiBuffer_ = std::unique_ptr<char[]>(new char[hdrLength]);

  int r = mc_serialize_req_ascii(msg_.get(), asciiBuffer_.get(), hdrLength,
    iovs_, kMaxIovs);

  iovsCount_ = r < 0 ? 0 : r;
}

void McProtocolSerializer::RequestContext::serializeMcMsgUmbrella(
    const McMsgRef& req, size_t reqId) {
  // Umbrella serializer doesn't work with McMsgRef and it will need to
  // increment refcount.
  auto msg = const_cast<mc_msg_t*>(req.get());
  ssize_t r = um_write_iovs(&umBackingMsg_, reqId, msg, iovs_, kMaxIovs);
  if (r == -1) {
    iovsCount_ = 0;
  } else {
    iovsCount_ = r;
  }
}

}} // facebook::memcache
