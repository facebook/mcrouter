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

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <boost/regex.hpp>
#include <folly/io/async/AsyncPipe.h>
#include <folly/io/async/AsyncSocketException.h>
#include <folly/io/IOBufQueue.h>

#include "mcrouter/lib/debug/Fifo.h"

namespace folly {
class EventBase;
};

namespace facebook { namespace memcache {

class FifoReader;
class ParserMap;

class FifoReadCallback : public folly::AsyncReader::ReadCallback {
 public:
  FifoReadCallback(std::string fifoName, ParserMap& parserMap) noexcept;

  void getReadBuffer(void** bufReturn, size_t* lenReturn) override;
  void readDataAvailable(size_t len) noexcept override;
  void readEOF() noexcept override;
  void readErr(const folly::AsyncSocketException& ex) noexcept override;

 private:
  static constexpr uint64_t kMinSize{256};
  folly::IOBufQueue readBuffer_{folly::IOBufQueue::cacheChainLength()};
  const std::string fifoName_;
  ParserMap& parserMap_;

  // Indicates if there is a pending message, i.e. a header has being read
  // (pendingHeader_) but its data hasn't being processed yet
  PacketHeader pendingHeader_;

  void feedParser(const PacketHeader& header,
                  std::unique_ptr<folly::IOBuf>&& buf);
};

/**
 * Manages all fifo readers in a directory.
 */
class FifoReaderManager {
 public:
  /**
   * Builds FifoReaderManager and starts watching "dir" for fifos
   * that match "filenamePattern".
   * If a fifo with a name that matches "filenamePattern" is found, a
   * folly:AsyncPipeReader for it is created and scheduled in "evb".
   *
   * @param evb             EventBase to run FifoReaderManager and
   *                        its FifoReaders.
   * @param map             Map of parsers.
   * @param dir             Directory to watch.
   * @param filenamePattern Regex that file names must match.
   */
  FifoReaderManager(folly::EventBase& evb, ParserMap& map,
                    std::string dir,
                    std::unique_ptr<boost::regex> filenamePattern);

  // non-copyable
  FifoReaderManager(const FifoReaderManager&) = delete;
  FifoReaderManager& operator=(const FifoReaderManager&) = delete;

 private:
  using FifoReader = std::pair<folly::AsyncPipeReader::UniquePtr,
                               std::unique_ptr<FifoReadCallback>>;

  static constexpr size_t kPollDirectoryIntervalMs = 1000;
  folly::EventBase& evb_;
  ParserMap& parserMap_;
  const std::string directory_;
  const std::unique_ptr<boost::regex> filenamePattern_;
  std::unordered_map<std::string, FifoReader> fifoReaders_;

  std::vector<std::string> getMatchedFiles() const;
  void runScanDirectory();
};

}} // facebook::memcache
