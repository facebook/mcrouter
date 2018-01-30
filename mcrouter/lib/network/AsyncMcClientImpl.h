/*
 *  Copyright (c) 2014-present, Facebook, Inc.
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

#include <folly/fibers/Baton.h>
#include <folly/io/IOBufQueue.h>
#include <folly/io/async/AsyncSocket.h>
#include <folly/io/async/AsyncTransport.h>
#include <folly/io/async/DelayedDestruction.h>
#include <folly/io/async/VirtualEventBase.h>

#include "mcrouter/lib/CompressionCodecManager.h"
#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/debug/ConnectionFifo.h"
#include "mcrouter/lib/fbi/cpp/ObjectPool.h"
#include "mcrouter/lib/network/ClientMcParser.h"
#include "mcrouter/lib/network/ConnectionOptions.h"
#include "mcrouter/lib/network/McClientRequestContext.h"
#include "mcrouter/lib/network/ReplyStatsContext.h"

namespace facebook {
namespace memcache {

/**
 * A base class for network communication with memcache protocol.
 *
 * This is an impl class, user should use AsyncMcClient.
 */
class AsyncMcClientImpl : public folly::DelayedDestruction,
                          private folly::AsyncSocket::ConnectCallback,
                          private folly::AsyncTransportWrapper::ReadCallback,
                          private folly::AsyncTransportWrapper::WriteCallback {
 public:
  enum class ConnectionDownReason {
    ERROR,
    ABORTED,
    CONNECT_TIMEOUT,
    CONNECT_ERROR,
    SERVER_GONE_AWAY,
  };

  using FlushList = boost::intrusive::list<
      folly::EventBase::LoopCallback,
      boost::intrusive::constant_time_size<false>>;

  static std::shared_ptr<AsyncMcClientImpl> create(
      folly::VirtualEventBase& eventBase,
      ConnectionOptions options);

  AsyncMcClientImpl(const AsyncMcClientImpl&) = delete;
  AsyncMcClientImpl& operator=(const AsyncMcClientImpl&) = delete;

  // Fail all requests and close connection.
  void closeNow();

  void setStatusCallbacks(
      std::function<void(const folly::AsyncSocket&)> onUp,
      std::function<void(ConnectionDownReason)> onDown);

  void setRequestStatusCallbacks(
      std::function<void(int pendingDiff, int inflightDiff)> onStateChange,
      std::function<void(int numToSend)> onWrite);

  template <class Request>
  ReplyT<Request> sendSync(
      const Request& request,
      std::chrono::milliseconds timeout,
      ReplyStatsContext* replyContext);

  void setThrottle(size_t maxInflight, size_t maxPending);

  size_t getPendingRequestCount() const;
  size_t getInflightRequestCount() const;

  void updateWriteTimeout(std::chrono::milliseconds timeout);

  /**
   * @return        The transport used to manage socket
   */
  const folly::AsyncTransportWrapper* getTransport() {
    return socket_.get();
  }

  double getRetransmissionInfo();

  template <class Request>
  double getDropProbability() const;

  void setFlushList(FlushList* flushList) {
    flushList_ = flushList;
  }

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
    std::function<void(const folly::AsyncSocket&)> onUp;
    std::function<void(ConnectionDownReason)> onDown;
  };
  struct RequestStatusCallbacks {
    std::function<void(int pendingDiff, int inflightDiff)> onStateChange;
    std::function<void(size_t numToSend)> onWrite;
  };

  folly::EventBase& eventBase_;
  std::unique_ptr<ParserT> parser_;

  // Pointer to current buffer. Updated by getReadBuffer()
  std::pair<void*, size_t> curBuffer_{nullptr, 0};

  // Socket related variables.
  ConnectionState connectionState_{ConnectionState::DOWN};
  folly::AsyncSocket::UniquePtr socket_;
  ConnectionStatusCallbacks statusCallbacks_;
  RequestStatusCallbacks requestStatusCallbacks_;

  // Debug pipe.
  ConnectionFifo debugFifo_;

  CodecIdRange supportedCompressionCodecs_ = CodecIdRange::Empty;

  McClientRequestContextQueue queue_;

  // Id for the next message that will be used by the next sendMsg() call.
  uint32_t nextMsgId_{1};

  bool outOfOrder_{false};
  bool pendingGoAwayReply_{false};

  // Throttle options (disabled by default).
  size_t maxPending_{0};
  size_t maxInflight_{0};

  // Writer loop related variables.
  class WriterLoop : public folly::EventBase::LoopCallback {
   public:
    explicit WriterLoop(AsyncMcClientImpl& client) : client_(client) {}
    ~WriterLoop() override {}
    void runLoopCallback() noexcept final;

   private:
    bool rescheduled_{false};
    AsyncMcClientImpl& client_;
  } writer_;
  FlushList* flushList_{nullptr};

  // Retransmission values
  uint32_t lastRetrans_{0}; // last known value of the no. of retransmissions
  uint64_t lastKBytes_{0}; // last known number of kBs sent

  bool isAborting_{false};

  ConnectionOptions connectionOptions_;

  std::unique_ptr<folly::EventBase::LoopCallback> eventBaseDestructionCallback_;

  // We need to be able to get shared_ptr to ourself and shared_from_this()
  // doesn't work correctly with DelayedDestruction.
  std::weak_ptr<AsyncMcClientImpl> selfPtr_;

  AsyncMcClientImpl(
      folly::VirtualEventBase& eventBase,
      ConnectionOptions options);

  ~AsyncMcClientImpl() override;

  // Common part for send/sendSync.
  void sendCommon(McClientRequestContextBase& req);

  void sendGoAwayReply();

  // Write some requests from sendQueue_ to the socket, until max inflight limit
  // is reached or queue is empty.
  void pushMessages();
  // Schedule next writer loop if it's not scheduled.
  void scheduleNextWriterLoop();
  void cancelWriterCallback();
  size_t getNumToSend() const;

  void attemptConnection();

  // Log error with additional diagnostic information.
  void logErrorWithContext(folly::StringPiece reason);
  folly::StringPiece clientStateToStr() const;

  // TAsyncSocket::ConnectCallback overrides
  void connectSuccess() noexcept final;
  void connectErr(const folly::AsyncSocketException& ex) noexcept final;

  // We've have encountered some error or we're shutting down the client.
  // It goes to DOWN state.
  void processShutdown(folly::StringPiece errorMessage);

  // AsyncTransportWrapper::ReadCallback overrides
  void getReadBuffer(void** bufReturn, size_t* lenReturn) final;
  void readDataAvailable(size_t len) noexcept final;
  void readEOF() noexcept final;
  void readErr(const folly::AsyncSocketException& ex) noexcept final;

  // AsyncTransportWrapper::WriteCallback overrides
  void writeSuccess() noexcept final;
  void writeErr(
      size_t bytesWritten,
      const folly::AsyncSocketException& ex) noexcept final;

  // Callbacks for McParser.
  template <class Reply>
  void replyReady(Reply&& reply, uint64_t reqId, ReplyStatsContext replyStats);
  void handleConnectionControlMessage(const UmbrellaMessageInfo& headerInfo);
  void parseError(mc_res_t result, folly::StringPiece reason);
  bool nextReplyAvailable(uint64_t reqId);

  static void incMsgId(uint32_t& msgId);
};
} // memcache
} // facebook

#include "AsyncMcClientImpl-inl.h"
