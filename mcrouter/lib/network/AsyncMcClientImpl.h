/*
 *  Copyright (c) 2016, Facebook, Inc.
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
#include <folly/io/async/AsyncTransport.h>
#include <folly/io/async/DelayedDestruction.h>
#include <folly/io/IOBufQueue.h>

#include "mcrouter/lib/CompressionCodecManager.h"
#include "mcrouter/lib/Operation.h"
#include "mcrouter/lib/debug/ConnectionFifo.h"
#include "mcrouter/lib/fbi/cpp/ObjectPool.h"
#include "mcrouter/lib/network/ClientMcParser.h"
#include "mcrouter/lib/network/ConnectionOptions.h"
#include "mcrouter/lib/network/McClientRequestContext.h"

namespace facebook { namespace memcache {

namespace detail {
class OnEventBaseDestructionCallback;
} // detail

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

  void setRequestStatusCallbacks(
      std::function<void(int pendingDiff, int inflightDiff)> onStateChange,
      std::function<void(int numToSend)> onWrite);

  void setCompressionCallback(
      std::function<void(bool, size_t, size_t)> compressionCallback);

  template <class Request>
  ReplyT<Request> sendSync(const Request& request,
                           std::chrono::milliseconds timeout);

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
  struct RequestStatusCallbacks {
    std::function<void(int pendingDiff, int inflightDiff)> onStateChange;
    std::function<void(size_t numToSend)> onWrite;
  };

  // We need to be able to get shared_ptr to ourself and shared_from_this()
  // doesn't work correctly with DelayedDestruction.
  std::weak_ptr<AsyncMcClientImpl> selfPtr_;

  folly::EventBase& eventBase_;
  std::unique_ptr<ParserT> parser_;

  // Pointer to current buffer. Updated by getReadBuffer()
  std::pair<void*, size_t> curBuffer_{nullptr, 0};

  // Socket related variables.
  ConnectionState connectionState_{ConnectionState::DOWN};
  ConnectionOptions connectionOptions_;
  folly::AsyncTransportWrapper::UniquePtr socket_;
  ConnectionStatusCallbacks statusCallbacks_;
  RequestStatusCallbacks requestStatusCallbacks_;
  std::function<void(bool, size_t, size_t)> compressionCallback_;

  // Debug pipe.
  ConnectionFifo debugFifo_;

  CodecIdRange supportedCompressionCodecs_ = CodecIdRange::Empty;

  bool outOfOrder_{false};
  McClientRequestContextQueue queue_;

  // Id for the next message that will be used by the next sendMsg() call.
  uint64_t nextMsgId_{1};

  // Throttle options (disabled by default).
  size_t maxPending_{0};
  size_t maxInflight_{0};

  // Retransmission values
  uint32_t lastRetrans_{0}; // last known value of the no. of retransmissions
  double approxPackets_{0.0}; // overestimation of the number of packets sent

  // Writer loop related variables.
  class WriterLoop;
  bool writeScheduled_{false};
  std::unique_ptr<WriterLoop> writer_;

  bool isAborting_{false};
  std::unique_ptr<detail::OnEventBaseDestructionCallback>
    eventBaseDestructionCallback_;

  AsyncMcClientImpl(folly::EventBase& eventBase,
                    ConnectionOptions options);

  ~AsyncMcClientImpl();

  // Common part for send/sendSync.
  void sendCommon(McClientRequestContextBase& req);

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
  void connectSuccess() noexcept override final;
  void connectErr(const folly::AsyncSocketException& ex)
    noexcept override final;

  // We've have encountered some error or we're shutting down the client.
  // It goes to DOWN state.
  void processShutdown();

  // AsyncTransportWrapper::ReadCallback overrides
  void getReadBuffer(void** bufReturn, size_t* lenReturn) override final;
  void readDataAvailable(size_t len) noexcept override final;
  void readEOF() noexcept override final;
  void readErr(const folly::AsyncSocketException& ex) noexcept override final;

  // AsyncTransportWrapper::WriteCallback overrides
  void writeSuccess() noexcept override final;
  void writeErr(size_t bytesWritten,
                const folly::AsyncSocketException& ex) noexcept override final;

  // Callbacks for McParser.
  template <class Reply>
  void replyReady(Reply&& reply, uint64_t reqId);
  void parseError(mc_res_t result, folly::StringPiece reason);
  bool nextReplyAvailable(uint64_t reqId);
  /**
   * Callback function called by McParser on each reply with compression status.
   *
   * @param replyCompressed             True if the reply was compressed. False
   *                                    otherwise.
   * @param numBytesBeforeCompression   Number of bytes before compression.
   * @param numBytesAfterCompression    Number of bytes after compression.
   */
  void updateCompressionStats(
      bool replyCompressed,
      size_t numBytesBeforeCompression,
      size_t numBytesAfterCompression);

  void sendFakeReply(McClientRequestContextBase& request);

  static void incMsgId(size_t& msgId);
};

}} // facebook::memcache

#include "AsyncMcClientImpl-inl.h"
