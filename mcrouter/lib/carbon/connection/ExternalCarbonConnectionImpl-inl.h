/*
 *  Copyright (c) 2017-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <functional>
#include <memory>
#include <utility>

namespace carbon {
namespace detail {

class Client {
 public:
  Client(
      facebook::memcache::ConnectionOptions connectionOptions,
      ExternalCarbonConnectionImpl::Options options);

  ~Client();

  size_t limitRequests(size_t requestsCount);

  template <class Request>
  facebook::memcache::ReplyT<Request> sendRequest(const Request& request) {
    SCOPE_EXIT {
      // New requests are scheduled as remote tasks, so we won't create
      // fibers for them, until this fiber is complete anyway.
      counting_sem_post(&outstandingReqsSem_, 1);
    };
    return client_.sendSync(request, connectionOptions_.writeTimeout);
  }

  void closeNow();

 private:
  const facebook::memcache::ConnectionOptions connectionOptions_;
  const ExternalCarbonConnectionImpl::Options options_;
  facebook::memcache::AsyncMcClient client_;
  counting_sem_t outstandingReqsSem_;
};

class ThreadInfo {
 public:
  ThreadInfo();

  std::weak_ptr<Client> createClient(
      facebook::memcache::ConnectionOptions connectionOptions,
      ExternalCarbonConnectionImpl::Options options);

  void releaseClient(std::weak_ptr<Client> clientWeak);

  template <typename F>
  void addTaskRemote(F&& f) {
    fiberManager_.addTaskRemote(std::forward<F>(f));
  }

  ~ThreadInfo();

 private:
  std::thread thread_;
  folly::fibers::FiberManager fiberManager_;
  std::unordered_set<std::shared_ptr<Client>> clients_;
};

} // detail

class ExternalCarbonConnectionImpl::Impl {
 public:
  Impl(
      facebook::memcache::ConnectionOptions connectionOptions,
      ExternalCarbonConnectionImpl::Options options);
  ~Impl();
  bool healthCheck();

  template <class Request, class F>
  void sendRequestOne(const Request& req, F&& f) {
    auto threadInfo = threadInfo_.lock();
    if (!threadInfo) {
      throw CarbonConnectionRecreateException(
          "Singleton<ThreadPool> was destroyed!");
    }

    auto client = client_.lock();
    assert(client);

    if (client->limitRequests(1) == 0) {
      f(req, facebook::memcache::ReplyT<Request>(mc_res_local_error));
      return;
    }

    threadInfo->addTaskRemote(
        [ clientWeak = client_, &req, f = std::forward<F>(f) ]() mutable {
          auto cl = clientWeak.lock();
          if (!cl) {
            folly::fibers::runInMainContext(
                [&req, f = std::forward<F>(f) ]() mutable {
                  f(req, facebook::memcache::ReplyT<Request>(mc_res_unknown));
                });
            return;
          }

          auto reply = cl->sendRequest(req);
          folly::fibers::runInMainContext(
              [&req, f = std::forward<F>(f), &reply ]() mutable {
                f(req, std::move(reply));
              });
        });
  }

  template <class Request, class F>
  void sendRequestMulti(
      std::vector<std::reference_wrapper<const Request>>&& reqs,
      F&& f) {
    auto threadInfo = threadInfo_.lock();
    if (!threadInfo) {
      throw CarbonConnectionRecreateException(
          "Singleton<ThreadPool> was destroyed!");
    }

    auto cl = client_.lock();
    assert(cl);

    auto ctx =
        std::make_shared<std::vector<std::reference_wrapper<const Request>>>(
            std::move(reqs));

    for (size_t i = 0; i < ctx->size();) {
      auto num = cl->limitRequests(ctx->size() - i);

      if (num == 0) {
        // Hit outstanding limit.
        for (; i < ctx->size(); ++i) {
          f((*ctx)[i].get(),
            facebook::memcache::ReplyT<Request>(mc_res_local_error));
        }
        break;
      }

      threadInfo->addTaskRemote(
          [ clientWeak = client_, ctx, i, num, f ]() mutable {
            auto client = clientWeak.lock();
            if (!client) {
              folly::fibers::runInMainContext([&ctx, i, num, &f]() mutable {
                for (size_t cnt = 0; cnt < num; ++cnt, ++i) {
                  f((*ctx)[i].get(),
                    facebook::memcache::ReplyT<Request>(mc_res_unknown));
                }
              });
              return;
            }

            for (size_t cnt = 0; cnt + 1 < num; ++cnt, ++i) {
              const Request& req = (*ctx)[i];
              folly::fibers::addTaskFinally(
                  [clientWeak, &req] {
                    if (auto c = clientWeak.lock()) {
                      return c->sendRequest(req);
                    }
                    return facebook::memcache::ReplyT<Request>(mc_res_unknown);
                  },
                  [f, &req](folly::Try<facebook::memcache::ReplyT<Request>>&&
                                r) mutable { f(req, std::move(r.value())); });
            }

            // Send last request in a batch on this fiber.
            const auto& req = (*ctx)[i].get();
            auto reply = client->sendRequest(req);
            folly::fibers::runInMainContext(
                [&req, &f, &reply]() mutable { f(req, std::move(reply)); });
          });

      i += num;
    }
  }

 private:
  std::weak_ptr<detail::ThreadInfo> threadInfo_;
  std::weak_ptr<detail::Client> client_;
};

template <class Request>
void ExternalCarbonConnectionImpl::sendRequestOne(
    const Request& req,
    RequestCb<Request> cb) {
  try {
    impl_->sendRequestOne(req, std::move(cb));
  } catch (const CarbonConnectionRecreateException&) {
    impl_ = std::make_unique<Impl>(connectionOptions_, options_);
    return impl_->sendRequestOne(req, std::move(cb));
  }
}

template <class Request>
void ExternalCarbonConnectionImpl::sendRequestMulti(
    std::vector<std::reference_wrapper<const Request>>&& reqs,
    RequestCb<Request> cb) {
  try {
    impl_->sendRequestMulti(std::move(reqs), std::move(cb));
  } catch (const CarbonConnectionRecreateException&) {
    impl_ = std::make_unique<Impl>(connectionOptions_, options_);
    return impl_->sendRequestMulti(std::move(reqs), std::move(cb));
  }
}

} // carbon
