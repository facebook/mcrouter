/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
namespace facebook {
namespace memcache {

template <class Request>
McSerializedRequest::McSerializedRequest(
    const Request& req,
    size_t reqId,
    mc_protocol_t protocol,
    const CodecIdRange& compressionCodecs)
    : protocol_(protocol), typeId_(Request::typeId) {
  switch (protocol_) {
    case mc_ascii_protocol:
      new (&asciiRequest_) AsciiSerializedRequest;
      if (req.key().size() > MC_KEY_MAX_LEN_ASCII) {
        result_ = Result::BAD_KEY;
        return;
      }
      if (!asciiRequest_.prepare(req, iovsBegin_, iovsCount_)) {
        result_ = Result::ERROR;
      }
      break;
    case mc_caret_protocol:
      new (&caretRequest_) CaretSerializedMessage;
      if (req.key().size() > MC_KEY_MAX_LEN_UMBRELLA) {
        return;
      }
      if (!caretRequest_.prepare(
              req, reqId, compressionCodecs, iovsBegin_, iovsCount_)) {
        result_ = Result::ERROR;
      }
      break;
    case mc_umbrella_protocol:
      new (&umbrellaMessage_) UmbrellaSerializedMessage;
      if (req.key().size() > MC_KEY_MAX_LEN_UMBRELLA) {
        return;
      }
      if (!umbrellaMessage_.prepare(req, reqId, iovsBegin_, iovsCount_)) {
        result_ = Result::ERROR;
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
}
} // facebook::memcache
