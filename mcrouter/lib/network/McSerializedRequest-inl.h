/*
 *  Copyright (c) 2015-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
namespace facebook {
namespace memcache {

namespace detail {

template <class Request>
typename std::enable_if<Request::hasKey, uint64_t>::type getKeySize(
    const Request& req) {
  return req.key().size();
}

template <class Request>
typename std::enable_if<!Request::hasKey, uint64_t>::type getKeySize(
    const Request&) {
  return 0;
}

template <class Request>
typename std::enable_if<
    ListContains<McRequestList, Request>::value,
    McSerializedRequest::Result>::type
prepareUmbrella(
    const Request& req,
    UmbrellaSerializedMessage& serialized,
    size_t reqId,
    const struct iovec*& iovOut,
    size_t& niovOut) {
  return serialized.prepare(req, reqId, iovOut, niovOut)
      ? McSerializedRequest::Result::OK
      : McSerializedRequest::Result::ERROR;
}

template <class Request>
typename std::enable_if<
    !ListContains<McRequestList, Request>::value,
    McSerializedRequest::Result>::type
prepareUmbrella(
    const Request&,
    UmbrellaSerializedMessage&,
    size_t,
    const struct iovec*&,
    size_t&) {
  // Error out umbrella serialization of non-umbrella requests.
  return McSerializedRequest::Result::ERROR;
}

} // detail

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
      if (detail::getKeySize(req) > MC_KEY_MAX_LEN_ASCII) {
        result_ = Result::BAD_KEY;
        return;
      }
      if (!asciiRequest_.prepare(req, iovsBegin_, iovsCount_)) {
        result_ = Result::ERROR;
      }
      break;
    case mc_caret_protocol:
      new (&caretRequest_) CaretSerializedMessage;
      if (detail::getKeySize(req) > MC_KEY_MAX_LEN_UMBRELLA) {
        return;
      }
      if (!caretRequest_.prepare(
              req, reqId, compressionCodecs, iovsBegin_, iovsCount_)) {
        result_ = Result::ERROR;
      }
      break;
    case mc_umbrella_protocol_DONOTUSE:
      new (&umbrellaMessage_) UmbrellaSerializedMessage;
      if (detail::getKeySize(req) > MC_KEY_MAX_LEN_UMBRELLA) {
        return;
      }

      result_ = detail::prepareUmbrella(
          req, umbrellaMessage_, reqId, iovsBegin_, iovsCount_);
      break;
    case mc_unknown_protocol:
    case mc_binary_protocol:
    case mc_nprotocols:
      checkLogic(false, "Used unsupported protocol! Value: {}", (int)protocol_);
      result_ = Result::ERROR;
      iovsCount_ = 0;
  }
}

} // memcache
} // facebook
