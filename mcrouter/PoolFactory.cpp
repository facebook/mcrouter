/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include "PoolFactory.h"

#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>

#include <folly/json.h>
#include <folly/Memory.h>

#include "mcrouter/ConfigApi.h"
#include "mcrouter/flavor.h"
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/lib/WeightedCh3HashFunc.h"
#include "mcrouter/McrouterLogFailure.h"
#include "mcrouter/options.h"
#include "mcrouter/priorities.h"
#include "mcrouter/proxy.h"
#include "mcrouter/ProxyClientCommon.h"
#include "mcrouter/routes/RateLimiter.h"
#include "mcrouter/routes/ShardSplitter.h"

using facebook::memcache::Md5Hash;
using folly::dynamic;
using std::make_shared;
using std::string;

#define DYNAMIC_EXPECT(v, t, n)                                        \
  if (v.type() != t) {                                                 \
    LOG(ERROR) << "Expected dynamic" #t " (" n ")";                    \
    goto epilogue;                                                     \
  }

#define DYNAMIC_EXPECT_THROW(v, t, n)                                  \
  if (v.type() != t) {                                                 \
    throw std::runtime_error(                                          \
        "Expected dynamic" #t " (" n ")");                             \
  }

namespace facebook { namespace memcache { namespace mcrouter {

namespace {

const int MAX_HASH_SALT_LEN = 255;
const string POOL_LOCALITY_REGION = "region";
const string POOL_LOCALITY_CLUSTER = "cluster";

mc_transport_t parse_transport(const dynamic& jobj,
                               mc_transport_t tdefault) {
  auto it = jobj.find("transport");
  if (it != jobj.items().end()) {
    dynamic jtransport = it->second;
    DYNAMIC_EXPECT(jtransport, dynamic::Type::STRING, "transport");

    auto jstr = jtransport.asString();
    const char* str = jstr.c_str();
    if (strcasecmp("TCP", str) == 0) {
      return mc_stream;
    } else {
      LOG(ERROR) << "Invalid IP protocol - expected TCP";
      goto epilogue;
    }
  }
  return tdefault;

epilogue:
  return mc_unknown_transport;
}

mc_protocol_t parse_protocol(const dynamic& jobj,
                             mc_protocol_t pdefault) {
  auto it = jobj.find("protocol");
  if (it != jobj.items().end()) {
    dynamic jmcp = it->second;
    DYNAMIC_EXPECT(jmcp, dynamic::Type::STRING, "protocol");

    auto jstr = jmcp.asString();
    const char* str = jstr.c_str();
    if (strcasecmp("ascii", str) == 0) {
      return mc_ascii_protocol;
    } else if (strcasecmp("umbrella", str) == 0) {
      return mc_umbrella_protocol;
    } else {
      LOG(ERROR) << "Expected a memcache protocol";
      goto epilogue;
    }
  }
  return pdefault;

epilogue:
  return mc_unknown_protocol;
}

bool isQosValid(uint64_t qos) {
  return (qos <= 4);
}
bool isQosValid(folly::dynamic& jQos) {
  DYNAMIC_EXPECT(jQos, dynamic::Type::INT64, "qos");
  {
    uint64_t qos = (uint64_t) jQos.asInt();
    return isQosValid(qos);
  }

epilogue:
  return false;
}

} // anonymous namespace

PoolFactory::PoolFactory(const folly::dynamic& config,
                         ConfigApi* configApi,
                         const McrouterOptions& mcOpts)
  : configApi_(configApi) {

  if (mcOpts.regional_pools_timeout_ms == 0) {
    opts_.regional_pools_timeout = to<timeval_t>(mcOpts.server_timeout_ms);
  } else {
    opts_.regional_pools_timeout =
      to<timeval_t>(mcOpts.regional_pools_timeout_ms);
  }

  if (mcOpts.cluster_pools_timeout_ms == 0) {
    opts_.cluster_pools_timeout = to<timeval_t>(mcOpts.server_timeout_ms);
  } else {
    opts_.cluster_pools_timeout =
      to<timeval_t>(mcOpts.cluster_pools_timeout_ms);
  }
  opts_.rxpriority = get_event_priority(mcOpts, CLIENT_REPLY);
  opts_.txpriority = get_event_priority(mcOpts, CLIENT_REQUEST);

  opts_.region = mcOpts.default_route.getRegion().str();
  opts_.cluster = mcOpts.default_route.getCluster().str();
  opts_.cross_region_timeout_ms = mcOpts.cross_region_timeout_ms;
  opts_.cross_cluster_timeout_ms = mcOpts.cross_cluster_timeout_ms;
  opts_.within_cluster_timeout_ms = mcOpts.within_cluster_timeout_ms;

  if (isQosValid(mcOpts.default_qos_class)) {
    opts_.default_qos_class = mcOpts.default_qos_class;
  } else {
    opts_.default_qos_class = 0;
    logFailure(memcache::failure::Category::kInvalidConfig,
               "Invalid defaul-qos-class config");
  }

  // parse the clusters
  if (!parseClusters(config)) {
    throw std::runtime_error("parse_clusters failed");
  }

  // parse pools
  if (!parsePools(config, 0)) {
    throw std::runtime_error("parse_pools failed");
  }

  // parse the regional pools
  // the "delete_time" will be parsed from the json
  // object if it exists
  if (!parsePools(config, 1)) {
    throw std::runtime_error("parse_pools (regional) failed");
  }

  if (!parseMigratedPools(config, /*is_regional=*/true)) {
    throw std::runtime_error("parse_migrated_pools (regional) failed");
  }
}

const std::unordered_map<std::string, std::shared_ptr<ProxyGenericPool>>&
PoolFactory::pools() const {
  return pools_;
}

const std::unordered_map<std::string,
                           std::shared_ptr<const ProxyClientCommon>>&
PoolFactory::clients() const {
  return clients_;
}

string PoolFactory::genProxyClientKey(const AccessPoint& ap) {
    /* Once upon a time, ProxyClient's could be reused at will within
     * a configuration, and times were good.  Unfortunately, we needed to
     * have failover, and the one reasonable way to implement that was by
     * having ProxyClient's aware of which pool they were in.  This
     * change forced us to use ProxyClient's only once, which
     * completely broke sharing ProxyClient's within a single config
     * (but not from an old config into a new one -- we only have a brief
     * moment of failover vulnerability before we're up and running).
     * For that reason, we actually need to maintain N connections for a
     * particular endpoint if it's used N times.  To do this in a
     * semi-reasonable fashion, we key into the hosts dictionary in the
     * normal way (using the accesspoint hash) for the first one, and
     * then we begin appending -2, -3, -4 and so on for subsequent ones.
     * This has O(n^2) behavior, but that shouldn't be that big of a deal
     * (if you have a whole lot of duplicate connections, you're probably
     * doing something wrong).
     */

    auto keyBase = ap.toString();
    auto key = keyBase;
    unsigned int keyNum = 1;
    while (clients_.find(key) != clients_.end()) {
      key = folly::stringPrintf("%s-%u", keyBase.c_str(), ++keyNum);
    }
    return key;
}

int PoolFactory::addPoolToConfig(std::shared_ptr<ProxyGenericPool> pool) {
  auto pool_name = pool->getName();
  if (pools_.find(pool_name) != pools_.end()) {
    LOG(ERROR) << "bad config: duplicate pool: " << pool_name;
    return 0;
  }

  pools_.emplace(pool_name, std::move(pool));
  return 1;
}

int PoolFactory::parseClusters(const dynamic& json) {
  if (json.find("clusters") == json.items().end()) {
    // clusters is optional
    return 1;
  }
  dynamic jclusters = json["clusters"];;
  DYNAMIC_EXPECT(jclusters, dynamic::Type::OBJECT, "clusters");

  for (auto& jiter: jclusters.items()) {
    const dynamic jcluster = jiter.second;
    DYNAMIC_EXPECT(jcluster, dynamic::Type::OBJECT, "cluster");
    if (!parsePools(jcluster, 0)) {
      LOG(ERROR) << "Error while parsing pools";
      goto epilogue;
    }
    if (!parseMigratedPools(jcluster, /*is_regional=*/false)) {
      LOG(ERROR) << "Error parsing migrated pools";
      goto epilogue;
    }
  }

  return 1;
epilogue:
  return 0;
}

int PoolFactory::parseMigratedPools(const dynamic& json, bool is_regional) {
  int success = 0;
  dynamic jmigrated_pools = dynamic::object;

  auto it = json.find(is_regional ?
                      "migrated_regional_pools" : "migrated_pools");
  if (it == json.items().end()) {
    success = 1;
    goto epilogue;
  }

  jmigrated_pools = it->second;
  DYNAMIC_EXPECT(jmigrated_pools, dynamic::Type::OBJECT,
                 "jmigrated_pools");

  for (auto& jiter : jmigrated_pools.items()) {
    auto jstr = jiter.first.asString();
    const dynamic jpool = jiter.second;

    auto migrated_pool = make_shared<ProxyMigratedPool>(jstr.toStdString());

    it = jpool.find("from_pool");
    if (it == jpool.items().end()) {
      LOG(ERROR) << "from_pool missing in a migrated pool";
      goto epilogue;
    }
    DYNAMIC_EXPECT(it->second, dynamic::Type::STRING, "from pool");
    jstr = it->second.asString();
    auto from_generic = fetchPool(jstr);
    migrated_pool->from_pool = dynamic_cast<ProxyPool*>(from_generic.get());
    FBI_ASSERT(!from_generic || migrated_pool->from_pool);


    it = jpool.find("to_pool");
    if (it == jpool.items().end()) {
      LOG(ERROR) << "to_pool missing in a migrated pool";
      goto epilogue;
    }
    DYNAMIC_EXPECT(it->second, dynamic::Type::STRING, "to pool");
    jstr = it->second.asString();
    auto to_generic = fetchPool(jstr);
    migrated_pool->to_pool = dynamic_cast<ProxyPool*>(to_generic.get());
    FBI_ASSERT(!to_generic || migrated_pool->to_pool);

    it = jpool.find("migration_start_time_ts");
    if (it == jpool.items().end()) {
      LOG(ERROR) << "need to specify migration_start_time for migrated pool";
      goto epilogue;
    }
    DYNAMIC_EXPECT(it->second, dynamic::Type::INT64, "migration start time");
    migrated_pool->migration_start_ts = (uint64_t) it->second.asInt();

    it = jpool.find("migration_interval_sec");
    if (it == jpool.items().end()) {
      LOG(ERROR) << "need to specify migration_interval for migration pool";
      goto epilogue;
    }
    DYNAMIC_EXPECT(it->second, dynamic::Type::INT64, "migration interval");
    migrated_pool->migration_interval_sec = (uint64_t) it->second.asInt();

    it = jpool.find("warming_up");
    if (it == jpool.items().end()) {
      migrated_pool->warming_up = false;
    } else {
      DYNAMIC_EXPECT(it->second, dynamic::Type::BOOL, "warming_up");
      migrated_pool->warming_up = it->second.asBool();
    }

    it = jpool.find("warmup_exptime");
    if (it == jpool.items().end()) {
      migrated_pool->warmup_exptime = 0;
    } else {
      DYNAMIC_EXPECT(it->second, dynamic::Type::INT64, "warmup_exptime");
      migrated_pool->warmup_exptime = it->second.asInt();
    }

    if (addPoolToConfig(migrated_pool) == 0) {
      goto epilogue;
    }
  }

  success = 1;
epilogue:
  return success;
}

int PoolFactory::parsePools(const dynamic& json, int is_regional) {
  dynamic jpools = dynamic::object;

  auto it = json.find(is_regional ? "regional_pools" : "pools");
  if (it == json.items().end()) {
    return 1;
  }

  jpools = it->second;
  if (!jpools.isObject()) {
    LOG(ERROR) << "pools should be object";
    return 0;
  }

  for (auto& jiter: jpools.items()) {
    const dynamic jpool = jiter.second;
    string jstr = jiter.first.asString().toStdString();
    auto pool = parsePool(jstr, jpool, jpools);

    if (!pool) {
      LOG(ERROR) << "Error while parsing " << jstr << " pool";
      return 0;
    }
  } // pools

  return 1;
}

std::shared_ptr<ProxyGenericPool>
PoolFactory::parsePool(const string& pool_name_str,
                       const dynamic& jpool,
                       const dynamic& jpools) {
  int success = 0;
  std::shared_ptr<ProxyPool> pool;
  AccessPoint ap;

  {
    timeval_t timeout;
    DYNAMIC_EXPECT(jpool, dynamic::Type::OBJECT, "pool");

    // pool_locality
    auto it = jpool.find("pool_locality");
    if (it != jpool.items().end()) {
      dynamic jpool_locality = it->second;
      DYNAMIC_EXPECT(jpool_locality, dynamic::Type::STRING, "pool_locality");
      string jstr = jpool_locality.asString().toStdString();
      if (jstr == POOL_LOCALITY_CLUSTER) {
        pool = make_shared<ProxyRegularPool>(pool_name_str);
        timeout = opts_.cluster_pools_timeout;
      } else if(jstr == POOL_LOCALITY_REGION) {
        pool = make_shared<ProxyRegionalPool>(pool_name_str);
        timeout = opts_.regional_pools_timeout;
      } else {
        LOG(ERROR) << jstr << " pool locality not supported. Found for pool " <<
                      pool_name_str;
        goto epilogue;
      }
    } else {
      pool = make_shared<ProxyRegularPool>(pool_name_str);
      timeout = opts_.cluster_pools_timeout;
    }

    // delete_time
    it = jpool.find("delete_time");
    if (it != jpool.items().end()) {
      dynamic jdelete_time = it->second;
      DYNAMIC_EXPECT(jdelete_time,  dynamic::Type::INT64, "delete_time");
      pool->delete_time = jdelete_time.asInt();
    } else {
      pool->delete_time = 0;
    }

    // region & cluster
    string region, cluster;
    it = jpool.find("region");
    if (it != jpool.items().end()) {
      dynamic jregion = it->second;
      DYNAMIC_EXPECT(jregion, dynamic::Type::STRING, "pool_region");
      region = jregion.asString().toStdString();
    }
    it = jpool.find("cluster");
    if (it != jpool.items().end()) {
      dynamic jcluster = it->second;
      DYNAMIC_EXPECT(jcluster, dynamic::Type::STRING, "pool_cluster");
      cluster = jcluster.asString().toStdString();
    }

    // load and set timeout for each pool
    it = jpool.find("server_timeout");
    if (it != jpool.items().end()) {
      dynamic jserver_timeout = it->second;
      DYNAMIC_EXPECT(jserver_timeout,  dynamic::Type::INT64, "server_timeout");
      timeout.tv_sec = jserver_timeout.asInt() / 1000;
      timeout.tv_usec =
        (jserver_timeout.asInt() % 1000) * 1000;
    }
    if (!region.empty() && !cluster.empty()) {
      if (region == opts_.region && cluster == opts_.cluster) {
        if (opts_.within_cluster_timeout_ms != 0) {
          timeout = to<timeval_t>(opts_.within_cluster_timeout_ms);
        }
      } else if (region == opts_.region) {
        if (opts_.cross_cluster_timeout_ms != 0) {
          timeout = to<timeval_t>(opts_.cross_cluster_timeout_ms);
        }
      } else {
        if (opts_.cross_region_timeout_ms != 0) {
          timeout = to<timeval_t>(opts_.cross_region_timeout_ms);
        }
      }
    }
    pool->timeout = timeout;

    // hash
    pool->hash = proxy_hash_ch3;
    it = jpool.find("hash");
    if (it != jpool.items().end()) {
      dynamic jhash = it->second;
      DYNAMIC_EXPECT(jhash, dynamic::Type::STRING, "hash");

      auto jstr = jhash.asString();
      const char* str = jstr.c_str();
      if (strcasecmp("ch2", str) == 0) {
        pool->hash = proxy_hash_ch2;
      } else if (strcasecmp("ch3", str) == 0) {
        pool->hash = proxy_hash_ch3;
      } else if (strcasecmp("crc32", str) == 0) {
        pool->hash = proxy_hash_crc32;
      } else if (strcasecmp("latest", str) == 0) {
        pool->hash = proxy_hash_latest;
      } else if (strcasecmp("const_shard", str) == 0) {
        pool->hash = proxy_hash_const_shard;
      } else if (strcasecmp("wch3", str) == 0) {
        pool->hash = proxy_hash_wch3;
      } else {
        LOG(ERROR) << "Expected a hash discipline";
        goto epilogue;
      }
    }

    // hash_salt
    it = jpool.find("hash_salt");
    if (it != jpool.items().end()) {
      dynamic jhash_salt = jpool["hash_salt"];
      DYNAMIC_EXPECT(jhash_salt, dynamic::Type::STRING, "hash_salt");

      auto str = jhash_salt.asString().toStdString();
      if (str.length() > MAX_HASH_SALT_LEN) {
        LOG(ERROR) << "hash_salt string too long";
        goto epilogue;
      }
      pool->hash_salt = str;
    }

    pool->transport = parse_transport(jpool, mc_stream);
    if (pool->transport == mc_unknown_transport) {
      goto epilogue;
    }

    pool->protocol = parse_protocol(jpool, mc_ascii_protocol);
    if (pool->protocol == mc_unknown_protocol) {
      goto epilogue;
    }

    int keep_routing_prefix;
    it = jpool.find("keep_routing_prefix");
    if (it == jpool.items().end()) {
      keep_routing_prefix = 0;
    } else {
      dynamic json_keep_routing_prefix = it->second;
      DYNAMIC_EXPECT(json_keep_routing_prefix, dynamic::Type::BOOL,
          "keep_routing_prefix");
      keep_routing_prefix = (int) json_keep_routing_prefix.asBool();
    }
    pool->keep_routing_prefix = keep_routing_prefix;

    pool->attach_default_routing_prefix = false;
    it = jpool.find("attach_default_routing_prefix");
    if (it != jpool.items().end()) {
      DYNAMIC_EXPECT(it->second, dynamic::Type::BOOL,
          "attach_default_routing_prefix");
      pool->attach_default_routing_prefix = it->second.getBool();
    }

    // devnull_asynclog
    it = jpool.find("devnull_asynclog");
    if (it != jpool.items().end()) {
      dynamic jdevnull_asynclog = it->second;
      DYNAMIC_EXPECT(jdevnull_asynclog, dynamic::Type::BOOL,
                     "devnull_asynclog");
      pool->devnull_asynclog = jdevnull_asynclog.asBool();
    } else {
      pool->devnull_asynclog = false;
    }

    // servers
    it = jpool.find("servers");
    dynamic jservers = dynamic::object;
    if (it == jpool.items().end()) {
      it = jpool.find("server_list_from_pool");
      if (it == jpool.items().end()) {
        LOG(ERROR) <<
          "CRITICAL: failed to find \"servers\" or \"server_list_from_pool\"";
        goto epilogue;
      }
      dynamic source_pool = it->second;
      DYNAMIC_EXPECT(source_pool, dynamic::Type::STRING,
                     "server_list_from_pool");
      string src_pool_name = source_pool.asString().toStdString();
      dynamic src_pool_json = dynamic::object;
      it = jpools.find(src_pool_name);
      if (it != jpools.items().end()) {
        src_pool_json = it->second;
      } else {
        std::string src_pool;
        if (!configApi_->get(ConfigType::Pool, src_pool_name, src_pool)) {
          LOG(ERROR) << "Failed to find source pool " << src_pool_name <<
                        " for pool " << pool_name_str;
          goto epilogue;
        }
        src_pool_json = folly::parseJson(src_pool);
      }

      DYNAMIC_EXPECT(src_pool_json, dynamic::Type::OBJECT, "source_pool");
      it = src_pool_json.find("servers");
      if (it == src_pool_json.items().end()) {
        LOG(ERROR) << "Failed to find servers list in source pool " <<
                      src_pool_name;
        goto epilogue;
      }
      jservers = it->second;
    } else {
      jservers = it->second;
    }
    DYNAMIC_EXPECT(jservers, dynamic::Type::ARRAY, "pool_servers");
    size_t nclients = jservers.size();
    pool->clients.reserve(nclients);

    uint64_t qosDefault = 0;
    auto jPoolQos = jpool.getDefault("qos", opts_.default_qos_class);
    if (isQosValid(jPoolQos)) {
      qosDefault = jPoolQos.asInt();
    } else {
      qosDefault = opts_.default_qos_class;
      logFailure(memcache::failure::Category::kInvalidConfig,
                 "Invalid qos config for pool {}", pool_name_str);
    }

    auto jPoolUseSsl = jpool.getDefault("use_ssl", false);
    DYNAMIC_EXPECT(jPoolUseSsl, dynamic::Type::BOOL, "use_ssl");
    bool useSslDefault = jPoolUseSsl.asBool();

    for (size_t i = 0; i < nclients; i++) {
      const dynamic jserver = jservers[i];
      bool serverUseSsl = useSslDefault;
      uint64_t serverQos = qosDefault;
      if (jserver.isString()) {
        auto jhostnameStr = jserver.asString();

        // we support both host:port and host:port:protocol
        if (!AccessPoint::create(jhostnameStr, pool->protocol, ap)) {
          goto epilogue;
        }
      } else if (jserver.isObject()) {
        dynamic jhostname = jserver["hostname"];
        DYNAMIC_EXPECT(jhostname, dynamic::Type::STRING, "hostname");

        auto jServerQos = jserver.getDefault("qos", qosDefault);
        if (isQosValid(jServerQos)) {
          serverQos = jServerQos.asInt();
        } else {
          serverQos = qosDefault;
          logFailure(memcache::failure::Category::kInvalidConfig,
                     "Invalid qos config for host {} in pool {}",
                     jhostname.asString(), pool_name_str);
        }

        auto jServerUseSsl = jserver.getDefault("use_ssl", jPoolUseSsl);
        DYNAMIC_EXPECT(jServerUseSsl, dynamic::Type::BOOL, "use_ssl");
        serverUseSsl = jServerUseSsl.asBool();

        auto jhostnameStr = jhostname.asString();
        if (parse_transport(jserver, pool->transport) == mc_unknown_transport ||
            !AccessPoint::create(jhostnameStr,
                                 parse_protocol(jserver, pool->protocol),
                                 ap)) {
          goto epilogue;
        }

        if (ap.getProtocol() == mc_unknown_protocol) {
          goto epilogue;
        }
      } else {
          LOG(ERROR) << "Expected json string or object (server)";
          goto epilogue;
      }

      auto proxy_client_key = genProxyClientKey(ap);

      auto client = make_shared<ProxyClientCommon>(clients_.size(),
        pool->timeout,
        ap,
        pool->keep_routing_prefix,
        pool->attach_default_routing_prefix,
        pool->devnull_asynclog,
        pool.get(), proxy_client_key,
        opts_.rxpriority,
        opts_.txpriority,
        i,
        serverUseSsl,
        serverQos);

      FBI_ASSERT(proxy_client_key == client->proxy_client_key);

      clients_.emplace(client->proxy_client_key, client);

      pool->clients.push_back(std::move(client));
    } // servers

    // shard splits
    it = jpool.find("shard_splits");
    if (it != jpool.items().end()) {
      pool->shardSplitter = folly::make_unique<ShardSplitter>(it->second);
    }

    // Hash weights
    it = jpool.find("hash_weights");
    if (it != jpool.items().end()) {
      dynamic jhash_weights = jpool["hash_weights"];
      DYNAMIC_EXPECT(jhash_weights, dynamic::Type::ARRAY, "hash_weights");
      auto n = pool->clients.size();
      std::vector<double> hash_weights;
      for (auto& entry : jhash_weights) {
        double weight = entry.asDouble();
        if (weight < 0.0 || weight > 1.0) {
          LOG(ERROR) << "hash weight value " << weight <<
                        " expected to be >= 0.0 and <= 1.0";
          goto epilogue;
        }
        hash_weights.push_back(weight);
      }
      if (hash_weights.size() < n) {
        LOG(ERROR) << "CONFIG IS BROKEN!!! Hash weight vector size ("
                   << hash_weights.size()
                   << ") is smaller than number of servers (" << n
                   << "). Missing weights are set to 0.5";
        hash_weights.resize(n, 0.5);
      } else if (hash_weights.size() > n) {
        hash_weights.resize(n);
      }
      assert(hash_weights.size() == n);
      pool->wch3_func = folly::make_unique<WeightedCh3HashFunc>(
                                            std::move(hash_weights));

      /* For backwards compatibility, ch3 with weights = wch3 */
      if (pool->hash == proxy_hash_ch3) {
        pool->hash = proxy_hash_wch3;
      }
    }

    // Rate limiting
    it = jpool.find("rates");
    if (it != jpool.items().end()) {
      dynamic rates = jpool["rates"];
      DYNAMIC_EXPECT(rates, dynamic::Type::OBJECT, "rates");
      pool->rate_limiter = folly::make_unique<RateLimiter>(rates);
    }

    if (!addPoolToConfig(pool)) {
      goto epilogue;
    }
    success = 1;
  }

epilogue:
  return success ? pool : nullptr;
}

#undef DYNAMIC_EXPECT

std::shared_ptr<ProxyGenericPool>
PoolFactory::fetchPool(folly::StringPiece poolName) {
  auto nameStr = poolName.str();
  auto pool = facebook::memcache::tryGet(pools_, nameStr);
  if (pool) {
    return pool;
  }

  std::string jsonStr;
  if (!configApi_->get(ConfigType::Pool, nameStr, jsonStr)) {
    return nullptr;
  }
  folly::dynamic json = nullptr;
  try {
    json = folly::parseJson(jsonStr);
  } catch (const std::exception& e) {
    LOG(ERROR) << "Can not parse pool " << nameStr << ": " << e.what();
    return nullptr;
  }
  if (parsePool(nameStr, json, {}) == 0) {
    LOG(ERROR) << "Couldn't parse " << nameStr << " pool";
    return nullptr;
  }
  return facebook::memcache::tryGet(pools_, nameStr);
}

}}} // facebook::memcache::mcrouter
