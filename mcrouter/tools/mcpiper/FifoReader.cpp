/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "FifoReader.h"

#include <cstring>
#include <fcntl.h>
#include <vector>

#include <boost/filesystem.hpp>

#include <folly/io/async/EventBase.h>

#include "mcrouter/tools/mcpiper/ClientServerMcParser.h"
#include "mcrouter/tools/mcpiper/ParserMap.h"

namespace fs = boost::filesystem;

namespace facebook { namespace memcache {

namespace {

PacketHeader parsePacketHeader(folly::ByteRange buf) {
  CHECK(buf.size() == sizeof(PacketHeader))
    << "Invalid header buffer size!";

  PacketHeader header;
  std::memcpy(&header, buf.data(), sizeof(PacketHeader));

  CHECK(header.packetSize() <= kFifoMaxPacketSize) <<
    "Packet too large: " << header.packetSize();

  return header;
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

    while (readBuffer_.chainLength() >= sizeof(PacketHeader)) {
      auto header = parsePacketHeader(
          readBuffer_.split(sizeof(PacketHeader))->coalesce());

      if (header.packetSize() > readBuffer_.chainLength()) {
        // Wait for more data.
        pendingHeader_ = std::move(header);
        return;
      }

      feedParser(header, readBuffer_.split(header.packetSize()));
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
  auto& parser = parserMap_.fetch(header.msgId());
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
