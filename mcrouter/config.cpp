/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include <event.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <string>
#include <utility>
#include <vector>

#include "folly/DynamicConverter.h"
#include "folly/Memory.h"
#include "folly/Range.h"
#include "mcrouter/lib/WeightedCh3HashFunc.h"
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/lib/mc/protocol.h"
#include "mcrouter/ProxyClientCommon.h"
#include "mcrouter/ProxyConfig.h"
#include "mcrouter/ProxyConfigBuilder.h"
#include "mcrouter/ProxyDestination.h"
#include "mcrouter/ProxyDestinationMap.h"
#include "mcrouter/ProxyThread.h"
#include "mcrouter/RuntimeVarsData.h"
#include "mcrouter/_router.h"
#include "mcrouter/config.h"
#include "mcrouter/dynamic_stats.h"
#include "mcrouter/flavor.h"
#include "mcrouter/proxy.h"
#include "mcrouter/routes/RateLimiter.h"
#include "mcrouter/stats.h"

namespace facebook { namespace memcache { namespace mcrouter {

proxy_pool_shadowing_policy_t::Data::Data()
    : start_index(0),
      end_index(0),
      start_key_fraction(0.0),
      end_key_fraction(0.0),
      shadow_pool(nullptr),
      shadow_type(DEFAULT_SHADOW_POLICY),
      validate_replies(false) {
}

proxy_pool_shadowing_policy_t::Data::Data(const folly::dynamic& json)
    : start_index(0),
      end_index(0),
      start_key_fraction(0.0),
      end_key_fraction(0.0),
      shadow_pool(nullptr),
      shadow_type(DEFAULT_SHADOW_POLICY),
      validate_replies(false) {
  checkLogic(json.isObject(), "shadowing_policy is not object");
  if (json.count("index_range")) {
    checkLogic(json["index_range"].isArray(),
               "shadowing_policy: index_range is not array");
    auto ar = folly::convertTo<std::vector<size_t>>(json["index_range"]);
    checkLogic(ar.size() == 2, "shadowing_policy: index_range size is not 2");
    start_index = ar[0];
    end_index = ar[1];
    checkLogic(start_index <= end_index,
               "shadowing_policy: index_range start > end");
  }
  if (json.count("key_fraction_range")) {
    checkLogic(json["key_fraction_range"].isArray(),
               "shadowing_policy: key_fraction_range is not array");
    auto ar = folly::convertTo<std::vector<double>>(json["key_fraction_range"]);
    checkLogic(ar.size() == 2,
               "shadowing_policy: key_fraction_range size is not 2");
    start_key_fraction = ar[0];
    end_key_fraction = ar[1];
    checkLogic(0 <= start_key_fraction &&
               start_key_fraction <= end_key_fraction &&
               end_key_fraction <= 1,
               "shadowing_policy: invalid key_fraction_range");
  }
  if (json.count("index_range_rv")) {
    checkLogic(json["index_range_rv"].isString(),
               "shadowing_policy: index_range_rv is not string");
    index_range_rv = json["index_range_rv"].asString().toStdString();
  }
  if (json.count("key_fraction_range_rv")) {
    checkLogic(json["key_fraction_range_rv"].isString(),
               "shadowing_policy: key_fraction_range_rv is not string");
    key_fraction_range_rv =
      json["key_fraction_range_rv"].asString().toStdString();
  }
}

proxy_pool_shadowing_policy_t::proxy_pool_shadowing_policy_t(
  const folly::dynamic& json, mcrouter_t* router)
    : data_(std::make_shared<Data>(json)) {

  if (router) {
    registerOnUpdateCallback(router);
  }
}

proxy_pool_shadowing_policy_t::proxy_pool_shadowing_policy_t(
  std::shared_ptr<Data> data,
  mcrouter_t* router)
    : data_(std::move(data)) {

  if (router) {
    registerOnUpdateCallback(router);
  }
}

proxy_pool_shadowing_policy_t::~proxy_pool_shadowing_policy_t() {
  /* We must unregister from updates before starting to destruct other
     members, like variable name strings */
  handle_.reset();
}

std::shared_ptr<const proxy_pool_shadowing_policy_t::Data>
proxy_pool_shadowing_policy_t::getData() {
  return data_.get();
}

void proxy_pool_shadowing_policy_t::registerOnUpdateCallback(
    mcrouter_t* router) {
  handle_ = router->rtVarsData.subscribeAndCall(
    [this](std::shared_ptr<const RuntimeVarsData> oldVars,
           std::shared_ptr<const RuntimeVarsData> newVars) {
      if (!newVars) {
        return;
      }
      auto dataCopy = std::make_shared<Data>(*this->getData());
      size_t start_index_temp = 0, end_index_temp = 0;
      double start_key_fraction_temp = 0, end_key_fraction_temp = 0;
      bool updateRange = false, updateKeyFraction = false;
      if (!dataCopy->index_range_rv.empty()) {
        auto valIndex =
            newVars->getVariableByName(dataCopy->index_range_rv);
        if (valIndex != nullptr) {
          checkLogic(valIndex.isArray(), "index_range_rv is not an array");

          checkLogic(valIndex.size() == 2, "Size of index_range_rv is not 2");

          checkLogic(valIndex[0].isInt(), "start_index is not an int");
          checkLogic(valIndex[1].isInt(), "end_index is not an int");
          start_index_temp = valIndex[0].asInt();
          end_index_temp = valIndex[1].asInt();
          checkLogic(start_index_temp <= end_index_temp,
                     "start_index > end_index");
          updateRange = true;
        }
      }
      if (!dataCopy->key_fraction_range_rv.empty()) {
        auto valFraction =
            newVars->getVariableByName(dataCopy->key_fraction_range_rv);
        if (valFraction != nullptr) {
          checkLogic(valFraction.isArray(),
                     "key_fraction_range_rv is not an array");
          checkLogic(valFraction.size() == 2,
                     "Size of key_fraction_range_rv is not 2");
          checkLogic(valFraction[0].isNumber(),
                     "start_key_fraction is not a number");
          checkLogic(valFraction[1].isNumber(),
                     "end_key_fraction is not a number");
          start_key_fraction_temp = valFraction[0].asDouble();
          end_key_fraction_temp = valFraction[1].asDouble();

          checkLogic(start_key_fraction_temp >= 0.0 &&
                     start_key_fraction_temp <= 1.0 &&
                     end_key_fraction_temp >= 0.0 &&
                     end_key_fraction_temp <= 1.0 &&
                     start_key_fraction_temp <= end_key_fraction_temp,
                     "Invalid values for start_key_fraction and/or "
                     "end_key_fraction");

          updateKeyFraction = true;
        }
      }

      if (updateRange) {
        dataCopy->start_index = start_index_temp;
        dataCopy->end_index = end_index_temp;
      }
      if (updateKeyFraction) {
        dataCopy->start_key_fraction = start_key_fraction_temp;
        dataCopy->end_key_fraction = end_key_fraction_temp;
      }

      this->data_.set(std::move(dataCopy));
    });
}

static void proxy_config_swap(proxy_t* proxy,
                              std::shared_ptr<ProxyConfig> config) {
  /* Update the number of server stat for this proxy. */
  stat_set_uint64(proxy, num_servers_stat, 0);
  for (auto& it : config->pools()) {
    auto pool = it.second;

    switch (pool->getType()) {
    case REGULAR_POOL:
    case REGIONAL_POOL: {
      auto proxy_pool = std::dynamic_pointer_cast<const ProxyPool>(pool);
      FBI_ASSERT(proxy_pool);
      stat_incr(proxy, num_servers_stat, proxy_pool->clients.size());
      break;
    }
    default:;
    }
  }

  sfrlock_wrlock(proxy->config_lock.get());

  auto oldConfig = std::move(proxy->config);
  proxy->config = config;

  stat_set_uint64(proxy, config_last_success_stat, time(nullptr));

  sfrlock_wrunlock(proxy->config_lock.get());

  if (oldConfig) {
    if (!proxy->opts.sync) {
      auto configReq = new old_config_req_t(std::move(oldConfig));
      asox_queue_entry_t oldConfigEntry;
      oldConfigEntry.data = configReq;
      oldConfigEntry.nbytes = sizeof(*configReq);
      oldConfigEntry.priority = 0;
      oldConfigEntry.type = request_type_old_config;
      oldConfigEntry.time_enqueued = time(nullptr);
      asox_queue_enqueue(proxy->request_queue, &oldConfigEntry);
    }
  }
}

int router_configure(mcrouter_t* router, folly::StringPiece input) {
  FBI_ASSERT(router);

  size_t proxyCount = router->opts.num_proxies;
  std::vector<std::shared_ptr<ProxyConfig>> newConfigs;
  try {
    // assume default_route, default_region and default_cluster are same for
    // each proxy
    ProxyConfigBuilder builder(
      router->opts,
      router->proxy_threads[0]->proxy->default_route,
      router->proxy_threads[0]->proxy->default_region,
      router->proxy_threads[0]->proxy->default_cluster,
      router->configApi.get(),
      input);

    for (size_t i = 0 ; i < proxyCount; i++) {
      auto proxy = router->proxy_threads[i]->proxy;

      if (proxy->default_route.empty()) {
        LOG(ERROR) << "empty default route";
        return 0;
      }

      // current connections may be reused in new config, those which are
      // not reused will eventually be removed
      proxy->destinationMap->markAllAsUnused();

      newConfigs.push_back(builder.buildConfig(proxy));
    }
  } catch (const std::exception& e) {
    LOG(ERROR) << "CRITICAL: Error creating ProxyRoute: " << e.what();
    return 0;
  }

  for (size_t i = 0; i < proxyCount; i++) {
    proxy_config_swap(router->proxy_threads[i]->proxy, newConfigs[i]);
  }

  LOG_IF(INFO, !router->opts.constantly_reload_configs) <<
      "reconfigured " << proxyCount << " proxies with " <<
      newConfigs[0]->clients().size() << " clients and " <<
      newConfigs[0]->pools().size() << " pools (" <<
      newConfigs[0]->getConfigMd5Digest() << ")";

  return 1;
}

/** (re)configure the proxy. 1 on success, 0 on error.
    NB file-based configuration is synchronous
    but server-based configuration is asynchronous */
int router_configure(mcrouter_t *router) {
  int success = 0;

  {
    std::lock_guard<SFRWriteLock> lg(router->config_reconfig_lock.writeLock());
    /* mark config attempt before, so that
       successful config is always >= last config attempt. */
    router->last_config_attempt = time(nullptr);

    std::string config;
    success = router->configApi->getConfigFile(config);
    if (success) {
      success = router_configure_from_string(router, config);
    } else {
      LOG(INFO) << "Can not read config file";
    }

    if (!success) {
      router->config_failures++;
    }
  }

  return success;
}

ProxyPool::ProxyPool(std::string name)
    : ProxyGenericPool(std::move(name)),
      hash(proxy_hash_crc32),
      protocol(mc_unknown_protocol),
      transport(mc_unknown_transport),
      delete_time(0),
      keep_routing_prefix(0),
      devnull_asynclog(false),
      failover_exptime(0),
      pool_failover_policy(nullptr) {

  memset(&timeout, 0, sizeof(timeval_t));
}

ProxyPool::~ProxyPool() {
  for (auto& c : clients) {
    auto client = c.lock();
    if (client) {
      /* Clearing the pool pointer in the pclient should only be done
         if the pclient's pool matches the one being freed.  The
         reconfiguration code reuses pclients, and may have
         readjusted the pclient's pool to point to a new pool. */
      if (client->pool == this) {
        client->pool = nullptr;
      }
    }
  }
  if (pool_failover_policy) {
    delete pool_failover_policy;
  }
}

ProxyMigratedPool::ProxyMigratedPool(std::string name)
    : ProxyGenericPool(std::move(name)),
      from_pool(nullptr),
      to_pool(nullptr),
      migration_start_ts(0),
      migration_interval_sec(0),
      warming_up(false),
      warmup_exptime(0) {
}

proxy_pool_failover_policy_t::proxy_pool_failover_policy_t() {
  memset(op, 0, sizeof(op));
}

}}} // facebook::memcache::mcrouter
