/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <folly/Memory.h>

#include "mcrouter/config.h"
#include "mcrouter/flavor.h"
#include "mcrouter/McrouterInstance.h"
#include "mcrouter/McrouterLogger.h"
#include "mcrouter/options.h"
#include "mcrouter/proxy.h"
#include "mcrouter/routes/McExtraRouteHandleProvider.h"
#include "mcrouter/ShadowValidationData.h"
#include "mcrouter/standalone_options.h"

namespace facebook { namespace memcache { namespace mcrouter {

bool read_standalone_flavor(
    const std::string& flavor,
    std::unordered_map<std::string, std::string>& option_dict,
    std::unordered_map<std::string, std::string>& st_option_dict) {

  if (!readFlavor(flavor, st_option_dict, option_dict)) {
    LOG(ERROR) << "CRITICAL: Couldn't initialize from standalone flavor file "
               << flavor;
    return false;
  }
  return true;
}

std::unique_ptr<ConfigApi> createConfigApi(const McrouterOptions& opts) {
  return folly::make_unique<ConfigApi>(opts);
}

std::string performOptionSubstitution(std::string str) {
  return str;
}

std::unique_ptr<ExtraRouteHandleProviderIf> createExtraRouteHandleProvider() {
  return folly::make_unique<McExtraRouteHandleProvider>();
}

std::unique_ptr<McrouterLogger> createMcrouterLogger(McrouterInstance& router) {
  return folly::make_unique<McrouterLogger>(router);
}

void extraValidateOptions(const McrouterOptions& opts) {
  size_t numSources = 0;
  if (!opts.config_file.empty()) {
    ++numSources;
  }
  if (!opts.config_str.empty()) {
    ++numSources;
  }
  if (numSources == 0) {
    throw std::logic_error("No configuration source");
  } else if (numSources > 1) {
    throw std::logic_error("More than one configuration source");
  }
}

void applyTestMode(McrouterOptions& opts) {
  opts.enable_failure_logging = false;
  opts.stats_logging_interval = 0;
}

McrouterOptions defaultTestOptions() {
  auto opts = McrouterOptions();
  applyTestMode(opts);
  return opts;
}

std::vector<std::string> defaultTestCommandLineArgs() {
  return { "--disable-failure-logging", "--stats-logging-interval=0" };
}

void logTkoEvent(proxy_t& proxy, const TkoLog& tkoLog) { }

void logFailover(proxy_t& proxy, const char* opName,
                 size_t retry, size_t maxRetries,
                 const McRequest& req, const McReply& normal,
                 const McReply& failover) { }

void logShadowValidationError(proxy_t& proxy,
                              const ShadowValidationData& valData) {
  VLOG_EVERY_N(1,100)
      << "Mismatch between shadow and normal reply" << std::endl
      << "Key:" << valData.fullKey << std::endl
      << "Expected Result:"
      << mc_res_to_string(valData.normalResult) << std::endl
      << "Shadow Result:"
      << mc_res_to_string(valData.shadowResult) << std::endl;
}

void initFailureLogger() { }

void scheduleSingletonCleanup() { }

std::unordered_map<std::string, folly::dynamic> additionalConfigParams() {
  return std::unordered_map<std::string, folly::dynamic>();
}

}}}  // facebook::memcache::mcrouter
