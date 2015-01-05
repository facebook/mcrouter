/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "MockMcClientTransport.h"

namespace facebook { namespace memcache {

MockMcClientTransport::MockMcClientTransport(folly::EventBase& eventBase)
    : eventBase_(eventBase) {
}

void MockMcClientTransport::fakeDataRead(const char* buf, size_t bytes) {
  fakeDataRead(reinterpret_cast<const uint8_t*>(buf), bytes);
}

void MockMcClientTransport::fakeDataRead(const uint8_t* buf, size_t bytes) {
  readDataQueue_.push(std::make_pair(buf, bytes));
}

void MockMcClientTransport::setReadCB(folly::AsyncTransportWrapper::ReadCallback* callback) {
  readCallback_ = callback;
}

MockMcClientTransport::ReadCallback*
MockMcClientTransport::getReadCallback() const {
  return dynamic_cast<MockMcClientTransport::ReadCallback*>(readCallback_);
}

void MockMcClientTransport::write(folly::AsyncTransportWrapper::WriteCallback* callback, const void* buf,
                                  size_t bytes, WriteFlags flags) {
  writeCallbacks_.push(callback);
  ensureLoopScheduled();
}

void MockMcClientTransport::writev(folly::AsyncTransportWrapper::WriteCallback* callback, const iovec* vec,
                                   size_t count, WriteFlags flags) {
  writeCallbacks_.push(callback);
  ensureLoopScheduled();
}

void MockMcClientTransport::writeChain(folly::AsyncTransportWrapper::WriteCallback* callback,
                                       std::unique_ptr<folly::IOBuf>&& buf,
                                       WriteFlags flags) {
  writeCallbacks_.push(callback);
  ensureLoopScheduled();
}

void MockMcClientTransport::close() {
  closeNow();
}

void MockMcClientTransport::closeNow() {
  if (readCallback_) {
    readCallback_->readEOF();
  }
}

void MockMcClientTransport::shutdownWrite() {
}

void MockMcClientTransport::shutdownWriteNow() {
}

bool MockMcClientTransport::good() const {
  return true;
}

bool MockMcClientTransport::readable() const {
  return true;
}

bool MockMcClientTransport::connecting() const {
  return false;
}

bool MockMcClientTransport::error() const {
  return false;
}

void MockMcClientTransport::attachEventBase(folly::EventBase*) {
  throw std::logic_error("Unsupported function call.");
}

void MockMcClientTransport::detachEventBase() {
  throw std::logic_error("Unsupported function call.");
}

bool MockMcClientTransport::isDetachable() const {
  return false;
}

folly::EventBase* MockMcClientTransport::getEventBase() const {
  return &eventBase_;
}

void MockMcClientTransport::setSendTimeout(uint32_t) {
}

uint32_t MockMcClientTransport::getSendTimeout() const {
  return 0;
}

void MockMcClientTransport::getLocalAddress(folly::SocketAddress*) const {
  throw std::logic_error("Unsupported function call.");
}

void MockMcClientTransport::getPeerAddress(folly::SocketAddress*) const {
  throw std::logic_error("Unsupported function call.");
}

bool MockMcClientTransport::isEorTrackingEnabled() const {
  throw std::logic_error("Unsupported function call.");
}

void MockMcClientTransport::setEorTracking(bool) {
  throw std::logic_error("Unsupported function call.");
}

size_t MockMcClientTransport::getAppBytesWritten() const {
  throw std::logic_error("Unsupported function call.");
}

size_t MockMcClientTransport::getRawBytesWritten() const {
  throw std::logic_error("Unsupported function call.");
}

size_t MockMcClientTransport::getAppBytesReceived() const {
  throw std::logic_error("Unsupported function call.");
}

size_t MockMcClientTransport::getRawBytesReceived() const {
  throw std::logic_error("Unsupported function call.");
}

void MockMcClientTransport::runLoopCallback() noexcept {
  loopCallbackScheduled_ = false;

  // Call write callbacks before read callbacks, that way we will reduce latency
  // between write and read callbacks.
  while (!writeCallbacks_.empty()) {
    writeCallbacks_.front()->writeSuccess();
    writeCallbacks_.pop();
  }

  if (readCallback_ == nullptr) {
    return;
  }
  // Perform reads.
  while (!readDataQueue_.empty() && readCallback_) {
    void* buffer = nullptr;
    size_t bufferSize = 0;
    readCallback_->getReadBuffer(&buffer, &bufferSize);
    uint8_t* bufferStart = reinterpret_cast<uint8_t*>(buffer);
    uint8_t* bufferEnd = bufferStart + bufferSize;
    while (bufferStart < bufferEnd && !readDataQueue_.empty()) {
      auto toCopy = std::min(static_cast<size_t>(bufferEnd - bufferStart),
                             readDataQueue_.front().second);
      memcpy(bufferStart, readDataQueue_.front().first, toCopy);
      bufferStart += toCopy;
      if (toCopy < readDataQueue_.front().second) {
        readDataQueue_.front().first += toCopy;
        readDataQueue_.front().second -= toCopy;
      } else {
        readDataQueue_.pop();
      }
    }
    readCallback_->readDataAvailable(bufferSize - (bufferEnd - bufferStart));
  }
}

void MockMcClientTransport::ensureLoopScheduled() {
  if (!loopCallbackScheduled_) {
    loopCallbackScheduled_ = true;
    eventBase_.runInLoop(this);
  }
}

}}  // facebook::memcache
