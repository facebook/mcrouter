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

#include "mcrouter/lib/network/gen-cpp2/mc_caret_protocol_types.h"

namespace facebook { namespace memcache {

/**
 * Type traits on the raw Thrift-generated structures. Useful in conditionally
 * defining functions, e.g., instantiate one version of the function template
 * for Thrift structures that have `key`, another version for Thrift structures
 * without a `key` field.
 */
template <class M>
struct HasKey {
  static constexpr bool value = false;
};

template <>
struct HasKey<cpp2::McGetRequest> {
  static constexpr bool value = true;
};

template <class M>
struct HasExptime {
  static constexpr bool value = false;
};

template <class M>
struct HasValue {
  static constexpr bool value = false;
};

}} // facebook::memcache
