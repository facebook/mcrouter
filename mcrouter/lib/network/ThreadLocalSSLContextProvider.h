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

#include <folly/Range.h>

namespace folly {
class SSLContext;
}  // namespace

namespace facebook { namespace memcache {

/**
 * Manages sets of certificates on per thread basis.
 * Each set will be loaded only once per thread and will be reloaded if it's
 * older than 5 minutes.
 */
std::shared_ptr<folly::SSLContext> getSSLContext(
  folly::StringPiece pemCertPath,
  folly::StringPiece pemKeyPath,
  folly::StringPiece pemCaPath);

}}  // facebook::memcache
