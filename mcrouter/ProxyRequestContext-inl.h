/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "mcrouter/lib/McRequest.h"
#include "mcrouter/lib/network/gen-cpp2/mc_caret_protocol_types.h"
#include "mcrouter/lib/network/TypedThriftMessage.h"
#include "mcrouter/proxy.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

namespace detail {

/**
 * Implementation class for storing the callback along with the context.
 */
template <class Request, class F>
class ProxyRequestContextTypedWithCallback
    : public ProxyRequestContextTyped<Request> {
 public:
  ProxyRequestContextTypedWithCallback(proxy_t& pr,
                                       const Request& req,
                                       F&& f,
                                       ProxyRequestPriority priority__)
      : ProxyRequestContextTyped<Request>(pr, req, priority__),
        f_(std::forward<F>(f)) {}

 protected:
  void sendReplyImpl(ReplyT<Request>&& reply) override final {
    auto req = this->req_;
    fiber_local::runWithoutLocals(
        [this, req, &reply]() { f_(*req, std::move(reply)); });
  }

 private:
  F f_;
};

/**
 * Temporary class to support old McrouterClient interface.
 */
template <class Operation, class F>
class LegacyProxyRequestContext
    : public ProxyRequestContextTyped<McRequestWithOp<Operation>> {
 public:
  LegacyProxyRequestContext(proxy_t& pr,
                            McMsgRef req,
                            F&& f,
                            ProxyRequestPriority priority__)
      : ProxyRequestContextTyped<McRequestWithOp<Operation>>(
            pr, request_, priority__),
        f_(std::forward<F>(f)),
        request_(std::move(req)) {}

 protected:
  void sendReplyImpl(ReplyT<McRequestWithOp<Operation>>&& reply)
      override final {

    fiber_local::runWithoutLocals(
        [this, &reply]() { f_(*this, std::move(reply)); });
  }

 private:
  F f_;
  McRequestWithOp<Operation> request_;
};

template <class F>
std::unique_ptr<ProxyRequestContext> legacyCreator(
    proxy_t& pr,
    McMsgRef req,
    mc_op_t op,
    McOpList::Item<0>,
    F&& f,
    ProxyRequestPriority priority) {
  throw std::runtime_error(std::string("send for requested op (") +
                           mc_op_to_string(op) + ") not supported");
}

template <int op_id, class F>
std::unique_ptr<ProxyRequestContext> legacyCreator(
    proxy_t& pr,
    McMsgRef req,
    mc_op_t op,
    McOpList::Item<op_id>,
    F&& f,
    ProxyRequestPriority priority) {
  if (McOpList::Item<op_id>::op::mc_op == op) {
    using Type =
        LegacyProxyRequestContext<typename McOpList::Item<op_id>::op, F>;
    return folly::make_unique<Type>(pr, std::move(req), std::forward<F>(f),
                                    priority);
  }
  return legacyCreator(pr, std::move(req), op, McOpList::Item<op_id - 1>(),
                       std::forward<F>(f), priority);
}

constexpr const char* kCommandNotSupportedStr = "Command not supported";

template <class Request>
bool precheckKey(ProxyRequestContextTyped<Request>& preq,
                 const Request& req) {
  auto k = req.fullKey();
  const nstring_t key{const_cast<char*>(k.begin()), k.size()};
  auto err = mc_client_req_key_check(key);
  if (err != mc_req_err_valid) {
    preq.sendReply(mc_res_local_error, mc_req_err_to_string(err));
    return false;
  }
  return true;
}

// Following methods validate the request and return true if it's correct,
// otherwise they reply it with error and return false;

template <class Request>
bool precheckRequest(ProxyRequestContextTyped<Request>& preq,
                     const Request& req) {
  return precheckKey(preq, req);
}

inline bool precheckRequest(
    ProxyRequestContextTyped<McRequestWithMcOp<mc_op_stats>>&,
    const McRequestWithMcOp<mc_op_stats>&) {
  return true;
}

inline bool precheckRequest(
    ProxyRequestContextTyped<TypedThriftRequest<cpp2::McStatsRequest>>&,
    const TypedThriftRequest<cpp2::McStatsRequest>&) {
  return true;
}

inline bool precheckRequest(
    ProxyRequestContextTyped<McRequestWithMcOp<mc_op_version>>&,
    const McRequestWithMcOp<mc_op_version>&) {
  return true;
}

inline bool precheckRequest(
    ProxyRequestContextTyped<TypedThriftRequest<cpp2::McVersionRequest>>&,
    const TypedThriftRequest<cpp2::McVersionRequest>&) {
  return true;
}

inline bool precheckRequest(
    ProxyRequestContextTyped<McRequestWithMcOp<mc_op_shutdown>>& preq,
    const McRequestWithMcOp<mc_op_shutdown>&) {
  // Return error (pretend to not even understand the protocol)
  preq.sendReply(mc_res_bad_command);
  return false;
}

inline bool precheckRequest(
    ProxyRequestContextTyped<TypedThriftRequest<cpp2::McShutdownRequest>>& preq,
    const TypedThriftRequest<cpp2::McShutdownRequest>&) {
  // Return error (pretend to not even understand the protocol)
  preq.sendReply(mc_res_bad_command);
  return false;
}

inline bool precheckRequest(
    ProxyRequestContextTyped<McRequestWithMcOp<mc_op_flushre>>& preq,
    const McRequestWithMcOp<mc_op_flushre>&) {
  // Return 'Not supported' message
  preq.sendReply(mc_res_local_error, kCommandNotSupportedStr);
  return false;
}

inline bool precheckRequest(
    ProxyRequestContextTyped<TypedThriftRequest<cpp2::McFlushReRequest>>& preq,
    const TypedThriftRequest<cpp2::McFlushReRequest>&) {
  // Return 'Not supported' message
  preq.sendReply(mc_res_local_error, kCommandNotSupportedStr);
  return false;
}

inline bool precheckRequest(
    ProxyRequestContextTyped<McRequestWithMcOp<mc_op_flushall>>& preq,
    const McRequestWithMcOp<mc_op_flushall>&) {

  if (!preq.proxy().getRouterOptions().enable_flush_cmd) {
    preq.sendReply(mc_res_local_error, "Command disabled");
    return false;
  }
  return true;
}

inline bool precheckRequest(
    ProxyRequestContextTyped<TypedThriftRequest<cpp2::McFlushAllRequest>>& preq,
    const TypedThriftRequest<cpp2::McFlushAllRequest>&) {

  if (!preq.proxy().getRouterOptions().enable_flush_cmd) {
    preq.sendReply(mc_res_local_error, "Command disabled");
    return false;
  }
  return true;
}

} // detail

