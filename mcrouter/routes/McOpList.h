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

struct McOpList {
  template <int i>
  struct Item {};

  static constexpr int kLastItemId = 20;

  typedef Item<kLastItemId> LastItem;
};

template <> struct McOpList::Item<1> { typedef McOperation<mc_op_echo> op; };
template <> struct McOpList::Item<2> { typedef McOperation<mc_op_version> op; };
template <> struct McOpList::Item<3> { typedef McOperation<mc_op_get> op; };
template <> struct McOpList::Item<4> { typedef McOperation<mc_op_set> op; };
template <> struct McOpList::Item<5> { typedef McOperation<mc_op_add> op; };
template <> struct McOpList::Item<6> { typedef McOperation<mc_op_replace> op; };
template <> struct McOpList::Item<7> { typedef McOperation<mc_op_cas> op; };
template <> struct McOpList::Item<8> { typedef McOperation<mc_op_delete> op; };
template <> struct McOpList::Item<9> { typedef McOperation<mc_op_incr> op; };
template <> struct McOpList::Item<10> { typedef McOperation<mc_op_decr> op; };
template <> struct McOpList::Item<11> { typedef McOperation<mc_op_stats> op; };
template <> struct McOpList::Item<12> { typedef McOperation<mc_op_lease_get> op; };
template <> struct McOpList::Item<13> { typedef McOperation<mc_op_lease_set> op; };
template <> struct McOpList::Item<14> { typedef McOperation<mc_op_metaget> op; };
template <> struct McOpList::Item<15> { typedef McOperation<mc_op_gets> op; };
template <> struct McOpList::Item<16> { typedef McOperation<mc_op_get_service_info> op; };
template <> struct McOpList::Item<17> { typedef McOperation<mc_op_get_count> op; };
template <> struct McOpList::Item<18> { typedef McOperation<mc_op_bump_count> op; };
template <> struct McOpList::Item<19> { typedef McOperation<mc_op_get_unique_count> op; };
template <> struct McOpList::Item<20> { typedef McOperation<mc_op_bump_unique_count> op; };

}}
