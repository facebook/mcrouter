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

#include "mcrouter/lib/mc/msg.h"
#include "mcrouter/lib/network/gen/Memcache.h"
#include "mcrouter/lib/network/TypedMsg.h"

namespace facebook { namespace memcache {

/* List of operations */
using RequestReplyPairs = List<
    Pair<TypedMsg<1, McGetRequest>, TypedMsg<2, McGetReply>>,
    Pair<TypedMsg<3, McSetRequest>, TypedMsg<4, McSetReply>>,
    Pair<TypedMsg<5, McDeleteRequest>, TypedMsg<6, McDeleteReply>>,
    Pair<TypedMsg<7, McLeaseGetRequest>,
         TypedMsg<8, McLeaseGetReply>>,
    Pair<TypedMsg<9, McLeaseSetRequest>,
         TypedMsg<10, McLeaseSetReply>>,
    Pair<TypedMsg<11, McAddRequest>, TypedMsg<12, McAddReply>>,
    Pair<TypedMsg<13, McReplaceRequest>,
         TypedMsg<14, McReplaceReply>>,
    Pair<TypedMsg<15, McGetsRequest>, TypedMsg<16, McGetsReply>>,
    Pair<TypedMsg<17, McCasRequest>, TypedMsg<18, McCasReply>>,
    Pair<TypedMsg<19, McIncrRequest>, TypedMsg<20, McIncrReply>>,
    Pair<TypedMsg<21, McDecrRequest>, TypedMsg<22, McDecrReply>>,
    Pair<TypedMsg<23, McMetagetRequest>,
         TypedMsg<24, McMetagetReply>>,
    Pair<TypedMsg<25, McVersionRequest>,
         TypedMsg<26, McVersionReply>>,
    Pair<TypedMsg<27, McAppendRequest>,
         TypedMsg<28, McAppendReply>>,
    Pair<TypedMsg<29, McPrependRequest>,
         TypedMsg<30, McPrependReply>>,
    Pair<TypedMsg<31, McTouchRequest>, TypedMsg<32, McTouchReply>>,
    Pair<TypedMsg<33, McShutdownRequest>,
         TypedMsg<34, McShutdownReply>>,
    Pair<TypedMsg<35, McQuitRequest>, TypedMsg<36, McQuitReply>>,
    Pair<TypedMsg<37, McStatsRequest>, TypedMsg<38, McStatsReply>>,
    Pair<TypedMsg<39, McExecRequest>, TypedMsg<40, McExecReply>>,
    Pair<TypedMsg<41, McFlushReRequest>,
         TypedMsg<42, McFlushReReply>>,
    Pair<TypedMsg<43, McFlushAllRequest>,
         TypedMsg<44, McFlushAllReply>>>;

using TRequestList = PairListFirstT<RequestReplyPairs>;
using TReplyList = PairListSecondT<RequestReplyPairs>;
using CarbonMessageList = ConcatenateListsT<TRequestList, TReplyList>;

struct ListChecker {
  StaticChecker<CarbonMessageList> checker;
};

using RequestOpMapping = List<KV<mc_op_get, McGetRequest>,
                              KV<mc_op_set, McSetRequest>,
                              KV<mc_op_delete, McDeleteRequest>,
                              KV<mc_op_lease_get, McLeaseGetRequest>,
                              KV<mc_op_lease_set, McLeaseSetRequest>,
                              KV<mc_op_add, McAddRequest>,
                              KV<mc_op_replace, McReplaceRequest>,
                              KV<mc_op_gets, McGetsRequest>,
                              KV<mc_op_cas, McCasRequest>,
                              KV<mc_op_incr, McIncrRequest>,
                              KV<mc_op_decr, McDecrRequest>,
                              KV<mc_op_metaget, McMetagetRequest>,
                              KV<mc_op_version, McVersionRequest>,
                              KV<mc_op_append, McAppendRequest>,
                              KV<mc_op_prepend, McPrependRequest>,
                              KV<mc_op_touch, McTouchRequest>,
                              KV<mc_op_shutdown, McShutdownRequest>,
                              KV<mc_op_quit, McQuitRequest>,
                              KV<mc_op_stats, McStatsRequest>,
                              KV<mc_op_exec, McExecRequest>,
                              KV<mc_op_flushre, McFlushReRequest>,
                              KV<mc_op_flushall, McFlushAllRequest>>;

using ReplyOpMapping = List<KV<mc_op_get, McGetReply>,
                            KV<mc_op_set, McSetReply>,
                            KV<mc_op_delete, McDeleteReply>,
                            KV<mc_op_lease_get, McLeaseGetReply>,
                            KV<mc_op_lease_set, McLeaseSetReply>,
                            KV<mc_op_add, McAddReply>,
                            KV<mc_op_replace, McReplaceReply>,
                            KV<mc_op_gets, McGetsReply>,
                            KV<mc_op_cas, McCasReply>,
                            KV<mc_op_incr, McIncrReply>,
                            KV<mc_op_decr, McDecrReply>,
                            KV<mc_op_metaget, McMetagetReply>,
                            KV<mc_op_version, McVersionReply>,
                            KV<mc_op_append, McAppendReply>,
                            KV<mc_op_prepend, McPrependReply>,
                            KV<mc_op_touch, McTouchReply>,
                            KV<mc_op_shutdown, McShutdownReply>,
                            KV<mc_op_quit, McQuitReply>,
                            KV<mc_op_stats, McStatsReply>,
                            KV<mc_op_exec, McExecReply>,
                            KV<mc_op_flushre, McFlushReReply>,
                            KV<mc_op_flushall, McFlushAllReply>>;

/**
 * Given a Request Type T and a Mapping of mc_op_t to Request Type,
 * gives, the mc_op_t corresponding to the Type T
 */
template <class T, class Mapping>
struct OpFromType;

template <class T>
struct OpFromType<T, List<>> {
  static constexpr mc_op_t value = mc_op_unknown;
};

template <class T, class KV1, class... KVs>
struct OpFromType<T, List<KV1, KVs...>> {
  static constexpr mc_op_t value = std::is_same<T, typename KV1::Value>::value
                                       ? static_cast<mc_op_t>(KV1::Key)
                                       : OpFromType<T, List<KVs...>>::value;
};

template <int op, class Mapping>
struct TypeFromOp;

template <int op>
struct TypeFromOp<op, List<>> {
    using type = void;
};

template <int op, class KV1, class... KVs>
struct TypeFromOp<op, List<KV1, KVs...>> {
    using type =
      typename std::conditional<op == KV1::Key,
                            typename KV1::Value,
                            typename TypeFromOp<op, List<KVs...>>::type>::type;
};

using CarbonRequestList = Values<TRequestList>;

template <class T>
using TRequestListContains = ListContains<CarbonRequestList, T>;

}} // facebook::memcache
