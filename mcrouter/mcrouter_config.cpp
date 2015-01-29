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

#include "mcrouter/_router.h"
#include "mcrouter/config.h"
#include "mcrouter/flavor.h"
#include "mcrouter/McrouterLogger.h"
#include "mcrouter/options.h"
#include "mcrouter/proxy.h"
#include "mcrouter/routes/McExtraRouteHandleProvider.h"

namespace facebook { namespace memcache { namespace mcrouter {

int router_configure_from_string(mcrouter_t* router, folly::StringPiece input) {
  return router_configure(router, input);
}

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

bool standaloneInit(const McrouterOptions& opts) {
  int numSources = (opts.config_file.empty() ? 0 : 1) +
    (opts.config_str.empty() ? 0 : 1);
  if (numSources == 0) {
    LOG(ERROR) << "no configuration source";
    return false;
  } else if (numSources > 1) {
    LOG(ERROR) << "ambiguous configuration options";
    return false;
  }
  return true;
}

std::unique_ptr<ExtraRouteHandleProviderIf> createExtraRouteHandleProvider() {
  return folly::make_unique<McExtraRouteHandleProvider>();
}

std::unique_ptr<McrouterLogger> createMcrouterLogger(mcrouter_t* router) {
  return folly::make_unique<McrouterLogger>(router);
}

McrouterOptions defaultTestOptions() {
  return McrouterOptions();
}

std::vector<std::string> defaultTestCommandLineArgs() {
  return {};
}

void logTkoEvent(proxy_t* proxy, const TkoLog& tkoLog) { }

void initFailureLogger() { }

void scheduleSingletonCleanup() { }

}}}  // facebook::memcache::mcrouter
