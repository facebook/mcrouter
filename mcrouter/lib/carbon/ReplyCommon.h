/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <thrift/lib/cpp2/FieldRef.h>
#include "mcrouter/lib/carbon/MessageCommon.h"
#include "mcrouter/lib/carbon/gen-cpp2/carbon_result_types.h"

namespace facebook {
namespace memcache {
struct AccessPoint;
} // namespace memcache
} // namespace facebook

namespace carbon {

/**
 * A reply a failover route dropped in favor of a later destination's. Held in
 * memory only, so an in-process client can account for attempts it never
 * received. Times are wall-clock microseconds.
 */
struct FailoverAttempt {
  // host:port, or empty if the route that produced it set no destination.
  std::string destination;
  carbon::Result result{carbon::Result::UNKNOWN};
  // For a TKO, carries the reason the destination is marked down.
  std::string message;
  int64_t startTimeUs{0};
  int64_t endTimeUs{0};
  // Empty unless the server replied and attached one.
  std::string artilleryServerBlockReport;
};

/**
 * Tracing-only reply state, held behind one pointer so an untraced reply
 * carries a null shared_ptr rather than every field.
 */
struct ReplyTracingData {
  std::string artilleryServerBlockReport;
  std::vector<FailoverAttempt> failoverAttempts;
};

class ReplyCommon : public MessageCommon {
 public:
  const std::shared_ptr<const facebook::memcache::AccessPoint>& destination()
      const noexcept;

  void setDestination(
      std::shared_ptr<const facebook::memcache::AccessPoint> ap) noexcept;

  // Store region string in an optional field.
  void setRegion(const std::string& region);

  // Get region string if set
  const std::optional<std::string>& getRegion() const;

  /**
   * Gets the serialized ArtilleryServerBlockReport the server attached to this
   * reply, describing the server block for the client to log on its behalf.
   * Opaque here: the consumer deserializes it.
   *
   * Lives on the reply rather than on MessageCommon: only replies ever carry a
   * report, and MessageCommon is also the base of RequestCommon, where the
   * field would be dead weight on every request object.
   *
   * @return  The serialized report, or empty string if the reply carries none.
   */
  const std::string& artilleryServerBlockReport() const {
    static const std::string kEmpty;
    return tracing_ ? tracing_->artilleryServerBlockReport : kEmpty;
  }

  void setArtilleryServerBlockReport(std::string report) {
    updateTracing([&](ReplyTracingData& tracing) {
      tracing.artilleryServerBlockReport = std::move(report);
    });
  }

  /**
   * Replies failover dropped before this one was chosen, oldest first. Only
   * recorded for traced requests; empty otherwise.
   */
  const std::vector<FailoverAttempt>& failoverAttempts() const {
    static const std::vector<FailoverAttempt> kEmpty;
    return tracing_ ? tracing_->failoverAttempts : kEmpty;
  }

  void setFailoverAttempts(std::vector<FailoverAttempt> attempts) {
    updateTracing([&](ReplyTracingData& tracing) {
      tracing.failoverAttempts = std::move(attempts);
    });
  }

  std::vector<FailoverAttempt> releaseFailoverAttempts() {
    if (!tracing_ || tracing_->failoverAttempts.empty()) {
      return {};
    }
    // Copies the attempts once and rebuilds the rest without them, rather than
    // copying the whole state again as setFailoverAttempts({}) would.
    auto attempts = tracing_->failoverAttempts;
    if (tracing_->artilleryServerBlockReport.empty()) {
      tracing_.reset();
    } else {
      tracing_ = std::make_shared<const ReplyTracingData>(ReplyTracingData{
          .artilleryServerBlockReport = tracing_->artilleryServerBlockReport,
          .failoverAttempts = {}});
    }
    return attempts;
  }

 private:
  // Copies of a reply share tracing_, so setters replace it rather than write
  // through it. All-empty state is stored as null.
  template <class Update>
  void updateTracing(Update&& update) {
    ReplyTracingData next = tracing_ ? *tracing_ : ReplyTracingData{};
    update(next);
    if (next.artilleryServerBlockReport.empty() &&
        next.failoverAttempts.empty()) {
      tracing_.reset();
    } else {
      tracing_ = std::make_shared<const ReplyTracingData>(std::move(next));
    }
  }

  std::shared_ptr<const facebook::memcache::AccessPoint> destination_;
  std::optional<std::string> region_;
  std::shared_ptr<const ReplyTracingData> tracing_;
};

class ReplyCommonThrift : public ReplyCommon {
 public:
  explicit ReplyCommonThrift(carbon::Result result__ = carbon::Result::UNKNOWN);

  carbon::Result result() const;

  carbon::Result& result();

  auto result_ref() const& {
    return apache::thrift::detail::make_field_ref(
        this->result_, __isset.result);
  }

  auto result_ref() & {
    return apache::thrift::detail::make_field_ref(
        this->result_, __isset.result);
  }

 private:
  struct __isset {
    uint8_t result;
  } __isset = {};
  carbon::Result result_{carbon::Result::UNKNOWN};
};

} // namespace carbon
