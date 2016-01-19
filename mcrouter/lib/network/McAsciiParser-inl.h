/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "mcrouter/lib/fbi/cpp/util.h"

namespace facebook {
namespace memcache {
namespace detail {

template <class OpReq>
class CallbackBase<List<OpReq>> {
 public:
  virtual ~CallbackBase() = default;
  virtual void multiOpEnd() noexcept = 0;
  virtual void onRequest(typename OpReq::First op,
                         typename OpReq::Second&& req,
                         bool noreply = false) noexcept = 0;
};

template <class OpReq, class... OpReqs>
class CallbackBase<List<OpReq, OpReqs...>>
    : public CallbackBase<List<OpReqs...>> {
 public:
  using CallbackBase<List<OpReqs...>>::onRequest;

  virtual void onRequest(typename OpReq::First op,
                         typename OpReq::Second&& req,
                         bool noreply = false) noexcept = 0;
};

template <class Callback, class OpReqs>
class CallbackWrapper;

template <class Callback, class OpReq>
class CallbackWrapper<Callback, List<OpReq>>
    : public CallbackBase<SupportedReqs> {
 public:
  explicit CallbackWrapper(Callback& callback) : callback_(callback) {}

  virtual void multiOpEnd() noexcept override { callback_.multiOpEnd(); }

  using CallbackBase<SupportedReqs>::onRequest;

  virtual void onRequest(typename OpReq::First op,
                         typename OpReq::Second&& req,
                         bool noreply = false) noexcept override {
    callback_.onRequest(op, std::move(req), noreply);
  }

 protected:
  Callback& callback_;
};

template <class Callback, class OpReq, class... OpReqs>
class CallbackWrapper<Callback, List<OpReq, OpReqs...>>
    : public CallbackWrapper<Callback, List<OpReqs...>> {
 public:
  explicit CallbackWrapper(Callback& callback)
      : CallbackWrapper<Callback, List<OpReqs...>>(callback) {}

  using CallbackWrapper<Callback, List<OpReqs...>>::onRequest;

  void onRequest(typename OpReq::First op,
                 typename OpReq::Second&& req,
                 bool noreply = false) noexcept override {
    this->callback_.onRequest(op, std::move(req), noreply);
  }
};

} // detail

template <class T>
T McClientAsciiParser::getReply() {
  assert(state_ == State::COMPLETE);
  state_ = State::UNINIT;
  return std::move(currentMessage_.get<T>());
}

// Forward-declare initializers.
template <>
void McClientAsciiParser::initializeReplyParser<McOperation<mc_op_get>,
                                                McRequest>();

template <>
void McClientAsciiParser::initializeReplyParser<McOperation<mc_op_gets>,
                                                McRequest>();

template <>
void McClientAsciiParser::initializeReplyParser<McOperation<mc_op_lease_get>,
                                                McRequest>();

template <>
void McClientAsciiParser::initializeReplyParser<McOperation<mc_op_set>,
                                                McRequest>();

template <>
void McClientAsciiParser::initializeReplyParser<McOperation<mc_op_add>,
                                                McRequest>();

template <>
void McClientAsciiParser::initializeReplyParser<McOperation<mc_op_replace>,
                                                McRequest>();

template <>
void McClientAsciiParser::initializeReplyParser<McOperation<mc_op_lease_set>,
                                                McRequest>();

template <>
void McClientAsciiParser::initializeReplyParser<McOperation<mc_op_cas>,
                                                McRequest>();

template <>
void McClientAsciiParser::initializeReplyParser<McOperation<mc_op_incr>,
                                                McRequest>();

template <>
void McClientAsciiParser::initializeReplyParser<McOperation<mc_op_decr>,
                                                McRequest>();

template <>
void McClientAsciiParser::initializeReplyParser<McOperation<mc_op_version>,
                                                McRequest>();

template <>
void McClientAsciiParser::initializeReplyParser<McOperation<mc_op_delete>,
                                                McRequest>();

template <>
void McClientAsciiParser::initializeReplyParser<McOperation<mc_op_touch>,
                                                McRequest>();

template <>
void McClientAsciiParser::initializeReplyParser<McOperation<mc_op_metaget>,
                                                McRequest>();

template <>
void McClientAsciiParser::initializeReplyParser<McOperation<mc_op_flushall>,
                                                McRequest>();

template<>
void McClientAsciiParser::initializeReplyParser<McOperation<mc_op_append>,
                                                McRequest>();

template<>
void McClientAsciiParser::initializeReplyParser<McOperation<mc_op_prepend>,
                                                McRequest>();

template <class Operation, class Request>
void McClientAsciiParser::initializeReplyParser() {
  throwLogic(
      "Unexpected call to McAsciiParser::initializeReplyParser "
      "with template arguments [Operation = {}, Request = {}]",
      typeid(Operation).name(),
      typeid(Request).name());
}

template <class Callback>
McServerAsciiParser::McServerAsciiParser(Callback& callback)
    : callback_(folly::make_unique<
          detail::CallbackWrapper<Callback, detail::SupportedReqs>>(callback)) {
}
} // memcache
} // facebook
