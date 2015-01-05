/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <mutex>
#include <set>

#include <glog/logging.h>

#include "mcrouter/lib/fbi/cpp/sfrlock.h"

namespace facebook { namespace memcache { namespace mcrouter {

/* CallbackPool::CallbackHandle */
template<typename... Args>
struct CallbackPool<Args...>::CallbackHandleImpl {
 public:
  CallbackHandleImpl(const CallbackHandleImpl&) = delete;
  CallbackHandleImpl& operator=(const CallbackHandleImpl&) = delete;
  ~CallbackHandleImpl() {
    std::lock_guard<SFRWriteLock> lck(data_->callbackLock.writeLock());
    data_->callbacks.erase(this);
  }
 private:
  friend class CallbackPool;

  std::shared_ptr<Data> data_;
  const OnUpdateFunc func_;

  CallbackHandleImpl(std::shared_ptr<Data> data, OnUpdateFunc func)
      : data_(std::move(data)),
        func_(std::move(func)) {
    std::lock_guard<SFRWriteLock> lck(data_->callbackLock.writeLock());
    data_->callbacks.insert(this);
  }
};

/* CallbackPool::Data */
template<typename... Args>
struct CallbackPool<Args...>::Data {
  std::set<CallbackHandleImpl*> callbacks;
  SFRLock callbackLock;
};

/* CallbackPool */

template<typename... Args>
CallbackPool<Args...>::CallbackPool()
  : data_(std::make_shared<Data>()) {
}

template<typename... Args>
void CallbackPool<Args...>::notify(Args... args) {
  std::lock_guard<SFRReadLock> lck(data_->callbackLock.readLock());
  for (auto& it : data_->callbacks) {
    try {
      it->func_(args...);
    } catch (const std::exception& e) {
      LOG(ERROR) << "Error occured in callback: " << e.what();
    } catch (...) {
      LOG(ERROR) << "Unknown error occured in callback";
    }
  }
}

template<typename... Args>
typename CallbackPool<Args...>::CallbackHandle
CallbackPool<Args...>::subscribe(OnUpdateFunc callback) {
  return std::unique_ptr<CallbackHandleImpl>(
    new CallbackHandleImpl(data_, std::move(callback)));
}

}}} // facebook::memcache::mcrouter