template <class Request>
void ProxyRequestContextTyped<Request>::sendReply(ReplyT<Request>&& reply) {
  if (this->recording()) {
    return;
  }

  if (this->replied_) {
    return;
  }
  this->replied_ = true;
  auto result = reply.result();

  sendReplyImpl(std::move(reply));
  req_ = nullptr;

  stat_incr(this->proxy().stats, request_replied_stat, 1);
  stat_incr(this->proxy().stats, request_replied_count_stat, 1);
  if (mc_res_is_err(result)) {
    stat_incr(this->proxy().stats, request_error_stat, 1);
    stat_incr(this->proxy().stats, request_error_count_stat, 1);
  } else {
    stat_incr(this->proxy().stats, request_success_stat, 1);
    stat_incr(this->proxy().stats, request_success_count_stat, 1);
  }
}

template <class Request>
void ProxyRequestContextTyped<Request>::startProcessing() {
  std::unique_ptr<ProxyRequestContextTyped<Request>> self(this);

  if (!detail::precheckRequest(*this, *req_)) {
    return;
  }

  if (proxy().being_destroyed) {
    /* We can't process this, since 1) we destroyed the config already,
       and 2) the clients are winding down, so we wouldn't get any
       meaningful response back anyway. */
    LOG(ERROR) << "Outstanding request on a proxy that's being destroyed";
    sendReply(ReplyT<Request>(mc_res_unknown));
    return;
  }

  proxy().dispatchRequest(*req_, std::move(self));
}

template <class Request>
std::shared_ptr<ProxyRequestContextTyped<Request>>
ProxyRequestContextTyped<Request>::process(
    std::unique_ptr<Type> preq, std::shared_ptr<const ProxyConfig> config) {

  preq->config_ = std::move(config);
  return std::shared_ptr<Type>(
      preq.release(),
      /* Note: we want to delete on main context here since the destructor
         can do complicated things, like finalize stats entry and
         destroy a stale config.  There might not be enough stack space
         for these operations. */
      [](ProxyRequestContext* ctx) {
        folly::fibers::runInMainContext([ctx] { delete ctx; });
      });
}

template <class Request, class F>
std::unique_ptr<ProxyRequestContextTyped<Request>>
createProxyRequestContext(proxy_t& pr,
                          const Request& req,
                          F&& f,
                          ProxyRequestPriority priority) {
  using Type = detail::ProxyRequestContextTypedWithCallback<Request, F>;
  return folly::make_unique<Type>(pr, req, std::forward<F>(f), priority);
}

template <class F>
std::unique_ptr<ProxyRequestContext> createLegacyProxyRequestContext(
    proxy_t& pr, McMsgRef req, F&& f, ProxyRequestPriority priority) {
  auto op = req->op;
  return detail::legacyCreator(pr, std::move(req), op, McOpList::LastItem(),
                               std::forward<F>(f), priority);
}
} // mcrouter
} // memcache
} // facebook
