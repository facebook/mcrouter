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

#include "mcrouter/lib/fbi/cpp/LogFailure.h"

namespace facebook { namespace memcache { namespace mcrouter {

template<class Data>
Observable<Data>::Observable(Data data)
  : data_(std::move(data)) {
}

template<class Data>
typename Observable<Data>::CallbackHandle
Observable<Data>::subscribe(OnUpdateOldNew callback) {
  return pool_.subscribe(std::move(callback));
}

template<class Data>
typename Observable<Data>::CallbackHandle
Observable<Data>::subscribeAndCall(OnUpdateOldNew callback) {
  std::lock_guard<SFRReadLock> lck(dataLock_.readLock());
  try {
    callback(Data(), data_);
  } catch (const std::exception& e) {
    LOG_FAILURE("mcrouter", failure::Category::kOther,
                "Error occured in observable callback: {}", e.what());
  } catch (...) {
    LOG_FAILURE("mcrouter", failure::Category::kOther,
                "Unknown error occured in observable callback");
  }
  return subscribe(std::move(callback));
}

template<class Data>
Data Observable<Data>::get() {
  std::lock_guard<SFRReadLock> lck(dataLock_.readLock());
  return data_;
}

template<class Data>
void Observable<Data>::set(Data data) {
  std::lock_guard<SFRWriteLock> lck(dataLock_.writeLock());
  auto old = std::move(data_);
  data_ = std::move(data);
  // no copy here, because old and data are passed by const reference
  pool_.notify(old, data_);
}

template<class Data>
template<typename... Args>
void Observable<Data>::emplace(Args&&... args) {
  set(Data(std::forward<Args>(args)...));
}

}}} // facebook::memcache::mcrouter
