/*
 *  Copyright (c) 2014-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "ShardSplitter.h"

#include <folly/dynamic.h>

#include "mcrouter/lib/fbi/cpp/globals.h"
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/routes/ShardHashFunc.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

namespace {
constexpr size_t kMaxSplits = 26 * 26 + 1;
constexpr size_t kHostIdModulo = 16384;

size_t checkShardSplitSize(
    const folly::StringPiece shardId,
    const folly::dynamic& splitValue,
    folly::StringPiece name) {
  checkLogic(
      splitValue.isInt(),
      "ShardSplitter: {} is not an int for {}",
      name,
      shardId);
  auto split = splitValue.asInt();
  checkLogic(
      split > 0, "ShardSplitter: {} value is <= 0 for {}", name, shardId);
  if (static_cast<size_t>(split) > kMaxSplits) {
    LOG(ERROR) << "ShardSplitter: " << name << " value is greater than "
               << kMaxSplits << " for " << shardId;
    return kMaxSplits;
  }
  return static_cast<size_t>(split);
}
} // anonymous

size_t ShardSplitter::ShardSplitInfo::getSplitSizeForCurrentHost() const {
  if (migrating_) {
    auto now = std::chrono::system_clock::now();
    if (now < startTime_) {
      return oldSplitSize_;
    } else if (now > startTime_ + migrationPeriod_) {
      migrating_ = false;
      return newSplitSize_;
    } else {
      double point = std::chrono::duration_cast<std::chrono::duration<double>>(
                         now - startTime_)
                         .count() /
          migrationPeriod_.count();
      return globals::hostid() % kHostIdModulo /
                  static_cast<double>(kHostIdModulo) <
              point
          ? newSplitSize_
          : oldSplitSize_;
    }
  }
  return newSplitSize_;
}

ShardSplitter::ShardSplitter(const folly::dynamic& json) {
  checkLogic(json.isObject(), "ShardSplitter: config is not an object");

  auto now = std::chrono::system_clock::now();
  for (const auto& it : json.items()) {
    checkLogic(
        it.first.isString(),
        "ShardSplitter: expected string for key in shard split config, "
        "but got {}",
        it.first.typeName());
    folly::StringPiece shardId = it.first.getString();
    checkLogic(
        it.second.isInt() || it.second.isObject(),
        "ShardSplitter: split config for {} is not an int or object",
        it.first.asString());
    if (it.second.isInt()) {
      auto splitCnt = checkShardSplitSize(shardId, it.second, "shard_splits");
      if (splitCnt != 1) {
        shardSplits_.emplace(it.first.c_str(), ShardSplitInfo(splitCnt));
      }
    } else if (it.second.isObject()) {
      auto oldSplitJson = it.second.getDefault("old_split_size", 1);
      auto oldSplit =
          checkShardSplitSize(shardId, oldSplitJson, "old_split_size");
      auto newSplitJson = it.second.getDefault("new_split_size", 1);
      auto newSplit =
          checkShardSplitSize(shardId, newSplitJson, "new_split_size");
      auto startTimeJson = it.second.getDefault("split_start", 0);
      checkLogic(
          startTimeJson.isInt(),
          "ShardSplitter: split_start is not an int for {}",
          shardId);
      checkLogic(
          startTimeJson.asInt() >= 0,
          "ShardSplitter: split_start is negative for {}",
          shardId);
      std::chrono::system_clock::time_point startTime(
          std::chrono::seconds(startTimeJson.asInt()));
      auto migrationPeriodJson = it.second.getDefault("migration_period", 0);
      checkLogic(
          migrationPeriodJson.isInt(),
          "ShardSplitter: migration_period is not an int for {}",
          shardId);
      checkLogic(
          migrationPeriodJson.asInt() >= 0,
          "ShardSplitter: migration_period is negative for {}",
          shardId);
      std::chrono::duration<double> migrationPeriod(
          migrationPeriodJson.asInt());
      auto fanoutDeletesJson = it.second.getDefault("fanout_deletes", false);
      checkLogic(
          fanoutDeletesJson.isBool(),
          "ShardSplitter: fanout_deletes is not bool for {}",
          shardId);
      if (now > startTime + migrationPeriod || newSplit == oldSplit) {
        shardSplits_.emplace(
            shardId.str(),
            ShardSplitInfo(newSplit, fanoutDeletesJson.asBool()));
      } else {
        shardSplits_.emplace(
            shardId.str(),
            ShardSplitInfo(
                oldSplit,
                newSplit,
                startTime,
                migrationPeriod,
                fanoutDeletesJson.asBool()));
      }
    }
  }
}

const ShardSplitter::ShardSplitInfo* ShardSplitter::getShardSplit(
    folly::StringPiece routingKey,
    folly::StringPiece& shard) const {
  if (!getShardId(routingKey, shard)) {
    return nullptr;
  }

  auto splitIt = shardSplits_.find(shard);
  if (splitIt == shardSplits_.end()) {
    return nullptr;
  }
  return &splitIt->second;
}

} // mcrouter
} // memcache
} // facebook
