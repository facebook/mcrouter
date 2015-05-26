/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <string>

#include <folly/dynamic.h>

#include "mcrouter/lib/FailoverErrorsSettings.h"
#include "mcrouter/lib/OperationTraits.h"

namespace facebook { namespace memcache { namespace mcrouter {

struct FailoverWithExptimeSettings {
  struct OperationSettings {
    bool gets{true};
    bool updates{true};
    bool deletes{false};

    OperationSettings() = default;

    void override(const folly::dynamic& json);

    void disable();

    template <class Operation>
    bool shouldFailover(Operation,
                        typename GetLike<Operation>::Type = 0) const {
      return gets;
    }

    template <class Operation>
    bool shouldFailover(Operation,
                        typename UpdateLike<Operation>::Type = 0) const {
      return updates;
    }

    template <class Operation>
    bool shouldFailover(Operation,
                        typename DeleteLike<Operation>::Type = 0) const {
      return deletes;
    }

    template <class Operation>
    bool shouldFailover(Operation,
                        OtherThanT(Operation,
                                   GetLike<>,
                                   UpdateLike<>,
                                   DeleteLike<>) = 0) const {
      return false;
    }
  };

  FailoverErrorsSettings getFailoverErrors() const;

  OperationSettings tko;
  OperationSettings connectTimeout;
  OperationSettings dataTimeout;
  bool failoverTagging{false};

  FailoverWithExptimeSettings() = default;

  explicit FailoverWithExptimeSettings(const folly::dynamic& json);
};

}}}  // facebook::memcache::mcrouter
