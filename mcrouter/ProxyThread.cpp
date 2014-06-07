#include "ProxyThread.h"

#include "folly/io/async/EventBase.h"
#include "mcrouter/_router.h"
#include "mcrouter/config.h"
#include "mcrouter/proxy.h"

namespace facebook { namespace memcache { namespace mcrouter {

ProxyThread::ProxyThread(proxy_t* proxy_)
  : proxy(proxy_),
    thread_handle(0),
    thread_stack(nullptr),
    isSafeToDeleteProxy(false) {
}

int ProxyThread::spawn() {
  return spawn_thread(&thread_handle,
                      &thread_stack,
                      proxyThreadRunHandler, this,
                      proxy->router->wantRealtimeThreads());
}

void ProxyThread::stopAndJoin() {
  if (thread_handle && proxy->router->pid == getpid()) {
    FBI_ASSERT(proxy->request_queue != nullptr);
    asox_queue_entry_t entry;
    entry.type = request_type_router_shutdown;
    entry.priority = 0;
    entry.data = nullptr;
    entry.nbytes = 0;
    asox_queue_enqueue(proxy->request_queue, &entry);
    {
      std::unique_lock<std::mutex> lk(mux);
      isSafeToDeleteProxy = true;
    }
    cv.notify_all();
    pthread_join(thread_handle, nullptr);
  }
  if (thread_stack) {
    free(thread_stack);
  }
}

void ProxyThread::stopAwriterThreads() {
  auto router = proxy->router;
  if (router->pid == getpid()) {
    proxy_stop_awriter_threads(proxy);
  }
  if (proxy->awriter_thread_stack != nullptr) {
    free(proxy->awriter_thread_stack);
  }
  if (proxy->stats_log_writer_thread_stack != nullptr) {
    free(proxy->stats_log_writer_thread_stack);
    proxy->stats_log_writer_thread_stack = nullptr;
  }
}

void ProxyThread::proxyThreadRun() {
  FBI_ASSERT(proxy->router != nullptr);
  mcrouter_set_thread_name(pthread_self(), proxy->router->opts, "mcrpxy");

  while(!proxy->router->shutdownStarted()) {
    mcrouterLoopOnce(proxy->eventBase);
  }

  while (proxy->fiberManager.hasTasks()) {
    mcrouterLoopOnce(proxy->eventBase);
  }

  stopAwriterThreads();
  // Delete the proxy from the proxy thread so that the clients get
  // deleted from the same thread where they were created.
  folly::EventBase *eventBase = proxy->eventBase;
  std::unique_lock<std::mutex> lk(mux);
  // This is to avoid a race condition where proxy is deleted
  // before the call to stopAndJoin is made.
  cv.wait(lk,
    [this]() {
      return this->isSafeToDeleteProxy;
    });
  delete proxy;
  if (eventBase != nullptr) {
    delete eventBase;
  }
}

void *ProxyThread::proxyThreadRunHandler(void *arg) {
  auto proxyThread = reinterpret_cast<ProxyThread*>(arg);
  proxyThread->proxyThreadRun();
  return nullptr;
}

}}}  // facebook::memcache::mcrouter
