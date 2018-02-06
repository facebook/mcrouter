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

#include <folly/IntrusiveList.h>
#include <folly/io/async/AsyncSSLSocket.h>
#include <folly/io/async/AsyncSocket.h>
#include <folly/io/async/AsyncTransport.h>
#include <folly/io/async/DelayedDestruction.h>
#include <folly/io/async/EventBase.h>

#include "mcrouter/lib/CompressionCodecManager.h"
#include "mcrouter/lib/debug/ConnectionFifo.h"
#include "mcrouter/lib/network/AsyncMcServerWorkerOptions.h"
#include "mcrouter/lib/network/ServerMcParser.h"
#include "mcrouter/lib/network/gen/Memcache.h"

namespace facebook {
namespace memcache {

class McServerOnRequest;
class McServerRequestContext;
class MultiOpParent;
class WriteBuffer;
class WriteBufferIntrusiveList;
class WriteBufferQueue;

/**
 * A session owns a single transport, and processes the request/reply stream.
 */
class McServerSession : public folly::DelayedDestruction,
                        private folly::AsyncSSLSocket::HandshakeCB,
                        private folly::AsyncTransportWrapper::ReadCallback,
                        private folly::AsyncTransportWrapper::WriteCallback {
 private:
  folly::SafeIntrusiveListHook hook_;

 public:
  using Queue =
      folly::CountedIntrusiveList<McServerSession, &McServerSession::hook_>;

  class StateCallback {
   public:
    virtual ~StateCallback() {}
    virtual void onWriteQuiescence(McServerSession&) = 0;
    virtual void onCloseStart(McServerSession&) = 0;
    virtual void onCloseFinish(McServerSession&) = 0;
    virtual void onShutdown() = 0;
  };

  /**
   * Returns true if this object is a part of an intrusive list
   */
  bool isLinked() const noexcept {
    return hook_.is_linked();
  }

  /**
   * Creates a new session.  Sessions manage their own lifetime.
   * A session will self-destruct right after an onCloseFinish() callback
   * call, by which point all of the following must have occured:
   *   1) All outstanding requests have been replied and pending
   *      writes have been completed/errored out.
   *   2) The outgoing connection is closed, either via an explicit close()
   *      call or due to some error.
   *
   * onCloseStart() callback marks the beggining of session
   * teardown. The application can inititate any cleanup process. After
   * onCloseStart() the socket is no longer readable, and the application
   * should try to flush out all outstanding requests so that session
   * can be closed.
   *
   * The application may use onCloseFinish() callback as the point in
   * time after which the session is considered done, and no event loop
   * iteration is necessary.
   *
   * The session will be alive for the duration of onCloseFinish callback,
   * but this is the last time the application can safely assume that
   * the session is alive.
   *
   * The onWriteQuiescence() callback is invoked when all pending writes are
   * done, rather than invoking it for each write.
   *
   * @param transport  Connected transport; transfers ownership inside
   *                   this session.
   *
   * @throw            std::runtime_error if we fail to create McServerSession
   *                   object
   */
  static McServerSession& create(
      folly::AsyncTransportWrapper::UniquePtr transport,
      std::shared_ptr<McServerOnRequest> cb,
      StateCallback& stateCb,
      const AsyncMcServerWorkerOptions& options,
      void* userCtxt,
      const CompressionCodecMap* codecMap = nullptr);

  /**
   * Eventually closes the transport. All pending writes will still be drained.
   * Please refer create() for info about the callbacks invoked.
   */
  void close();

  /**
   * Same as close(), but if GoAway is enabled, will send the message to the
   * client and wait for acknowledgement before actually closing.
   */
  void beginClose(folly::StringPiece reason);

  /**
   * Returns true if there are some unfinished writes pending to the transport.
   */
  bool writesPending() const {
    return inFlight_ > 0;
  }

  /**
   * Allow clients to pause and resume reading form the sockets.
   * See pause(PauseReason) and resume(PauseReason) below.
   */
  void pause() {
    pause(PAUSE_USER);
  }
  void resume() {
    resume(PAUSE_USER);
  }

  /**
   * Get the user context associated with this session.
   */
  void* userContext() {
    return userCtxt_;
  }

  /**
   * Get the peer's socket address
   */
  const folly::SocketAddress& getSocketAddress() const noexcept {
    return socketAddress_;
  }

  /**
   * @return  the client's common name obtained from the
   *          SSL cert if this is an SSL session. Else it
   *          returns empty string.
   */
  folly::StringPiece getClientCommonName() const noexcept {
    return clientCommonName_;
  }

  /**
   * @return the EventBase for this thread
   */
  folly::EventBase& getEventBase() const noexcept {
    return eventBase_;
  }

  std::shared_ptr<CpuController> getCpuController() const noexcept {
    return options_.cpuController;
  }

  std::shared_ptr<MemoryController> getMemController() const noexcept {
    return options_.memController;
  }

 private:
  const AsyncMcServerWorkerOptions& options_;

  folly::AsyncTransportWrapper::UniquePtr transport_;
  folly::EventBase& eventBase_;
  std::shared_ptr<McServerOnRequest> onRequest_;
  StateCallback& stateCb_;

  // Debug fifo fields
  ConnectionFifo debugFifo_;
  bool hasPendingMultiOp_{false};

  enum State {
    STREAMING, /* close() was not called */
    CLOSING, /* close() was called, waiting on pending requests */
    CLOSED, /* close() was called and connection was torn down.
               This is a short lived state to prevent another close()
               between the first close() and McServerSession destruction
               from doing anything */
  };
  State state_{STREAMING};

  // Pointer to current buffer. Updated by getReadBuffer()
  std::pair<void*, size_t> curBuffer_;

  // All writes to be written at the end of the loop in a single batch.
  std::unique_ptr<WriteBufferIntrusiveList> pendingWrites_;

  /**
   * Queue of write buffers.
   * Only initialized after we know the protocol (see ensureWriteBufs())
   */
  std::unique_ptr<WriteBufferQueue> writeBufs_;

  /**
   * True iff SendWritesCallback has been scheduled.
   */
  bool writeScheduled_{false};

  /**
   * Total number of alive McTransactions in the system.
   */
  size_t inFlight_{0};

  /**
   * Total number of alive McTransactions, excluding subrequests.
   * Used to make throttling decisions.
   * The intention is to count metagets as one request as far as
   * throttling is concerned.
   */
  size_t realRequestsInFlight_{0};

  struct SendWritesCallback : public folly::EventBase::LoopCallback {
    explicit SendWritesCallback(McServerSession& session) : session_(session) {}
    void runLoopCallback() noexcept final {
      session_.sendWrites();
    }
    McServerSession& session_;
  };

  SendWritesCallback sendWritesCallback_;

  /* OR-able bits of pauseState_ */
  enum PauseReason : uint64_t {
    PAUSE_THROTTLED = 1 << 0,
    PAUSE_WRITE = 1 << 1,
    PAUSE_USER = 1 << 2,
  };

  /* Reads are enabled iff pauseState_ == 0 */
  uint64_t pauseState_{0};

  // Compression
  const CompressionCodecMap* compressionCodecMap_{nullptr};
  CodecIdRange codecIdRange_ = CodecIdRange::Empty;

  ServerMcParser<McServerSession> parser_;

  /* In-order protocol state */

  /* headReqid_ <= tailReqid_.  Since we must output replies sequentially,
     headReqid_ tracks the last reply id we're allowed to sent out.
     Out of order replies are stalled in the blockedReplies_ queue. */
  uint64_t headReqid_{0}; /**< Id of next unblocked reply */
  uint64_t tailReqid_{0}; /**< Id to assign to next request */
  std::unordered_map<uint64_t, std::unique_ptr<WriteBuffer>> blockedReplies_;

  /* If non-null, a multi-op operation is being parsed.*/
  std::shared_ptr<MultiOpParent> currentMultiop_;

  folly::SocketAddress socketAddress_;

  /**
   * If this session corresponds to an SSL session then
   * this is set to the common name from client cert
   */
  std::string clientCommonName_;

  void* userCtxt_{nullptr};

  std::unique_ptr<folly::AsyncTimeout> goAwayTimeout_;

  /**
   * pause()/resume() reads from the socket (TODO: does not affect the already
   * read buffer - requests in it will still be processed).
   *
   * We stop reading from the socket if at least one reason has a pause()
   * without a corresponding resume().
   */
  void pause(PauseReason reason);
  void resume(PauseReason reason);

  /**
   * Flush pending writes to the transport.
   */
  void sendWrites();

  /**
   * Check if no outstanding transactions, and close socket and
   * call onCloseFinish_() if so.
   */
  void checkClosed();

  void reply(std::unique_ptr<WriteBuffer> wb, uint64_t reqid);

  /**
   * Called when end context is seen (for multi-op requests) or connection
   * close to close out an in flight multi-op request.
   */
  void processMultiOpEnd();

  /* TAsyncTransport's readCallback */
  void getReadBuffer(void** bufReturn, size_t* lenReturn) final;
  void readDataAvailable(size_t len) noexcept final;
  void readEOF() noexcept final;
  void readErr(const folly::AsyncSocketException& ex) noexcept final;

  /* McParser's callback if ASCII request is read into a typed request */
  template <class Request>
  void asciiRequestReady(Request&& req, mc_res_t result, bool noreply);

  template <class Request>
  void umbrellaRequestReady(Request&& req, uint64_t reqid);
  template <class Request>
  void umbrellaRequestReadyImpl(McServerRequestContext&& ctx, Request&& req);

  void caretRequestReady(
      const UmbrellaMessageInfo& headerInfo,
      const folly::IOBuf& reqBody);

  void processConnectionControlMessage(const UmbrellaMessageInfo& headerInfo);

  void parseError(mc_res_t result, folly::StringPiece reason);

  /* Ascii parser callbacks */
  template <class Request>
  void onRequest(Request&& req, bool noreply) {
    mc_res_t result = mc_res_unknown;
    if (req.key().fullKey().size() > MC_KEY_MAX_LEN_ASCII) {
      result = mc_res_bad_key;
    }
    asciiRequestReady(std::move(req), result, noreply);
  }

  /* ASCII parser callbacks for special commands */
  void onRequest(McVersionRequest&& req, bool noreply);

  void onRequest(McShutdownRequest&& req, bool noreply);

  void onRequest(McQuitRequest&& req, bool noreply);

  void multiOpEnd();

  /**
   * Must be called after parser has detected the protocol (i.e.
   * at least one request was processed).
   * Closes the session on protocol error
   * @return True if writeBufs_ has value after this call,
   *         False on any protocol error.
   */
  void ensureWriteBufs();

  void queueWrite(std::unique_ptr<WriteBuffer> wb);

  void completeWrite();

  /* TAsyncTransport's writeCallback */
  void writeSuccess() noexcept final;
  void writeErr(
      size_t bytesWritten,
      const folly::AsyncSocketException& ex) noexcept final;

  /* AsyncSSLSocket::HandshakeCB interface */
  bool handshakeVer(
      folly::AsyncSSLSocket* sock,
      bool preverifyOk,
      X509_STORE_CTX* ctx) noexcept final;
  void handshakeSuc(folly::AsyncSSLSocket* sock) noexcept final;
  void handshakeErr(
      folly::AsyncSSLSocket* sock,
      const folly::AsyncSocketException& ex) noexcept final;

  void onTransactionStarted(bool isSubRequest);
  void onTransactionCompleted(bool isSubRequest);

  void writeToDebugFifo(const WriteBuffer* wb) noexcept;

  /**
   * Update the connection's valid range of codec ids that may be used
   * to compress the reply.  Any requests that are still in flight will be
   * replied assuming this newly updated range.
   */
  void updateCompressionCodecIdRange(
      const UmbrellaMessageInfo& headerInfo) noexcept;

  McServerSession(
      folly::AsyncTransportWrapper::UniquePtr transport,
      std::shared_ptr<McServerOnRequest> cb,
      StateCallback& stateCb,
      const AsyncMcServerWorkerOptions& options,
      void* userCtxt,
      const CompressionCodecMap* codecMap);

  McServerSession(const McServerSession&) = delete;
  McServerSession& operator=(const McServerSession&) = delete;

  friend class McServerRequestContext;
  friend class ServerMcParser<McServerSession>;
};
} // memcache
} // facebook

#include "McServerSession-inl.h"
