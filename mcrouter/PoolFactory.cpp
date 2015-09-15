/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "PoolFactory.h"

#include <folly/json.h>

#include "mcrouter/ClientPool.h"
#include "mcrouter/ConfigApi.h"
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/McrouterLogFailure.h"
#include "mcrouter/options.h"
#include "mcrouter/ProxyClientCommon.h"

namespace facebook { namespace memcache { namespace mcrouter {

namespace {

bool isQosClassValid(uint64_t qos) {
  return qos <= 4;
}
bool isQosPathValid(uint64_t qos) {
  return qos <= 3;
}

mc_protocol_t parseProtocol(const folly::dynamic& obj, mc_protocol_t def) {
  if (auto jprotocol = obj.get_ptr("protocol")) {
    checkLogic(jprotocol->isString(),
               "Protocol: expected string, {} found",
               jprotocol->typeName());

    auto str = jprotocol->stringPiece();
    if (equalStr("ascii", str, folly::AsciiCaseInsensitive())) {
      return mc_ascii_protocol;
    } else if (equalStr("umbrella", str, folly::AsciiCaseInsensitive())) {
      return mc_umbrella_protocol;
    }
    checkLogic(false, "Unknown protocol '{}'", str);
  }
  return def;
}

}  // anonymous namespace

PoolFactory::PoolFactory(const folly::dynamic& config,
                         ConfigApi& configApi,
                         const McrouterOptions& opts)
  : configApi_(configApi),
    opts_(opts) {

  checkLogic(config.isObject(), "config is not an object");
  if (auto jpools = config.get_ptr("pools")) {
    checkLogic(jpools->isObject(), "config: 'pools' is not an object");

    for (const auto& it : jpools->items()) {
      parsePool(it.first.stringPiece().str(), it.second);
    }
  }
}

void PoolFactory::parseQos(std::string parentName, const folly::dynamic& jQos,
                           uint64_t& qosClass, uint64_t& qosPath) {
  if (!jQos.isObject()) {
    MC_LOG_FAILURE(opts_, memcache::failure::Category::kInvalidConfig,
                   "{}: qos must be an object.", parentName);
    return;
  }

  uint64_t prevClass = qosClass;
  if (auto jClass = jQos.get_ptr("class")) {
    if (jClass->isInt() && isQosClassValid(jClass->getInt())) {
      qosClass = jClass->getInt();
    } else {
      MC_LOG_FAILURE(opts_, memcache::failure::Category::kInvalidConfig,
                     "{}: qos.class must be an integer in the range [0, 4]",
                     parentName);
    }
  }
  if (auto jPath = jQos.get_ptr("path")) {
    if (jPath->isInt() && isQosPathValid(jPath->getInt())) {
      qosPath = jPath->getInt();
    } else {
      MC_LOG_FAILURE(opts_, memcache::failure::Category::kInvalidConfig,
                     "{}: qos.path must be an integer in the range [0, 3]",
                     parentName);
      qosClass = prevClass;
    }
  }
}

std::shared_ptr<ClientPool>
PoolFactory::parsePool(const folly::dynamic& json) {
  checkLogic(json.isString() || json.isObject(),
             "Pool should be a string (name of pool) or an object");
  if (json.isString()) {
    return parsePool(json.stringPiece().str(), json);
  } else {
    auto name = json.get_ptr("name");
    checkLogic(name && name->isString(), "Pool should have string 'name'");
    return parsePool(name->stringPiece().str(), json);
  }
}

std::shared_ptr<ClientPool>
PoolFactory::parsePool(const std::string& name, const folly::dynamic& json) {
  auto seenPoolIt = pools_.find(name);
  if (seenPoolIt != pools_.end()) {
    return seenPoolIt->second;
  }

  if (json.isString()) {
    // get the pool from ConfigApi
    std::string jsonStr;
    checkLogic(configApi_.get(ConfigType::Pool, name, jsonStr),
               "Can not read pool: {}", name);
    return parsePool(name, parseJsonString(jsonStr));
  } else {
    // one day we may add inheriting from local pool
    if (auto jinherit = json.get_ptr("inherit")) {
      checkLogic(jinherit->isString(),
                 "Pool {}: inherit is not a string", name);
      auto path = jinherit->stringPiece().str();
      std::string jsonStr;
      checkLogic(configApi_.get(ConfigType::Pool, path, jsonStr),
                 "Can not read pool from: {}", path);
      auto newJson = parseJsonString(jsonStr);
      for (auto& it : json.items()) {
        newJson.insert(it.first, it.second);
      }
      newJson.erase("inherit");
      return parsePool(name, newJson);
    }
  }

  // pool_locality
  std::chrono::milliseconds timeout{opts_.server_timeout_ms};
  if (auto jlocality = json.get_ptr("pool_locality")) {
    if (!jlocality->isString()) {
      MC_LOG_FAILURE(opts_, memcache::failure::Category::kInvalidConfig,
                     "Pool {}: pool_locality is not a string", name);
    } else {
      auto str = jlocality->stringPiece();
      if (str == "cluster") {
        if (opts_.cluster_pools_timeout_ms != 0) {
          timeout = std::chrono::milliseconds(opts_.cluster_pools_timeout_ms);
        }
      } else if (str == "region") {
        if (opts_.regional_pools_timeout_ms != 0) {
          timeout = std::chrono::milliseconds(opts_.regional_pools_timeout_ms);
        }
      } else {
        MC_LOG_FAILURE(opts_, memcache::failure::Category::kInvalidConfig,
                       "Pool {}: '{}' pool locality is not supported",
                       name, str);
      }
    }
  }

  // region & cluster
  std::string region, cluster;
  if (auto jregion = json.get_ptr("region")) {
    if (!jregion->isString()) {
      MC_LOG_FAILURE(opts_, memcache::failure::Category::kInvalidConfig,
                     "Pool {}: pool_region is not a string", name);
    } else {
      region = jregion->stringPiece().str();
    }
  }
  if (auto jcluster = json.get_ptr("cluster")) {
    if (!jcluster->isString()) {
      MC_LOG_FAILURE(opts_, memcache::failure::Category::kInvalidConfig,
                     "Pool {}: pool_cluster is not a string", name);
    } else {
      cluster = jcluster->stringPiece().str();
    }
  }

  if (auto jtimeout = json.get_ptr("server_timeout")) {
    if (!jtimeout->isInt()) {
      MC_LOG_FAILURE(opts_, memcache::failure::Category::kInvalidConfig,
                     "Pool {}: server_timeout is not an int", name);
    } else {
      timeout = std::chrono::milliseconds(jtimeout->getInt());
    }
  }

  if (!region.empty() && !cluster.empty()) {
    auto& route = opts_.default_route;
    if (region == route.getRegion() && cluster == route.getCluster()) {
      if (opts_.within_cluster_timeout_ms != 0) {
        timeout = std::chrono::milliseconds(opts_.within_cluster_timeout_ms);
      }
    } else if (region == route.getRegion()) {
      if (opts_.cross_cluster_timeout_ms != 0) {
        timeout = std::chrono::milliseconds(opts_.cross_cluster_timeout_ms);
      }
    } else {
      if (opts_.cross_region_timeout_ms != 0) {
        timeout = std::chrono::milliseconds(opts_.cross_region_timeout_ms);
      }
    }
  }

  auto protocol = parseProtocol(json, mc_ascii_protocol);

  bool keep_routing_prefix = false;
  if (auto jkeep_routing_prefix = json.get_ptr("keep_routing_prefix")) {
    checkLogic(jkeep_routing_prefix->isBool(),
               "Pool {}: keep_routing_prefix is not a bool");
    keep_routing_prefix = jkeep_routing_prefix->getBool();
  }

  uint64_t qosClass = opts_.default_qos_class;
  uint64_t qosPath = opts_.default_qos_path;
  if (auto jqos = json.get_ptr("qos")) {
    parseQos(folly::sformat("Pool {}", name), *jqos, qosClass, qosPath);
  }

  bool useSsl = false;
  if (auto juseSsl = json.get_ptr("use_ssl")) {
    checkLogic(juseSsl->isBool(), "Pool {}: use_ssl is not a bool", name);
    useSsl = juseSsl->getBool();
  }
  bool useTyped = false;
  if (auto juseTyped = json.get_ptr("use_typed")) {
    checkLogic(juseTyped->isBool(), "Pool {}: use_typed is not a bool", name);
    useTyped = juseTyped->getBool();
  }

  // servers
  auto jservers = json.get_ptr("servers");
  checkLogic(jservers, "Pool {}: servers not found", name);
  checkLogic(jservers->isArray(), "Pool {}: servers is not an array", name);
  auto clientPool = std::make_shared<ClientPool>(name);
  for (size_t i = 0; i < jservers->size(); ++i) {
    const auto& server = jservers->at(i);
    std::shared_ptr<AccessPoint> ap;
    bool serverUseSsl = useSsl;
    uint64_t serverQosClass = qosClass;
    uint64_t serverQosPath = qosPath;
    checkLogic(server.isString() || server.isObject(),
               "Pool {}: server #{} is not a string/object", name, i);
    if (server.isString()) {
      // we support both host:port and host:port:protocol
      ap = AccessPoint::create(server.stringPiece(), protocol);
      checkLogic(ap != nullptr,
                 "Pool {}: invalid server {}", name, server.stringPiece());
    } else { // object
      auto jhostname = server.get_ptr("hostname");
      checkLogic(jhostname,
                 "Pool {}: hostname not found for server #{}", name, i);
      checkLogic(jhostname->isString(),
                 "Pool {}: hostname is not a string for server #{}", name, i);

      if (auto jqos = server.get_ptr("qos")) {
        parseQos(folly::sformat("Pool {}, server #{}", name, i),
                 *jqos, qosClass, qosPath);
      }

      if (auto juseSsl = server.get_ptr("use_ssl")) {
        checkLogic(juseSsl->isBool(),
                   "Pool {}: use_ssl is not a bool for server #{}", name, i);
        serverUseSsl = juseSsl->getBool();
      }

      ap = AccessPoint::create(jhostname->stringPiece(),
                               parseProtocol(server, protocol));
      checkLogic(ap != nullptr, "Pool {}: invalid server #{}", name, i);
    }

    if (useTyped) {
      checkLogic(ap->getProtocol() == mc_umbrella_protocol,
                 "Typed requests only supported with Umbrella");
    }
    auto client = clientPool->emplaceClient(timeout,
                                            std::move(ap),
                                            keep_routing_prefix,
                                            serverQosClass,
                                            serverQosPath,
                                            serverUseSsl,
                                            useTyped);

    clients_.push_back(std::move(client));
  } // servers

  // weights
  if (auto jweights = json.get_ptr("weights")) {
    clientPool->setWeights(*jweights);
  }

  pools_.emplace(name, clientPool);
  return clientPool;
}

}}}  // facebook::memcache::mcrouter
