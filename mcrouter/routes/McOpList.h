/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#pragma once

#include "mcrouter/lib/fbi/cpp/TypeList.h"
#include "mcrouter/lib/mc/msg.h"
#include "mcrouter/lib/McOperation.h"

namespace facebook { namespace memcache {

typedef List<
  McOperation<mc_op_echo>,
  McOperation<mc_op_version>,
  McOperation<mc_op_get>,
  McOperation<mc_op_set>,
  McOperation<mc_op_add>,
  McOperation<mc_op_replace>,
  McOperation<mc_op_cas>,
  McOperation<mc_op_delete>,
  McOperation<mc_op_incr>,
  McOperation<mc_op_decr>,
  McOperation<mc_op_stats>,
  McOperation<mc_op_lease_get>,
  McOperation<mc_op_lease_set>,
  McOperation<mc_op_metaget>,
  McOperation<mc_op_gets>,
  McOperation<mc_op_get_service_info>,
  McOperation<mc_op_get_count>,
  McOperation<mc_op_bump_count>,
  McOperation<mc_op_get_unique_count>,
  McOperation<mc_op_bump_unique_count>>
McOperationList;

}}
