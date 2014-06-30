/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include "SessionTestHarness.h"

#include "mcrouter/lib/network/McServerSession.h"
#include "thrift/lib/cpp/transport/TSocketAddress.h"

using apache::thrift::async::WriteFlags;

namespace facebook { namespace memcache {

class MockAsyncSocket : public apache::thrift::async::TAsyncTransport {
 public:
  explicit MockAsyncSocket(SessionTestHarness& harness)
      : harness_(harness) {
  }

  // Methods inherited from TAsyncTransport
  void setReadCallback(ReadCallback* callback) override {
    harness_.setReadCallback(callback);
  }

  ReadCallback* getReadCallback() const override {
    return harness_.getReadCallback();
  }

  void write(WriteCallback* callback, const void* buf, size_t bytes,
             WriteFlags flags = WriteFlags::NONE) override {
    iovec op;
    op.iov_base = const_cast<void*>(buf);
    op.iov_len = bytes;
    writev(callback, &op, 1, flags);
  }

  void writev(WriteCallback* callback, const iovec* vec, size_t count,
              WriteFlags flags = WriteFlags::NONE) override {
    std::string out;
    for (size_t i = 0; i < count; ++i) {
      out += std::string(reinterpret_cast<char*>(vec[i].iov_base),
                         vec[i].iov_len);
    }

    harness_.write(out);

    if (callback) {
      callback->writeSuccess();
    }
  }

  void writeChain(WriteCallback* callback,
                  std::unique_ptr<folly::IOBuf>&& buf,
                  WriteFlags flags = WriteFlags::NONE) override {
    throw std::runtime_error("not implemented");
  }

  void close() override {}
  void closeNow() override {}
  void closeWithReset() override {}
  void shutdownWrite() override {}
  void shutdownWriteNow() override {}

  void setSendTimeout(uint32_t milliseconds) override { }
  uint32_t getSendTimeout() const override { return 0; }

  bool readable() const override { return true; }
  bool good() const override { return true; }
  bool error() const override { return false; }
  void attachEventBase(folly::EventBase* eventBase) override {}
  void detachEventBase() override {}
  folly::EventBase* getEventBase() const override {
    return &harness_.eventBase_;
  }

  bool isDetachable() const override { return false; }

  void getLocalAddress(
    apache::thrift::transport::TSocketAddress* address) const override {}
  void getPeerAddress(
    apache::thrift::transport::TSocketAddress* address) const override {}

  bool isEorTrackingEnabled() const override { return false; }

  void setEorTracking(bool track) override {}

  bool connecting() const override {
    return false;
  }

  size_t getAppBytesWritten() const override {
    return 0;
  }

  size_t getRawBytesWritten() const override {
    return 0;
  }

  size_t getAppBytesReceived() const override {
    return 0;
  }

  size_t getRawBytesReceived() const override {
    return 0;
  }

 private:
  SessionTestHarness& harness_;
  std::string corkedOutput_;
};

SessionTestHarness::SessionTestHarness(AsyncMcServerWorker::Options opts)
    : session_(
      McServerSession::create(
        apache::thrift::async::TAsyncTransport::UniquePtr(
          new MockAsyncSocket(*this)
        ),
        std::make_shared<McServerOnRequestWrapper<OnRequest>>(
          OnRequest(*this)),
        nullptr,
        nullptr,
        std::move(opts))) {
}

void SessionTestHarness::inputPacket(folly::StringPiece p) {
  savedInputs_.push_back(p.str());
  flushSavedInputs();
}

void SessionTestHarness::flushSavedInputs() {
  while (!savedInputs_.empty() && read_) {
    folly::StringPiece p = savedInputs_.front();
    void* buf;
    size_t len;
    while (!p.empty() && read_) {
      read_->getReadBuffer(&buf, &len);
      auto nread = std::min(p.size(), len);
      std::memcpy(buf, p.begin(), nread);
      read_->readDataAvailable(nread);
      p.advance(nread);
    }

    if (!p.empty()) {
      assert(!read_);
      auto rest = p.str();
      savedInputs_.pop_front();
      savedInputs_.push_front(p.str());
    } else {
      savedInputs_.pop_front();
    }
  }
}

}}  // facebook::memcache
