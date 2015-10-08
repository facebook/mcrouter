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

#include <chrono>
#include <string>

#include <folly/experimental/fibers/Baton.h>
#include <folly/io/IOBufQueue.h>
#include <folly/io/async/AsyncTransport.h>
#include <folly/io/async/DelayedDestruction.h>

#include "mcrouter/lib/fbi/cpp/ObjectPool.h"
#include "mcrouter/lib/network/ConnectionOptions.h"
#include "mcrouter/lib/network/McClientRequestContext.h"
#include "mcrouter/lib/network/ClientMcParser.h"

namespace facebook { namespace memcache {

class Fifo;
template <int op>
class McOperation;
class McReply;
class McRequest;

namespace detail {
class OnEventBaseDestructionCallback;
}

/**
 * A base class for network communication with memcache protocol.
 *
 * This is an impl class, user should use AsyncMcClient.
 */
class AsyncMcClientImpl :
      public folly::DelayedDestruction,
      private folly::AsyncSocket::ConnectCallback,
      private folly::AsyncTransportWrapper::ReadCallback,
      private folly::AsyncTransportWrapper::WriteCallback {

 public:

  static std::shared_ptr<AsyncMcClientImpl> create(folly::EventBase& eventBase,
                                                   ConnectionOptions options);

  AsyncMcClientImpl(const AsyncMcClientImpl&) = delete;
  AsyncMcClientImpl& operator=(const AsyncMcClientImpl&) = delete;

  // Fail all requests and close connection.
  void closeNow();

  void setStatusCallbacks(
    std::function<void()> onUp,
    std::function<void(bool)> onDown);

  template <class Operation, class Request>
  typename ReplyType<Operation, Request>::type
  sendSync(const Request& request, Operation,
           std::chrono::milliseconds timeout);

  void setThrottle(size_t maxInflight, size_t maxPending);

  size_t getPendingRequestCount() const;
  size_t getInflightRequestCount() const;
  std::pair<uint64_t, uint64_t> getBatchingStat() const;

  void updateWriteTimeout(std::chrono::milliseconds timeout);
 private:
  using ParserT = ClientMcParser<AsyncMcClientImpl>;
  friend ParserT;

  enum class ConnectionState {
    UP, // Connection is open and we can write into it.
    DOWN, // Connection is not open (or close), we need to reconnect.
    CONNECTING, // Currently connecting.
    ERROR // Currently processing error.
  };

  struct ConnectionStatusCallbacks {
    std::function<void()> onUp;
    std::function<void(bool)> onDown;
  };

  // We need to be able to get shared_ptr to ourself and shared_from_this()
  // doesn't work correctly with DelayedDestruction.
  std::weak_ptr<AsyncMcClientImpl> selfPtr_;

  // Stats.
  std::pair<uint64_t, uint16_t> batchStatPrevious{0, 0};
  std::pair<uint64_t, uint16_t> batchStatCurrent{0, 0};

  folly::EventBase& eventBase_;
  std::unique_ptr<ParserT> parser_;

  // Pointer to current buffer. Updated by getReadBuffer()
  std::pair<void*, size_t> curBuffer_{nullptr, 0};

  // Socket related variables.
  ConnectionState connectionState_{ConnectionState::DOWN};
  ConnectionOptions connectionOptions_;
  folly::AsyncTransportWrapper::UniquePtr socket_;
  ConnectionStatusCallbacks statusCallbacks_;

  // Debug pipe.
  Fifo* debugFifo_{nullptr};

  bool outOfOrder_{false};
  McClientRequestContextQueue queue_;

  // Id for the next message that will be used by the next sendMsg() call.
  uint64_t nextMsgId_{1};

  // Throttle options (disabled by default).
  size_t maxPending_{0};
  size_t maxInflight_{0};

  // Writer loop related variables.
  class WriterLoop;
  bool writeScheduled_{false};
  std::unique_ptr<WriterLoop> writer_;

  bool isAborting_{false};
  std::unique_ptr<detail::OnEventBaseDestructionCallback>
    eventBaseDestructionCallback_;

  AsyncMcClientImpl(folly::EventBase& eventBase,
                    ConnectionOptions options,
                    Fifo* debugFifo);

  ~AsyncMcClientImpl();

  // Common part for send/sendSync.
  void sendCommon(McClientRequestContextBase& req);

  // Write some requests from sendQueue_ to the socket, until max inflight limit
  // is reached or queue is empty.
  void pushMessages();
  // Schedule next writer loop if it's not scheduled.
  void scheduleNextWriterLoop();
  void cancelWriterCallback();

  void attemptConnection();

  // Log error with additional diagnostic information.
  void logErrorWithContext(folly::StringPiece reason);
  folly::StringPiece clientStateToStr() const;

  // TAsyncSocket::ConnectCallback overrides
  void connectSuccess() noexcept override;
  void connectErr(const folly::AsyncSocketException& ex) noexcept override;

  // We've have encountered some error or we're shutting down the client.
  // It goes to DOWN state.
  void processShutdown();

  // AsyncTransportWrapper::ReadCallback overrides
  void getReadBuffer(void** bufReturn, size_t* lenReturn) override;
  void readDataAvailable(size_t len) noexcept override;
  void readEOF() noexcept override;
  void readErr(const folly::AsyncSocketException& ex) noexcept override;

  // AsyncTransportWrapper::WriteCallback overrides
  void writeSuccess() noexcept override;
  void writeErr(size_t bytesWritten,
                const folly::AsyncSocketException& ex) noexcept override;

  // Callbacks for McParser.
  template <class Reply>
  void replyReady(Reply&& reply, uint64_t reqId);
  void parseError(mc_res_t result, folly::StringPiece reason);
  bool nextReplyAvailable(uint64_t reqId);

  void sendFakeReply(McClientRequestContextBase& request);

  static void incMsgId(size_t& msgId);
};

}} // facebook::memcache

#include "AsyncMcClientImpl-inl.h"
