/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <folly/FileUtil.h>
#include <folly/io/async/EventBase.h>
#include <folly/io/async/EventFDWrapper.h>
#include <folly/io/async/EventHandler.h>
#include <folly/MPMCQueue.h>
#include <folly/Random.h>

namespace facebook { namespace memcache {

/**
 * Relaxed notification - slight increase of average (not p99) latency
 * for improved CPU time (fewer cross-thread notifications)
 */
class Notifier {
 public:
  using NowUsecFunc = int64_t (*)();

  /**
   * @param noNotifyRate  Request rate at which we stop all per-request
   *   notifications.  At any rate from 0 to noNotifyRate, we linearly
   *   reduce the fraction of requests that get notified (starting from
   *   100% of requests initially).
   *   If 0, this logic is disabled - we notify on every request.
   *
   * @param waitThreshold  Force notification after this number of us
   *   passed since the queue was last drained.
   *   If 0, this logic is disabled.
   *
   * @param nowFunc  Function that returns current time in us.
   */
  Notifier(size_t noNotifyRate,
           int64_t waitThresholdUs,
           NowUsecFunc nowFunc) noexcept;

  void bumpMessages() noexcept {
    ++curMessages_;
  }

  size_t currentNotifyPeriod() const noexcept {
    return period_;
  }

  bool shouldNotify() noexcept {
    return (state_.exchange(State::NOTIFIED) == State::EMPTY);
  }

  bool shouldNotifyRelaxed() noexcept;

  template <class F>
  void drainWhileNonEmpty(F&& drainFunc) {
    auto expected = State::READING;
    do {
      state_ = State::READING;
      drainFunc();
    } while (!state_.compare_exchange_strong(expected, State::EMPTY));

    waitStart_ = nowFunc_();
  }

  void maybeUpdatePeriod() noexcept;

  size_t noNotifyRate() const {
    return noNotifyRate_;
  }

 private:
  const size_t noNotifyRate_;
  const int64_t waitThreshold_;
  const NowUsecFunc nowFunc_;
  int64_t lastTimeUsec_;
  size_t curMessages_{0};

  static constexpr int64_t kUpdatePeriodUsec = 1000000;

  std::atomic<size_t> FOLLY_ALIGN_TO_AVOID_FALSE_SHARING period_{0};
  std::atomic<size_t> FOLLY_ALIGN_TO_AVOID_FALSE_SHARING counter_{0};
  std::atomic<int64_t> FOLLY_ALIGN_TO_AVOID_FALSE_SHARING waitStart_;

  enum class State {
    EMPTY,
    NOTIFIED,
    READING,
  };

  std::atomic<State> state_ FOLLY_ALIGN_TO_AVOID_FALSE_SHARING;
};

template <class T>
class MessageQueue {
 public:
  /**
   * Must be called from the event base thread.
   *
   * @param capactiy  All queue storage is allocated upfront.
   *   If queue is full, further writes will block.
   * @param onMessage Called on every message from the event base thread.
   * @param noNotifyRate  Request rate at which we stop all per-request
   *   notifications.  At any rate from 0 to noNotifyRate, we linearly
   *   reduce the fraction of requests that get notified (starting from
   *   100% of requests initially).
   *   If 0, this logic is disabled - we notify on every request.
   * @param waitThreshold  Force notification after this number of us
   *   passed since the queue was last drained.
   *   If 0, this logic is disabled.
   * @param nowFunc  Function that returns current time in us.
   * @param notifyCallback  Called every time after a notification
   *   event is posted.
   */
  MessageQueue(size_t capacity,
               std::function<void(T&&)> onMessage,
               size_t noNotifyRate,
               int64_t waitThreshold,
               Notifier::NowUsecFunc nowFunc,
               std::function<void()> notifyCallback)
      : queue_(capacity),
        onMessage_(std::move(onMessage)),
        notifier_(noNotifyRate, waitThreshold, nowFunc),
        handler_(*this),
        timeoutHandler_(*this),
        notifyCallback_(std::move(notifyCallback)) {

    efd_ = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK | EFD_SEMAPHORE);
    PCHECK(efd_ >= 0);
  }

  /**
   * Must be called from the event base thread.
   */
  void attachEventBase(folly::EventBase& evb) {
    handler_.initHandler(&evb, efd_);
    handler_.registerHandler(
      folly::EventHandler::READ | folly::EventHandler::PERSIST);

    if (notifier_.noNotifyRate() > 0) {
      timeoutHandler_.attachEventBase(&evb);
      timeoutHandler_.scheduleTimeout(kWakeupEveryMs);
    }
  }

  size_t currentNotifyPeriod() const noexcept {
    return notifier_.currentNotifyPeriod();
  }

  /**
   * Must be called from the event base thread.
   * Manually drains the queue, calling the callback on any remaining messages.
   * Note: the user must guarantee that the queue is empty on destruction.
   */
  void drain() {
    notifier_.drainWhileNonEmpty(
      [this] () {
        drainImpl();
      });
  }

  ~MessageQueue() {
    handler_.unregisterHandler();
    if (efd_ >= 0) {
      PCHECK(folly::closeNoInt(efd_) == 0);
    }
  }

  /**
   * Put a new element into the queue. Can be called from any thread.
   * Allows inplace construction of the message.
   * Will block if queue is full until the reader catches up.
   *
   * @return true if the notify event was posted
   */
  template <class... Args>
  void blockingWrite(Args&&... args) noexcept {
    queue_.blockingWrite(std::forward<Args>(args)...);
    if (notifier_.shouldNotify()) {
      doNotify();
    }
  }

  template <class... Args>
  void blockingWriteRelaxed(Args&&... args) noexcept {
    queue_.blockingWrite(std::forward<Args>(args)...);
    if (notifier_.shouldNotifyRelaxed()) {
      doNotify();
    }
  }

 private:
  static constexpr int64_t kWakeupEveryMs = 2;
  folly::MPMCQueue<T> queue_;
  std::function<void(T&&)> onMessage_;
  Notifier notifier_;

  class EventHandler : public folly::EventHandler {
   public:
    explicit EventHandler(MessageQueue& q) : parent_(q) {}
    void handlerReady(uint16_t events) noexcept override {
      parent_.onEvent();
    }

   private:
    MessageQueue& parent_;
  };

  class TimeoutHandler : public folly::AsyncTimeout {
   public:
    explicit TimeoutHandler(MessageQueue& q) : parent_(q) {}
    void timeoutExpired() noexcept override {
      parent_.onTimeout();
    }

   private:
    MessageQueue& parent_;
  };

  EventHandler handler_;
  TimeoutHandler timeoutHandler_;
  std::function<void()> notifyCallback_;
  int efd_{-1};

  void onEvent() {
    uint64_t value;
    auto res = ::read(efd_, &value, sizeof(value));
    CHECK(res == sizeof(value));

    drain();
  }

  void onTimeout() {
    drain();
    notifier_.maybeUpdatePeriod();
    timeoutHandler_.scheduleTimeout(kWakeupEveryMs);
  }

  void doNotify() {
    assert(efd_ >= 0);
    uint64_t n = 1;
    PCHECK(::write(efd_, &n, sizeof(n)) == sizeof(n));
    if (notifyCallback_) {
      notifyCallback_();
    }
  }

  void drainImpl() {
    T message;
    while (queue_.read(message)) {
      onMessage_(std::move(message));
      notifier_.bumpMessages();
    }
  }
};

}}  // facebook::memcache
