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
void McClientAsciiParser::initializeReplyParser<
    McRequestWithMcOp<mc_op_get>>();

template <>
void McClientAsciiParser::initializeReplyParser<
    McRequestWithMcOp<mc_op_gets>>();

template <>
void McClientAsciiParser::initializeReplyParser<
    McRequestWithMcOp<mc_op_lease_get>>();

template <>
void McClientAsciiParser::initializeReplyParser<
    McRequestWithMcOp<mc_op_set>>();

template <>
void McClientAsciiParser::initializeReplyParser<
    McRequestWithMcOp<mc_op_add>>();

template <>
void McClientAsciiParser::initializeReplyParser<
    McRequestWithMcOp<mc_op_replace>>();

template <>
void McClientAsciiParser::initializeReplyParser<
    McRequestWithMcOp<mc_op_lease_set>>();

template <>
void McClientAsciiParser::initializeReplyParser<
    McRequestWithMcOp<mc_op_cas>>();

template <>
void McClientAsciiParser::initializeReplyParser<
    McRequestWithMcOp<mc_op_incr>>();

template <>
void McClientAsciiParser::initializeReplyParser<
    McRequestWithMcOp<mc_op_decr>>();

template <>
void McClientAsciiParser::initializeReplyParser<
    McRequestWithMcOp<mc_op_version>>();

template <>
void McClientAsciiParser::initializeReplyParser<
    McRequestWithMcOp<mc_op_delete>>();

template <>
void McClientAsciiParser::initializeReplyParser<
    McRequestWithMcOp<mc_op_touch>>();

template <>
void McClientAsciiParser::initializeReplyParser<
    McRequestWithMcOp<mc_op_metaget>>();

template <>
void McClientAsciiParser::initializeReplyParser<
    McRequestWithMcOp<mc_op_flushall>>();

template <>
void McClientAsciiParser::initializeReplyParser<
    McRequestWithMcOp<mc_op_append>>();

template <>
void McClientAsciiParser::initializeReplyParser<
    McRequestWithMcOp<mc_op_prepend>>();

template <class Request>
void McClientAsciiParser::initializeReplyParser() {
  throwLogic(
      "Unexpected call to McAsciiParser::initializeReplyParser "
      "with template arguments [Request = {}]",
      typeid(Request).name());
}

template <class Callback>
McServerAsciiParser::McServerAsciiParser(Callback& callback)
    : callback_(folly::make_unique<
          detail::CallbackWrapper<Callback, detail::SupportedReqs>>(callback)) {
}
} // memcache
} // facebook
