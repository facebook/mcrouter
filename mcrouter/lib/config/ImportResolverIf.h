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

#include <folly/Range.h>

namespace facebook { namespace memcache {

/**
 * Used by ConfigPreprocessor. Implementation should load additional files
 * by path passed to @import macro.
 */
class ImportResolverIf {
 public:
  /**
   * @param path parameter passed to @import macro
   *
   * @return JSON with macros
   */
  virtual std::string import(folly::StringPiece path) = 0;

  virtual ~ImportResolverIf() {}
};

}} // facebook::memcache
