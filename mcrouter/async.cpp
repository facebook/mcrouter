/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "async.h"

#include <limits.h>
#include <stdio.h>

#include <fcntl.h>
#include <inttypes.h>
#include <sys/dir.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>

#include <folly/Conv.h>
#include <folly/File.h>
#include <folly/FileUtil.h>
#include <folly/json.h>
#include <folly/ThreadName.h>
#include <folly/experimental/fibers/EventBaseLoopController.h>

#include "mcrouter/awriter.h"
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/McrouterInstance.h"
#include "mcrouter/McrouterLogFailure.h"
#include "mcrouter/proxy.h"
#include "mcrouter/ProxyClientCommon.h"
#include "mcrouter/stats.h"

#define ASYNCLOG_MAGIC  "AS1.0"
#define ASYNCLOG_MAGIC2  "AS2.0"

using folly::dynamic;

namespace facebook { namespace memcache { namespace mcrouter {

AsyncWriter::AsyncWriter(size_t maxQueueSize)
    : maxQueueSize_(maxQueueSize),
      pid_(getpid()),
      fiberManager_(
          folly::make_unique<folly::fibers::EventBaseLoopController>()),
      eventBase_(/* enableTimeMeasurement */ false) {
  auto& c = fiberManager_.loopController();
  dynamic_cast<folly::fibers::EventBaseLoopController&>(c)
      .attachEventBase(eventBase_);
}

AsyncWriter::~AsyncWriter() {
  stop();
  assert(!fiberManager_.hasTasks());
}

void AsyncWriter::stop() noexcept {
  {
    std::lock_guard<SFRWriteLock> lock(runLock_.writeLock());
    if (stopped_) {
      return;
    }
    stopped_ = true;
  }

  if (thread_.joinable()) {
    eventBase_.terminateLoopSoon();
    if (pid_ == getpid()) {
      thread_.join();
    } else {
      // fork workaround
      thread_.detach();
    }
  } else {
    while (fiberManager_.hasTasks()) {
      fiberManager_.loopUntilNoReady();
    }
  }
}

bool AsyncWriter::start(folly::StringPiece threadName) {
  std::lock_guard<SFRWriteLock> lock(runLock_.writeLock());
  if (thread_.joinable() || stopped_) {
    return false;
  }

  try {
    thread_ = std::thread([this]() {
      // will return after terminateLoopSoon is called
      eventBase_.loopForever();

      while (fiberManager_.hasTasks()) {
        fiberManager_.loopUntilNoReady();
      }
    });
    folly::setThreadName(thread_.native_handle(), threadName);
  } catch (const std::system_error& e) {
    LOG_FAILURE("mcrouter", memcache::failure::Category::kSystemError,
                "Can not start AsyncWriter thread {}: {}", threadName,
                e.what());
    return false;
  }

  return true;
}

bool AsyncWriter::run(std::function<void()> f) {
  std::lock_guard<SFRReadLock> lock(runLock_.readLock());
  if (stopped_) {
    return false;
  }

  if (maxQueueSize_ != 0) {
    auto size = queueSize_.load();
    do {
      if (maxQueueSize_ == size) {
        return false;
      }
    } while (!queueSize_.compare_exchange_weak(size, size + 1));
  }

  fiberManager_.addTaskRemote([this, f_ = std::move(f)]() {
      fiberManager_.runInMainContext(std::move(f_));
      if (maxQueueSize_ != 0) {
        --queueSize_;
      }
  });
  return true;
}

bool awriter_queue(AsyncWriter* w, awriter_entry_t *e) {
  return w->run([e, w] () {
    if (!w->isActive()) {
      e->callbacks->completed(e, EPIPE);
      return;
    }
    int r = e->callbacks->perform_write(e);
    e->callbacks->completed(e, r);
  });
}

static std::shared_ptr<folly::File> countedfd_new(int fd) {
  if (fd < 0) {
    return nullptr;
  }
  return std::make_shared<folly::File>(fd, true);
}

/** Opens the asynchronous request store.  */
static std::shared_ptr<folly::File> asynclog_open(proxy_t *proxy) {
  char path[PATH_MAX + 1];
  time_t now = time(nullptr);
  pid_t tid = syscall(SYS_gettid);
  struct tm date;
  struct stat st;
  int success = 0;
  int fd = -1;

  if (proxy->async_fd &&
      now - proxy->async_spool_time <= DEFAULT_ASYNCLOG_LIFETIME) {
    return proxy->async_fd;
  }

  if (proxy->async_fd) {
    proxy->async_fd = nullptr;
  }

  localtime_r(&now, &date);
  char hour_path[PATH_MAX+1];
  time_t hour_time = now - (now % 3600);
  if (snprintf(hour_path, PATH_MAX, "%s/%04d%02d%02dT%02d-%lld",
               proxy->router().opts().async_spool.c_str(),
               date.tm_year + 1900,
               date.tm_mon + 1,
               date.tm_mday,
               date.tm_hour,
               (long long) hour_time) > PATH_MAX) {
    hour_path[PATH_MAX] = '\0';
    LOG(ERROR) << "async log hourly spool path is too long: " << hour_path;
    goto epilogue;
  }

  if (stat(hour_path, &st) != 0) {
    mode_t old_umask = umask(0);
    int ret = mkdir(hour_path, 0777);
    int mkdir_errno = 0;
    if (ret != 0) {
      mkdir_errno = errno;
    }
    if (old_umask != 0) {
      umask(old_umask);
    }
    /* EEXIST is possible due to a race. We don't care. */
    if (ret != 0 && mkdir_errno != EEXIST) {
      LOG(ERROR) << "couldn't create async log hour spool path: " <<
                    hour_path << ". reason: " << strerror(mkdir_errno);
      goto epilogue;
    }
  }

  if (snprintf(path, PATH_MAX, "%s/%04d%02d%02dT%02d%02d%02d-%lld-%s-%s-t%d-%p",
               hour_path,
               date.tm_year + 1900,
               date.tm_mon + 1,
               date.tm_mday,
               date.tm_hour,
               date.tm_min,
               date.tm_sec,
               (long long) now,
               proxy->router().opts().service_name.c_str(),
               proxy->router().opts().router_name.c_str(),
               tid,
               proxy) > PATH_MAX) {
    path[PATH_MAX] = '\0';
    LOG(ERROR) << "async log path is too long: " << path;
    goto epilogue;
  }

  /*
   * Just in case, append to the log if it exists
   */
  if (stat(path, &st) != 0) {
    fd = open(path, O_WRONLY | O_CREAT, 0666);
    if (fd < 0) {
      LOG(ERROR) << "Can't create and open async store " << path << ": " <<
                    strerror(errno);
      goto epilogue;
    }
  } else {
    fd = open(path, O_WRONLY | O_APPEND, 0666);
    if (fd < 0) {
      LOG(ERROR) << "Can't re-open async store " << path << ": " <<
                    strerror(errno);
      goto epilogue;
    }
  }

  if (fstat(fd, &st)) {
    LOG(ERROR) << "Can't stat async store " << path << ": " << strerror(errno);
    goto epilogue;
  }
  if (!S_ISREG(st.st_mode)) {
    LOG(ERROR) << "Async store exists but is not a file: " << path << ": " <<
                  strerror(errno);
    goto epilogue;
  }

  proxy->async_fd = countedfd_new(fd);
  if (!proxy->async_fd) {
    LOG(ERROR) << "Unable to allocate memory for async_fd: " << strerror(errno);
    goto epilogue;
  }

  /* Ownership of the descriptor has been passed to prox->async_fd. */
  fd = -1;

  proxy->async_spool_time = now;

  success = 1;

  VLOG(1) << "Opened async store for " << path;

epilogue:
  if (!success) {
    if (fd != -1) {
      close(fd);
    }
    if (proxy->async_fd) {
      proxy->async_fd = nullptr;
    }
  }
  return proxy->async_fd;
}

/** Adds an asynchronous request to the event log. */
void asynclog_delete(proxy_t* proxy,
                     const ProxyClientCommon& pclient,
                     folly::StringPiece key,
                     folly::StringPiece poolName) {
  dynamic json = {};
  const auto& host = pclient.ap->getHost();
  const auto& port = proxy->router().opts().asynclog_port_override == 0
    ? pclient.ap->getPort()
    : proxy->router().opts().asynclog_port_override;

  if (proxy->router().opts().use_asynclog_version2) {
    json = dynamic::object;
    json["f"] = proxy->router().opts().flavor_name;
    json["h"] = folly::sformat("[{}]:{}", host, port);
    json["p"] = poolName.str();
    json["k"] = key.str();
  } else {
    /* ["host", port, escaped_command] */
    json.push_back(host);
    json.push_back(port);
    json.push_back(folly::sformat("delete {}\r\n", key));
  }

  auto fd = asynclog_open(proxy);
  if (!fd) {
    MC_LOG_FAILURE(proxy->router().opts(),
                   memcache::failure::Category::kSystemError,
                   "asynclog_open() failed (key {}, pool {})",
                   key, poolName);
    return;
  }

  // ["AS1.0", 1289416829.836, "C", ["10.0.0.1", 11302, "delete foo\r\n"]]
  // OR ["AS2.0", 1289416829.836, "C", {"f":"flavor","h":"[10.0.0.1]:11302",
  //                                    "p":"pool_name","k":"foo\r\n"}]
  dynamic jsonOut = {};
  if (proxy->router().opts().use_asynclog_version2) {
    jsonOut.push_back(ASYNCLOG_MAGIC2);
  } else {
    jsonOut.push_back(ASYNCLOG_MAGIC);
  }

  struct timeval timestamp;
  CHECK(gettimeofday(&timestamp, nullptr) == 0);

  auto timestamp_ms =
    facebook::memcache::to<std::chrono::milliseconds>(timestamp).count();

  jsonOut.push_back(1e-3 * timestamp_ms);
  jsonOut.push_back(std::string("C"));

  jsonOut.push_back(json);

  auto jstr = folly::toJson(jsonOut) + "\n";

  ssize_t size = folly::writeFull(fd->fd(), jstr.data(), jstr.size());
  if (size == -1 || size_t(size) < jstr.size()) {
    MC_LOG_FAILURE(proxy->router().opts(),
                   memcache::failure::Category::kSystemError,
                   "Error fully writing asynclog request (key {}, pool {})",
                   key, poolName);
  }
}

}}} // facebook::memcache::mcrouter
