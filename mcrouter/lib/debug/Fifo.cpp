/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "Fifo.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <boost/filesystem.hpp>
#include <glog/logging.h>

#include <folly/Bits.h>
#include <folly/FileUtil.h>

#include "mcrouter/lib/fbi/cpp/util.h"

namespace facebook { namespace memcache {

namespace {

bool exists(const char* fifoPath) {
  return access(fifoPath, F_OK) == 0;
}

bool canWrite(const char* fifoPath) {
  return access(fifoPath, W_OK) == 0;
}

bool create(const char* fifoPath) {
  return mkfifo(fifoPath, 0666) == 0;
}

bool removeFile(const char* fifoPath) {
  return ::remove(fifoPath) == 0;
}

bool isFifo(const char* fifoPath) {
  struct stat st;
  return (stat(fifoPath, &st) != -1) && S_ISFIFO(st.st_mode);
}

/**
 * Writes data to the PIPE.
 * Note: If the message is greater than PIPE_BUF (currently 4096 on
 * linux), writev will be called multiple times.
 */
bool doWrite(int pipeFd, uint64_t msgId,
             const struct iovec* iov, size_t iovcnt) noexcept {
  static_assert(IOV_MAX > 1, "IOV_MAX must be larger than 1.");

  int ioVecMaxSize = std::min(16, IOV_MAX);
  iovec pipeIov[ioVecMaxSize];

  // This method writes data to the fifo in a specific format, described bellow:
  //  - The data is divided into packets of at most PIPE_BUF bytes.
  //  - The collection of all packets written by a single call of doWrite()
  //    forms a message.
  //  - Each packet is composed of two parts: header and body.
  //    - The header contains metadata about the packet, most notably:
  //      - message id  - the id of the message the packet belogs to.
  //                      8 bytes, little endian.
  //      - packet id   - Incremental id of the packet, starting at 0.
  //                      2 bytes, little endian.
  //      - packet size - The size of the body of the packet.
  //                      2 bytes, little endian.
  //    - The body is the data itself (i.e. contents of iov).
  //  - Each packet is atomically written to the pipe. That means that
  //    the entire packet (header and body) will appear together in the
  //    pipe, as expected. The various packets of a message might be
  //    interleaved with the packets of another message though.

  // Create header
  PacketHeader header;
  header.msgId = folly::Endian::little(msgId);
  header.packetSize = 0;
  header.packetId = 0;
  uint32_t packetId = 0;
  pipeIov[0].iov_base = &header;
  pipeIov[0].iov_len = sizeof(PacketHeader);

  // Send data
  size_t iovIndex = 0;
  char* buf = reinterpret_cast<char*>(iov[iovIndex].iov_base);
  size_t bufLen = iov[iovIndex].iov_len;
  while (iovIndex < iovcnt) {
    size_t remSize = PIPE_BUF - sizeof(PacketHeader);
    size_t pipeIovIndex = 1;

    // Build pipeIov
    while (iovIndex < iovcnt && remSize > 0 && pipeIovIndex < ioVecMaxSize) {
      pipeIov[pipeIovIndex].iov_base = buf;
      if (bufLen <= remSize) {
        pipeIov[pipeIovIndex].iov_len = bufLen;
        remSize -= bufLen;
        ++iovIndex;
        if (iovIndex < iovcnt) {
          buf = reinterpret_cast<char*>(iov[iovIndex].iov_base);
          bufLen = iov[iovIndex].iov_len;
        }
      } else {
        pipeIov[pipeIovIndex].iov_len = remSize;
        buf += remSize;
        bufLen -= remSize;
        remSize = 0;
      }
      ++pipeIovIndex;
    }
    header.packetSize = folly::Endian::little(
        PIPE_BUF - remSize - sizeof(PacketHeader));
    header.packetId = folly::Endian::little(packetId);

    // Write to pipe
    auto res = folly::writevNoInt(pipeFd, pipeIov, pipeIovIndex);
    if (res == -1) {
      if (errno != EAGAIN) {
        PLOG(WARNING) << "Error writing to debug pipe.";
      }
      return false;
    }
    ++packetId;
  }

  return true;
}

} // anonymous namespace

Fifo::Fifo(std::string path) : path_(std::move(path)) {
  CHECK(!path_.empty()) << "Fifo path cannot be empty";
  auto dir = boost::filesystem::path(path_).parent_path().string();
  ensureDirExistsAndWritable(dir);
}

Fifo::~Fifo() {
  disconnect();
  if (exists(path_.c_str())) {
    if (!removeFile(path_.c_str())) {
      PLOG(ERROR) << "Error removing debug fifo file";
    }
  }
}

bool Fifo::isConnected() const noexcept {
  return fd_ >= 0;
}

bool Fifo::tryConnect() noexcept {
  if (isConnected()) {
    return true;
  }

  if (!exists(path_.c_str())) {
    if (!create(path_.c_str())) {
      PLOG(WARNING) << "Error creating debug fifo at \""
                    << path_ << "\".";
      return false;
    }
  }

  if (!isFifo(path_.c_str()) || !canWrite(path_.c_str())) {
    if (!removeFile(path_.c_str()) ||
        !create(path_.c_str())) {
      return false;
    }
  }

  int fd = folly::openNoInt(path_.c_str(), O_WRONLY | O_NONBLOCK);
  if (fd >= 0) {
    fd_.store(fd);
    return true;
  }

  return false;
}

void Fifo::disconnect() noexcept {
  auto oldFd = fd_.exchange(-1);
  if (oldFd >= 0) {
    close(oldFd);
  }
}

bool Fifo::writeIfConnected(uint64_t msgId, void* buf, size_t len) noexcept {
  iovec iov[1];
  iov[0].iov_base = buf;
  iov[0].iov_len = len;
  return writeIfConnected(msgId, iov, 1);
}

bool Fifo::writeIfConnected(uint64_t msgId,
                            const struct iovec* iov,
                            size_t iovcnt) noexcept {
  if (!isConnected()) {
    return false;
  }

  auto written = doWrite(fd_, msgId, iov, iovcnt);
  if (!written && errno == EPIPE) {
    disconnect();
  }
  return written;
}

}} // facebook::memcache
