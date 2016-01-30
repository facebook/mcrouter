/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "FifoReader.h"

#include <fcntl.h>

#include <algorithm>
#include <cstring>
#include <vector>

#include <boost/filesystem.hpp>
#include <boost/regex.hpp>

#include <folly/io/async/EventBase.h>
#include <folly/SocketAddress.h>

#include "mcrouter/tools/mcpiper/ClientServerMcParser.h"
#include "mcrouter/tools/mcpiper/ParserMap.h"

namespace fs = boost::filesystem;

namespace facebook { namespace memcache {

namespace {

PacketHeader parsePacketHeader(folly::ByteRange buf) {
  CHECK(buf.size() == sizeof(PacketHeader))
    << "Invalid packet header buffer size!";

  PacketHeader header;
  std::memcpy(&header, buf.data(), sizeof(PacketHeader));

  CHECK(header.packetSize() <= kFifoMaxPacketSize) <<
    "Packet too large: " << header.packetSize();

  return header;
}

uint8_t getVersion(const folly::IOBufQueue& bufQueue) {
  const size_t kLength = sizeof(MessageHeader().magic()) +
                         sizeof(MessageHeader().version());
  CHECK(bufQueue.chainLength() >= kLength)
    << "Buffer queue length is smaller than (magic + version) bytes.";

  size_t offset = 0;
  auto buf = bufQueue.front();
  while ((offset + buf->length()) < kLength) {
    offset += buf->length();
    buf = buf->next();
  }
  return buf->data()[kLength - offset - 1];
}

MessageHeader parseMessageHeader(folly::IOBufQueue& bufQueue) {
  const size_t version = getVersion(bufQueue);
  const size_t messageHeaderSize = MessageHeader::size(version);

  CHECK(messageHeaderSize <= sizeof(MessageHeader))
    << "MessageHeader struct cannot hold message header data";
  CHECK(bufQueue.chainLength() >= messageHeaderSize)
    << "Invalid message header buffer size!";

  auto buf = bufQueue.split(messageHeaderSize);

  MessageHeader header;
  std::memcpy(&header, buf->coalesce().data(), messageHeaderSize);

  return header;
}

bool isMessageHeader(const folly::IOBufQueue& bufQueue) {
  static_assert(sizeof(uint32_t) == sizeof(MessageHeader().magic()),
                "Magic expected to be of size of uint32_t.");
  CHECK(bufQueue.chainLength() >= sizeof(MessageHeader().magic()))
    << "Buffer queue length is smaller than magic bytes.";

  uint32_t magic = 0;
  size_t i = 0;
  auto buf = bufQueue.front();
  while (i < sizeof(uint32_t)) {
    size_t j = 0;
    while (j < buf->length() && i < sizeof(uint32_t)) {
      // data is sent in little endian format.
      magic += (buf->data()[j] << (i * CHAR_BIT));
      ++i;
      ++j;
    }
    buf = buf->next();
  }
  magic = folly::Endian::little(magic);

  return magic == MessageHeader().magic();
}

} // anonymous namespace

FifoReadCallback::FifoReadCallback(std::string fifoName,
                                   ParserMap& parserMap) noexcept
    : fifoName_(std::move(fifoName)),
      parserMap_(parserMap) {
}

void FifoReadCallback::getReadBuffer(void** bufReturn, size_t* lenReturn) {
  auto res = readBuffer_.preallocate(kMinSize, PIPE_BUF);
  *bufReturn = res.first;
  *lenReturn = res.second;
}

void FifoReadCallback::readDataAvailable(size_t len) noexcept {
  try {
    readBuffer_.postallocate(len);

    // Process any pending packet headers.
    if (pendingHeader_.packetSize() > 0) {
      if (readBuffer_.chainLength() < pendingHeader_.packetSize()) {
        return;
      }
      feedParser(pendingHeader_,
                 readBuffer_.split(pendingHeader_.packetSize()));
      pendingHeader_.setPacketSize(0);
    }

    while (readBuffer_.chainLength() >= std::max(sizeof(MessageHeader),
                                                 sizeof(PacketHeader))) {
      if (isMessageHeader(readBuffer_)) {
        auto msgHeader = parseMessageHeader(readBuffer_);

        auto fromAddr = msgHeader.getLocalAddress();
        auto toAddr = msgHeader.getPeerAddress();
        if (msgHeader.direction() == MessageDirection::Received) {
          std::swap(fromAddr, toAddr);
        }
        parserMap_.fetch(msgHeader.msgId()).setAddresses(fromAddr, toAddr);
        continue;
      }

      auto packetHeader = parsePacketHeader(
          readBuffer_.split(sizeof(PacketHeader))->coalesce());
      if (packetHeader.packetSize() > readBuffer_.chainLength()) {
        // Wait for more data.
        pendingHeader_ = std::move(packetHeader);
        return;
      }

      feedParser(packetHeader, readBuffer_.split(packetHeader.packetSize()));
    }
  } catch (const std::exception& ex) {
    CHECK(false) << "Unexpected exception: " << ex.what();
  }
}

void FifoReadCallback::feedParser(const PacketHeader& header,
                                  std::unique_ptr<folly::IOBuf>&& buf) {
  auto bodyBuf = buf->coalesce();
  CHECK(bodyBuf.size() == header.packetSize())
    << "Invalid header buffer size!";
  auto& parser = parserMap_.fetch(header.msgId()).parser();
  if (header.packetId() == 0) {
    parser.reset();
  }
  parser.parse(bodyBuf);
}

void FifoReadCallback::readEOF() noexcept {
  LOG(INFO) << "Fifo \"" << fifoName_ << "\" disconnected";
}

void FifoReadCallback::readErr(const folly::AsyncSocketException& e) noexcept {
  LOG(ERROR) << "Error reading fifo \"" << fifoName_ << "\": " << e.what();
}

FifoReaderManager::FifoReaderManager(
    folly::EventBase& evb, ParserMap& map, std::string dir,
    std::unique_ptr<boost::regex> filenamePattern)
    : evb_(evb),
      parserMap_(map),
      directory_(std::move(dir)),
      filenamePattern_(std::move(filenamePattern)) {
  runScanDirectory();
}

std::vector<std::string> FifoReaderManager::getMatchedFiles() const {
  std::vector<std::string> fifos;

  try {
    if (!fs::exists(directory_)) {
      LOG(ERROR) << "Directory \"" << directory_ << "\" not found.";
    } else if (!fs::is_directory(directory_)) {
      LOG(ERROR) << "Path \"" << directory_ << "\" is not a directory.";
    } else {
      fs::directory_iterator endIt; // default construction = end iterator.
      for (fs::directory_iterator it(directory_); it != endIt; ++it) {
        if (fs::is_directory(it->status())) {
          continue;
        }
        auto& path = it->path();
        if (!filenamePattern_ ||
            boost::regex_search(path.filename().string(),
                                *filenamePattern_,
                                boost::regex_constants::match_default)) {
          fifos.emplace_back(path.string());
        }
      }
    }
  } catch (const fs::filesystem_error& ex) {
    LOG(ERROR) << "Failed to find fifos: " << ex.what();
  }

  return fifos;
}


void FifoReaderManager::runScanDirectory() {
  auto fifos = getMatchedFiles();
  for (const auto& fifo : fifos) {
    if (fifoReaders_.find(fifo) != fifoReaders_.end()) {
      continue;
    }
    auto fd = ::open(fifo.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd >= 0) {
      auto pipeReader = folly::AsyncPipeReader::UniquePtr(
          new folly::AsyncPipeReader(&evb_, fd));
      auto callback = folly::make_unique<FifoReadCallback>(fifo, parserMap_);
      pipeReader->setReadCB(callback.get());
      fifoReaders_.emplace(fifo,
                           FifoReader(std::move(pipeReader),
                                      std::move(callback)));
    } else {
      PLOG(WARNING) << "Error opening fifo: " << fifo;
    }
  }

  evb_.runAfterDelay([this]() {
      runScanDirectory();
    }, kPollDirectoryIntervalMs);
}

}} // facebook::memcache
