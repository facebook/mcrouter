/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include "McParser.h"

namespace facebook { namespace memcache {

/* Adjust buffer size after this many requests */
const size_t kAdjustBufferSizeInterval = 10000;

/* Decay previous bytes per request value with this constant */
const double kBprDecay = 0.9;

McParser::McParser(ServerParseCallback* callback,
                   size_t requestsPerRead,
                   size_t minBufferSize,
                   size_t maxBufferSize)
    : type_(ParserType::SERVER),
      serverParseCallback_(callback),
      messagesPerRead_(requestsPerRead),
      minBufferSize_(minBufferSize),
      maxBufferSize_(maxBufferSize) {
  assert(serverParseCallback_ != nullptr);
  mc_parser_init(&mcParser_,
                 request_parser,
                 &parserMsgReady,
                 &parserParseError,
                 this);
}

McParser::McParser(ClientParseCallback* callback,
                   size_t repliesPerRead,
                   size_t minBufferSize,
                   size_t maxBufferSize)
    : type_(ParserType::CLIENT),
      clientParseCallback_(callback),
      messagesPerRead_(repliesPerRead),
      minBufferSize_(minBufferSize),
      maxBufferSize_(maxBufferSize) {
  assert(clientParseCallback_ != nullptr);
  mc_parser_init(&mcParser_,
                 reply_parser,
                 &parserMsgReady,
                 &parserParseError,
                 this);
}

McParser::~McParser() {
  mc_parser_reset(&mcParser_);
}

std::pair<void*, size_t> McParser::getReadBuffer() {
  if (protocol_ == mc_umbrella_protocol
      && umBodyBuffer_) {
    /* We're reading in umbrella message body */
    return std::make_pair(umBodyBuffer_->writableTail(),
                          umMsgInfo_.body_size - umBodyBuffer_->length());
  } else {
    auto p = readBuffer_.preallocate(bufferSize_, bufferSize_);
    /* preallocate might return more than asked,
       but it's always safe to use a smaller portion */
    p.second = std::min(p.second, bufferSize_);
    return p;
  }
}

void McParser::recalculateBufferSize(size_t read) {
  readBytes_ += read;
  if (LIKELY(parsedMessages_ < kAdjustBufferSizeInterval)) {
    return;
  }

  double bpr = (double)readBytes_ / parsedMessages_;
  if (UNLIKELY(bytesPerRequest_ == 0.0)) {
    bytesPerRequest_ = bpr;
  } else {
    bytesPerRequest_ = bytesPerRequest_ * kBprDecay + bpr * (1.0 - kBprDecay);
  }
  bufferSize_ = std::max(
    minBufferSize_,
    std::min((size_t)bytesPerRequest_ * messagesPerRead_, maxBufferSize_));
  parsedMessages_ = 0;
  readBytes_ = 0;
}

void McParser::errorHelper(McReply reply) {
  switch (type_) {
    case ParserType::SERVER:
      serverParseCallback_->parseError(std::move(reply));
      break;
    case ParserType::CLIENT:
      clientParseCallback_->parseError(std::move(reply));
      break;
  }
}

namespace {
/**
 * Given an IOBuf and a range of bytes [begin, begin + size) inside it,
 * returns a clone of the IOBuf so that cloned.data() == begin and
 * cloned.length() == size.
 */
folly::IOBuf cloneSubBuf(
  const std::unique_ptr<folly::IOBuf>& from,
  uint8_t* begin, size_t size) {

  folly::IOBuf out;
  from->cloneInto(out);
  assert(begin >= out.data() && begin <= out.data() + out.length());
  out.trimStart(begin - out.data());
  assert(size <= out.length());
  out.trimEnd(out.length() - size);
  return out;
}
}

void McParser::requestReadyHelper(McRequest req,
                                  mc_op_t operation,
                                  uint64_t reqid,
                                  mc_res_t result,
                                  bool noreply) {
  ++parsedMessages_;
  serverParseCallback_->requestReady(std::move(req), operation, reqid, result,
                                     noreply);
}

void McParser::replyReadyHelper(McReply reply,
                                mc_op_t operation,
                                uint64_t reqid) {
  ++parsedMessages_;
  clientParseCallback_->replyReady(std::move(reply), operation, reqid);
}

bool McParser::umMessageReady(
  const uint8_t* header,
  const uint8_t* body,
  const std::unique_ptr<folly::IOBuf>& bodyBuffer) {

  auto mutMsg = createMcMsgRef();
  uint64_t reqid;
  auto st = um_consume_no_copy(header, umMsgInfo_.header_size,
                               body, umMsgInfo_.body_size,
                               &reqid, mutMsg.get());
  if (st != um_ok) {
    errorHelper(McReply(mc_res_remote_error,
                        "Error parsing Umbrella message"));
    return false;
  }

  switch (type_) {
    case ParserType::SERVER:
      {
        McMsgRef msg(std::move(mutMsg));
        auto req = McRequest(msg.clone());
        if (msg->key.len != 0) {
          req.setKey(
            cloneSubBuf(bodyBuffer,
                        reinterpret_cast<uint8_t*>(msg->key.str),
                        msg->key.len));
        }
        if (msg->value.len != 0) {
          req.setValue(
            cloneSubBuf(bodyBuffer,
                        reinterpret_cast<uint8_t*>(msg->value.str),
                        msg->value.len));
        }
        requestReadyHelper(std::move(req), msg->op, reqid, msg->result,
                           msg->noreply);
      }
      break;
    case ParserType::CLIENT:
      {
        folly::IOBuf value;
        if (mutMsg->value.len != 0) {
          value = cloneSubBuf(bodyBuffer,
                              reinterpret_cast<uint8_t*>(mutMsg->value.str),
                              mutMsg->value.len);
          // Reset msg->value, or it will confuse McReply::releasedMsg
          mutMsg->value.str = nullptr;
          mutMsg->value.len = 0;
        }
        McMsgRef msg(std::move(mutMsg));
        auto reply = McReply(msg->result, msg.clone());
        if (value.length() != 0) {
          reply.setValue(std::move(value));
        }
        replyReadyHelper(std::move(reply), msg->op, reqid);
      }
      break;
  }
  return true;
}

bool McParser::readUmbrellaData(std::unique_ptr<folly::IOBuf> data) {
  if (umHeaderBuffer_ && !umHeaderBuffer_->empty()) {
    /* Headers are generally short (few dozen bytes), so it's really
       unlikely that we end up here */
    umHeaderBuffer_->appendChain(std::move(data));
    umHeaderBuffer_->coalesce();
  } else {
    umHeaderBuffer_ = std::move(data);
  }

  while (!umHeaderBuffer_->empty()) {
    auto st = um_parse_header(umHeaderBuffer_->data(),
                              umHeaderBuffer_->length(),
                              &umMsgInfo_);
    if (st == um_not_enough_data) {
      return true;
    }

    if (st != um_ok) {
      errorHelper(McReply(mc_res_remote_error,
                          "Error parsing Umbrella header"));
      return false;
    }

    /* Three cases: */
    if (umHeaderBuffer_->length() >= umMsgInfo_.message_size) {
      /* 1) we already have the entire message */
      if (!umMessageReady(
            umHeaderBuffer_->data(),
            umHeaderBuffer_->data() + umMsgInfo_.header_size,
            umHeaderBuffer_)) {
        umHeaderBuffer_.reset();
        return false;
      }
      /* Re-enter the loop */
      umHeaderBuffer_->trimStart(umMsgInfo_.message_size);
      continue;
    } else if (umHeaderBuffer_->length() >= umMsgInfo_.header_size) {
      /* 2) we have the entire header, but body is incomplete.
         Copy the partially read body into the new buffer.
         TODO: this copy could be eliminated, but needs
         some modification of umbrella library. */
      auto partial = umHeaderBuffer_->length() - umMsgInfo_.header_size;
      umBodyBuffer_ = folly::IOBuf::copyBuffer(
        umHeaderBuffer_->data() + umMsgInfo_.header_size,
        partial,
        /* headroom= */ 0,
        /* minTailroom= */ umMsgInfo_.body_size - partial);
      return true;
    }
    /* 3) else header is incomplete */
    return true;
  }
  return true;
}

bool McParser::readDataAvailable(size_t len) {
  SCOPE_EXIT {
    if (messagesPerRead_ > 0) {
      recalculateBufferSize(len);
    }
  };

  if (umBodyBuffer_) {
    umBodyBuffer_->append(len);
    if (umBodyBuffer_->length() == umMsgInfo_.body_size) {
      auto res = umMessageReady(umHeaderBuffer_->data(),
                                umBodyBuffer_->data(),
                                umBodyBuffer_);
      umHeaderBuffer_.reset();
      umBodyBuffer_.reset();
      return res;
    }
    return true;
  } else {
    readBuffer_.postallocate(len);
    auto data = readBuffer_.split(len);
    if (UNLIKELY(data->empty())) {
      return true;
    }

    if (UNLIKELY(!seenFirstByte_)) {
      seenFirstByte_ = true;
      protocol_ = mc_parser_determine_protocol(&mcParser_,
                                               *data->data());
      if (protocol_ == mc_umbrella_protocol) {
        outOfOrder_ = true;
      } else if (protocol_ == mc_ascii_protocol) {
        outOfOrder_ = false;
      } else {
        return false;
      }
    }

    if (protocol_ == mc_umbrella_protocol) {
      return readUmbrellaData(std::move(data));
    } else {
      /* mc_parser only works with contiguous blocks */
      auto bytes = data->coalesce();
      mc_parser_parse(&mcParser_, bytes.begin(), bytes.size());
      return true;
    }
  }
}

void McParser::msgReady(McMsgRef msg, uint64_t reqid) {
  auto operation = msg->op;
  auto result = msg->result;
  auto noreply = msg->noreply;

  switch (type_) {
    case ParserType::SERVER:
      requestReadyHelper(McRequest(std::move(msg)), operation, reqid, result,
                         noreply);
      break;
    case ParserType::CLIENT:
      replyReadyHelper(McReply(result, std::move(msg)), operation, reqid);
      break;
  }
}

void McParser::parserMsgReady(void* context,
                              uint64_t reqid,
                              mc_msg_t* msg) {
  auto parser = reinterpret_cast<McParser*>(context);
  parser->msgReady(McMsgRef::moveRef(msg), reqid);
}

void McParser::parseError(parser_error_t error) {
  std::string err;

  switch (error) {
    case parser_unspecified_error:
    case parser_malformed_request:
      err = "malformed request";
      break;
    case parser_out_of_memory:
      err = "out of memory";
      break;
  }

  errorHelper(McReply(mc_res_client_error, std::move(err)));
}

void McParser::parserParseError(void* context,
                                parser_error_t error) {
  auto parser = reinterpret_cast<McParser*>(context);
  parser->parseError(error);
}

}}  // facebook::memcache
