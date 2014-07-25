/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

#include "mcrouter/lib/network/AsyncMcServerWorker.h"
#include "mcrouter/lib/network/McServerParser.h"
#include "mcrouter/lib/network/McServerTransaction.h"
#include "thrift/lib/cpp/async/TAsyncTransport.h"

namespace facebook { namespace memcache {

class McServerOnRequest;

/**
 * A session owns a single transport, and processes the request/reply stream.
 */
class McServerSession :
      public apache::thrift::async::TDelayedDestruction,
      private apache::thrift::async::TAsyncTransport::ReadCallback,
      private apache::thrift::async::TAsyncTransport::WriteCallback,
      private McServerParser::ParseCallback {
 public:

  /**
   * @param transport  Connected transport; transfers ownership inside
   *                   this session.
   * @param onClosed   Will be called after the transport is closed,
   *                   passing a shared_ptr to this object. Note:
   *                   the passed in shared_ptr might be nullptr
   *                   in the situation where the session is only held
   *                   alive by a DestructorGuard.
   */
  static std::shared_ptr<McServerSession> create(
    apache::thrift::async::TAsyncTransport::UniquePtr transport,
    std::shared_ptr<McServerOnRequest> cb,
    std::function<void(std::shared_ptr<McServerSession>)> onClosed,
    std::function<void()> onShutdown,
    AsyncMcServerWorker::Options options);

  /**
   * Eventually closes the transport. All pending writes will still be drained.
   * onClosed callback will only be called after the last write completes.
   */
  void close();

  /**
   * Returns true if there are some unfinished writes pending to the transport.
   */
  bool writesPending() const {
    return inFlight_ > 0;
  }

 private:
  /**
   * This object must be handled through a shared_ptr
   * with a TDelayedDestruction destructor, as enforced by create().
   * weakThis_ is how this object can create more shared_ptrs
   * to itself.  If all shared_ptrs are gone, but the object is held
   * alive by a DestructorGuard, weakThis_.lock() will be null, signifiying
   * the impending destruction.
   *
   * This is basically std::enable_shared_from_this, but shared_from_this()
   * would always throw once all shared_ptrs are gone.
   */
  std::weak_ptr<McServerSession> weakThis_;

  apache::thrift::async::TAsyncTransport::UniquePtr transport_;
  std::shared_ptr<McServerOnRequest> onRequest_;
  std::function<void(std::shared_ptr<McServerSession>)> onClosed_;
  std::function<void()> onShutdown_;
  AsyncMcServerWorker::Options options_;

  enum State {
    STREAMING,
    SHUTDOWN_READ,
    CLOSING,
  };
  State state_{STREAMING};

  McServerParser parser_;

  /**
   * Requests (possibly replied) which we didn't write to the network yet
   */
  McServerTransaction::Queue unansweredRequests_;

  /**
   * Buffer for an incoming multiget request components
   */
  McServerTransaction::Queue multigetRequests_;

  /**
   * Transactions with replies being written to the transport after the current
   * loop iteration
   */
  McServerTransaction::Queue pendingWrites_;

  /**
   * Each batch contains transactions with replies already written
   * to the transport, waiting on write success.
   */
  std::deque<McServerTransaction::Queue> writeBatches_;

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
    void runLoopCallback() noexcept override;
    McServerSession& session_;
  };

  SendWritesCallback sendWritesCallback_;

  /* OR-able bits of pauseState_ */
  enum PauseReason : uint64_t {
    PAUSE_THROTTLED = 0x1,
    PAUSE_WRITE = 0x2,
  };

  /* Reads are enabled iff pauseState_ == 0 */
  uint64_t pauseState_{0};

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
   * Check if no outstanding transactions, and close socket/call onClosed if so.
   */
  void checkClosed();

  /**
   * Flush any already replied requests (replies might be queued up
   * due to head-of-line blocking)
   *
   * @param transaction  A reference to the transaction from unansweredRequests_
   *                     that became replied.  If the protocol is out of order,
   *                     this will simply write out the reply for that request
   *                     only immediately.
   */
  void onRequestReplied(McServerTransaction& transaction);

  /* TAsyncTransport's readCallback */
  void getReadBuffer(void** bufReturn, size_t* lenReturn) override;
  void readDataAvailable(size_t len) noexcept override;
  void readEOF() noexcept override;
  void readError(const apache::thrift::transport::TTransportException& ex)
    noexcept override;

  /* McServerParser's parseCallback */
  void requestReady(McRequest req,
                    mc_op_t operation,
                    uint64_t reqid,
                    mc_res_t result,
                    bool noreply);
  void parseError(McReply reply);

  /**
   * Queue up the transaction for writing.
   */
  void queueWrite(std::unique_ptr<McServerTransaction> transaction);

  void completeWrite();

  /* TAsyncTransport's writeCallback */
  void writeSuccess() noexcept override;
  void writeError(size_t bytesWritten,
                  const apache::thrift::transport::TTransportException& ex)
    noexcept override;

  void onTransactionStarted(bool isSubRequest);
  void onTransactionCompleted(bool isSubRequest);

  McServerSession(
    apache::thrift::async::TAsyncTransport::UniquePtr transport,
    std::shared_ptr<McServerOnRequest> cb,
    std::function<void(std::shared_ptr<McServerSession>)> onClosed,
    std::function<void()> onShutdown,
    AsyncMcServerWorker::Options options);

  McServerSession(const McServerSession&) = delete;
  McServerSession& operator=(const McServerSession&) = delete;

  friend class McServerTransaction;
};

}}  // facebook::memcache
