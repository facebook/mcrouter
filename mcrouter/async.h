/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <memory>

#include <folly/Range.h>

namespace facebook { namespace memcache {

struct AccessPoint;

namespace mcrouter {

struct proxy_t;

/**
 * Appends a 'delete' request entry to the asynclog.
 * This call blocks until the entry is written to the file
 * or an error occurs.
 */
void asynclog_delete(proxy_t* proxy,
                     const AccessPoint& ap,
                     folly::StringPiece key,
                     folly::StringPiece poolName);

}}} // facebook::memcache::mcrouter
