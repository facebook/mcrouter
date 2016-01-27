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

/**
 * Operation and ReplyType Specializations for McRequest/McReply.
 */

#include <string>
#include <type_traits>

#include "mcrouter/lib/mc/msg.h"
#include "mcrouter/lib/Operation.h"

namespace facebook { namespace memcache {

class McReply;

/**
 * For existing memcache operations, we use a template trick:
 * Each operation is McOperation<N> where N is one of the mc_op_* constants.
 */
template <int op>
struct McOperation {
  static const mc_op_t mc_op = (mc_op_t)op;
  static const char* const name;
};

template <int op>
const char* const McOperation<op>::name = mc_op_to_string((mc_op_t)op);

/**
 * TODO(jmswen) Soon we will use custom reply types for custom request types,
 * not simply McReply.
 */
template <typename Request>
struct ReplyType {
  typedef class McReply type;
};

}}
