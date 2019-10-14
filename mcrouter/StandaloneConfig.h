/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <functional>
#include <unordered_map>

namespace facebook {
namespace memcache {

// forward declarations
class McrouterOptions;
class McServerSession;

namespace mcrouter {
// forward declarations
class McrouterStandaloneOptions;

void standalonePreInitFromCommandLineOpts(
    const std::unordered_map<std::string, std::string>& standaloneOptionsDict);

void standaloneInit(
    const McrouterOptions& opts,
    const McrouterStandaloneOptions& standaloneOpts);

void initStandaloneSSL();

void finalizeStandaloneOptions(McrouterStandaloneOptions& opts);

std::function<void(McServerSession&)> getConnectionAclChecker(
    const std::string& serviceIdentity,
    bool enforce);

} // namespace mcrouter
} // namespace memcache
} // namespace facebook
