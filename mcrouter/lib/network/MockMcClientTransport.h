/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

#include <queue>

#include <folly/io/async/EventBase.h>
#include <thrift/lib/cpp/async/TAsyncTransport.h>

namespace facebook { namespace memcache {

/**
 * Simple TAsyncTransport implementation that consumes data and provides
 * interface to send data back (e.g. loopback comunication of caller with
 * itself).
 */
class MockMcClientTransport : public apache::thrift::async::TAsyncTransport,
                              private folly::EventBase::LoopCallback {
 public:
  using WriteFlags = apache::thrift::async::WriteFlags;

  explicit MockMcClientTransport(folly::EventBase& eventBase);

  /**
   * Provide transport with the data that should be later read from transport.
   */
  void fakeDataRead(const char* buf, size_t bytes);
  void fakeDataRead(const uint8_t* buf, size_t bytes);

  // apache::thrift::async::TAsyncTransport overrides

  void setReadCallback(ReadCallback* callback) override;
  ReadCallback* getReadCallback() const override;

  void write(WriteCallback* callback, const void* buf, size_t bytes,
             WriteFlags flags = WriteFlags::NONE) override;
  void writev(WriteCallback* callback, const iovec* vec, size_t count,
              WriteFlags flags = WriteFlags::NONE) override;
  void writeChain(WriteCallback* callback, std::unique_ptr<folly::IOBuf>&& buf,
                  WriteFlags flags = WriteFlags::NONE) override;

  // folly::AsyncTransport overrides

  void close() override;
  void closeNow() override;
  void shutdownWrite() override;
  void shutdownWriteNow() override;
  bool good() const override;
  bool readable() const override;
  bool connecting() const override;
  bool error() const override;
  void attachEventBase(folly::EventBase*) override;
  void detachEventBase() override;
  bool isDetachable() const override;
  folly::EventBase* getEventBase() const override;
  void setSendTimeout(uint32_t) override;
  uint32_t getSendTimeout() const override;
  void getLocalAddress(folly::SocketAddress*) const override;
  void getPeerAddress(folly::SocketAddress*) const override;
  bool isEorTrackingEnabled() const override;
  void setEorTracking(bool) override;
  size_t getAppBytesWritten() const override;
  size_t getRawBytesWritten() const override;
  size_t getAppBytesReceived() const override;
  size_t getRawBytesReceived() const override;
 private:
  void runLoopCallback() noexcept override;
  void ensureLoopScheduled();

  folly::EventBase& eventBase_;
  bool loopCallbackScheduled_{false};
  ReadCallback* readCallback_{nullptr};
  std::queue<std::pair<const uint8_t*,size_t>> readDataQueue_;
  std::queue<WriteCallback*> writeCallbacks_;
};

}}  // facebook::memcache
