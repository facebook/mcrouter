/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
/**
  @file asynchronous request logging (to /var/spool/mcproxy)
  for later replay by mcreplay
*/
// for printing PRIu64 in c++
#define __STDC_FORMAT_MACROS

#include "async.h"

#include <limits.h>

#ifndef IOV_MAX
/* POSIX says IOV_MAX is defined in limits.h, but glibc is broken */
#define __need_IOV_MAX /* must come before including stdio.h */
#endif
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

#include "mcrouter/_router.h"
#include "mcrouter/awriter.h"
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/lib/fibers/EventBaseLoopController.h"
#include "mcrouter/McrouterLogFailure.h"
#include "mcrouter/proxy.h"
#include "mcrouter/ProxyClientCommon.h"
#include "mcrouter/stats.h"

#define ASYNCLOG_MAGIC  "AS1.0"
#define ASYNCLOG_MAGIC2  "AS2.0"

using folly::dynamic;

namespace facebook { namespace memcache { namespace mcrouter {

namespace {

struct write_file_entry_t {
  std::string path;
  std::string contents;
  awriter_entry_t awentry;
};

// will be moved to async.h once all users of async.h are c++ code
class AsyncLogException : public std::runtime_error {
public:
  explicit AsyncLogException(const std::string& msg)
    : runtime_error(msg) {}
};

int file_entry_writer(awriter_entry_t* e) {
  int ret;
  writelog_entry_t *entry = (writelog_entry_t*)e->context;
  ssize_t size =
    folly::writeFull(entry->file->fd(),
                     entry->buf.data(),
                     entry->buf.size());
  if (size == -1) {
    ret = errno;
  } else if (size_t(size) < entry->buf.size()) {
    ret = EIO;
  } else {
    ret = 0;
  }

  return ret;
}

}  // anonymous namespace

AsyncWriter::AsyncWriter(size_t maxQueueSize)
    : maxQueueSize_(maxQueueSize),
      pid_(getpid()),
      fiberManager_(folly::make_unique<EventBaseLoopController>()) {
  auto& c = fiberManager_.loopController();
  dynamic_cast<EventBaseLoopController&>(c).attachEventBase(eventBase_);
}

AsyncWriter::~AsyncWriter() {
  stop();
  assert(!fiberManager_.hasTasks());
}

void AsyncWriter::stop() {
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
    logFailure(memcache::failure::Category::kSystemError,
               "Can not start AsyncWriter thread {}: {}", threadName, e.what());
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

  auto fWrapper = folly::makeMoveWrapper(std::move(f));
  fiberManager_.addTaskRemote([this, fWrapper]() {
    fiberManager_.runInMainContext([fWrapper]() {
      (*fWrapper)();
    });
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
               proxy->opts.async_spool.c_str(),
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
               (proxy->router ? proxy->router->opts.service_name.c_str() :
                "unknown"),
               (proxy->router ? proxy->router->opts.router_name.c_str() :
                "unknown"),
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

static void file_write_completed(awriter_entry_t *awe, int result) {
  writelog_entry_t *e = (writelog_entry_t*)awe->context;

  e->write_result = result;
  e->qentry.type = request_type_continue_reply_error;
  e->qentry.data = e;
  e->qentry.priority = 0;

  /*
   * Add the write completion to the proxy thread request queue.
   *
   * N.B. The enqueue below can only fail if we exceed the maximum queue
   *      length. Given that we haven't set a max, it cannot fail.
   */
  asox_queue_enqueue_nocopy(e->preq->proxy->request_queue, &e->qentry);
}

static writelog_entry_t* writelog_entry_new(proxy_request_t *preq,
                                            std::shared_ptr<folly::File> fd,
                                            std::string buf) {
  static const awriter_callbacks_t file_callbacks = {
    &file_write_completed,
    &file_entry_writer
  };

  writelog_entry_t *e = new writelog_entry_t();

  e->preq = preq;
  proxy_request_incref(e->preq);

  e->file = std::move(fd);

  e->buf = std::move(buf);

  e->awentry.context = e;
  e->awentry.callbacks = &file_callbacks;

  preq->delay_reply++;

  return e;
}

void writelog_entry_free(writelog_entry_t *e) {
  e->preq->delay_reply--;
  proxy_request_decref(e->preq);
  delete e;
}

static void asynclog_event(proxy_request_t *preq,
                           proxy_t *proxy,
                           const asynclog_event_type_t type,
                           const dynamic& event) {

  auto fd = asynclog_open(proxy);
  if (!fd) {
    throw AsyncLogException("asynclog_open() failed");
  }

  // ["AS1.0", 1289416829.836, "C", ["10.0.0.1", 11302, "delete foo\r\n"]]
  // OR ["AS2.0", 1289416829.836, "C", {"f":"flavor","h":"[10.0.0.1]:11302",
  //                                    "p":"pool_name","k":"foo\r\n"}]
  dynamic json = {};
  if (proxy->opts.use_asynclog_version2) {
    json.push_back(ASYNCLOG_MAGIC2);
  } else {
    json.push_back(ASYNCLOG_MAGIC);
  }

  struct timeval timestamp;
  if (gettimeofday(&timestamp, nullptr) == -1) {
    throw AsyncLogException("gettimeofday");
  }

  int timestamp_ms = timestamp.tv_usec / 1000;
  json.push_back(1e-3 * timestamp_ms + timestamp.tv_sec);

  std::string typestr(1, (char)type);
  json.push_back(typestr);

  if (!event.empty()) {
    json.push_back(event);
  }

  auto jstr = folly::toJson(json) + "\n";

  writelog_entry_t *e = writelog_entry_new(preq,
                                           fd,
                                           jstr.toStdString());
  if (!e) {
    throw AsyncLogException("Unable to allocate writelog entry");
  }

  if (!awriter_queue(preq->proxy->router->awriter.get(), &e->awentry)) {
    writelog_entry_free(e);
    throw AsyncLogException("Unable to queue writelog entry");
  }
}

/** stub, until I get marc's ascii protocol stuff into libmc */
ssize_t mc_ascii_req_to_string(const mc_msg_t* req, char* buf, size_t nbuf) {
  if (req->op == mc_op_delete) {
    return snprintf(buf, nbuf, "delete %.*s\r\n",
                    (int)req->key.len, req->key.str);
  } else if (req->op == mc_op_incr) {
    return snprintf(buf, nbuf, "incr %.*s %" PRIu64 "\r\n",
                    (int)req->key.len, req->key.str, req->delta);
  } else if (req->op == mc_op_decr) {
    return snprintf(buf, nbuf, "decr %.*s %" PRIu64 "\r\n",
                    (int)req->key.len, req->key.str, req->delta);
  } else {
    LOG(FATAL) << "don't know how to serialize " << mc_op_to_string(req->op);
    return 0;
  }
}

static void write_file_completed(awriter_entry_t* awe, int result) {
  write_file_entry_t* e = reinterpret_cast<write_file_entry_t*>(awe->context);
  delete e;
}

static int process_write_file(awriter_entry_t* awe) {
  write_file_entry_t* e = reinterpret_cast<write_file_entry_t*>(awe->context);

  if (!atomicallyWriteFileToDisk(e->contents, e->path)) {
    VLOG(1) << "Can not atomically write '" << e->path << "'";
    return -1;
  }
  return 0;
}

int async_write_file(AsyncWriter* awriter,
                     const std::string& path,
                     const std::string& contents) {
  static const awriter_callbacks_t cb = {
    &write_file_completed,
    &process_write_file
  };

  write_file_entry_t* e = new write_file_entry_t();
  e->path = path;
  e->contents = contents;
  e->awentry.context = e;
  e->awentry.callbacks = &cb;

  if (!awriter_queue(awriter, &e->awentry)) {
    delete e;
    return -1;
  }

  return 0;
}

/** Adds an asynchronous request to the event log. */
void asynclog_command(proxy_request_t *preq,
                      std::shared_ptr<const ProxyClientCommon> pclient,
                      const mc_msg_t* req,
                      folly::StringPiece poolName) {

  /* TODO: These two checks should be handled by the callers,
     but we have them here just in case for historical reasons. */

  if (req->op != mc_op_delete) {
    return;
  }

  if (preq->proxy->opts.asynclog_disable) {
    return;
  }

  dynamic json = {};
  auto host = pclient->ap.getHost();
  auto port = pclient->ap.getPort();
  char command[IOV_MAX];
  ssize_t command_len = mc_ascii_req_to_string(req, command, IOV_MAX);

  FBI_ASSERT(port > 0);
  if (command_len <= 0) {
    LOG(ERROR) << "mc_ascii_req_to_string";
    return;
  }

  if (preq->proxy->opts.use_asynclog_version2) {
    json = dynamic::object;
    json["f"] = preq->proxy->opts.router_name;
    json["h"] = folly::format("[{}]:{}", host, port).str();
    json["p"] = poolName.str();
    json["k"] = facebook::memcache::to<std::string>(req->key);
  } else {
    /* ["host", port, escaped_command] */
    json.push_back(host);
    json.push_back(port);
    json.push_back(std::string(command));
  }

  try {
    asynclog_event(preq, preq->proxy, asynclog_event_command, json);
  } catch (const AsyncLogException& e) {
    LOG(ERROR) << "asynclog_event() failed: " << e.what();
    return;
  }

  stat_incr(preq->proxy->stats, asynclog_requests_stat, 1);
}

}}} // facebook::memcache::mcrouter
