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
#include "mcrouter/lib/network/gen-cpp2/mc_caret_protocol_types.h"
#include "mcrouter/lib/network/TypedMsg.h"

namespace facebook { namespace memcache {

/* List of operations */
using RequestReplyPairs = List<
    Pair<TypedMsg<1, cpp2::McGetRequest>, TypedMsg<2, cpp2::McGetReply>>,
    Pair<TypedMsg<3, cpp2::McSetRequest>, TypedMsg<4, cpp2::McSetReply>>,
    Pair<TypedMsg<5, cpp2::McDeleteRequest>, TypedMsg<6, cpp2::McDeleteReply>>,
    Pair<TypedMsg<7, cpp2::McLeaseGetRequest>,
         TypedMsg<8, cpp2::McLeaseGetReply>>,
    Pair<TypedMsg<9, cpp2::McLeaseSetRequest>,
         TypedMsg<10, cpp2::McLeaseSetReply>>,
    Pair<TypedMsg<11, cpp2::McAddRequest>, TypedMsg<12, cpp2::McAddReply>>,
    Pair<TypedMsg<13, cpp2::McReplaceRequest>,
         TypedMsg<14, cpp2::McReplaceReply>>,
    Pair<TypedMsg<15, cpp2::McGetsRequest>, TypedMsg<16, cpp2::McGetsReply>>,
    Pair<TypedMsg<17, cpp2::McCasRequest>, TypedMsg<18, cpp2::McCasReply>>,
    Pair<TypedMsg<19, cpp2::McIncrRequest>, TypedMsg<20, cpp2::McIncrReply>>,
    Pair<TypedMsg<21, cpp2::McDecrRequest>, TypedMsg<22, cpp2::McDecrReply>>,
    Pair<TypedMsg<23, cpp2::McMetagetRequest>,
         TypedMsg<24, cpp2::McMetagetReply>>,
    Pair<TypedMsg<25, cpp2::McVersionRequest>,
         TypedMsg<26, cpp2::McVersionReply>>,
    Pair<TypedMsg<27, cpp2::McAppendRequest>,
         TypedMsg<28, cpp2::McAppendReply>>,
    Pair<TypedMsg<29, cpp2::McPrependRequest>,
         TypedMsg<30, cpp2::McPrependReply>>,
    Pair<TypedMsg<31, cpp2::McTouchRequest>, TypedMsg<32, cpp2::McTouchReply>>,
    Pair<TypedMsg<33, cpp2::McShutdownRequest>,
         TypedMsg<34, cpp2::McShutdownReply>>,
    Pair<TypedMsg<35, cpp2::McQuitRequest>, TypedMsg<36, cpp2::McQuitReply>>,
    Pair<TypedMsg<37, cpp2::McStatsRequest>, TypedMsg<38, cpp2::McStatsReply>>,
    Pair<TypedMsg<39, cpp2::McExecRequest>, TypedMsg<40, cpp2::McExecReply>>,
    Pair<TypedMsg<41, cpp2::McFlushReRequest>,
         TypedMsg<42, cpp2::McFlushReReply>>,
    Pair<TypedMsg<43, cpp2::McFlushAllRequest>,
         TypedMsg<44, cpp2::McFlushAllReply>>>;

using TRequestList = PairListFirstT<RequestReplyPairs>;
using TReplyList = PairListSecondT<RequestReplyPairs>;
using ThriftMessageList = ConcatenateListsT<TRequestList, TReplyList>;

struct ListChecker {
  StaticChecker<ThriftMessageList> checker;
};

using RequestOpMapping = List<KV<mc_op_get, cpp2::McGetRequest>,
                              KV<mc_op_set, cpp2::McSetRequest>,
                              KV<mc_op_delete, cpp2::McDeleteRequest>,
                              KV<mc_op_lease_get, cpp2::McLeaseGetRequest>,
                              KV<mc_op_lease_set, cpp2::McLeaseSetRequest>,
                              KV<mc_op_add, cpp2::McAddRequest>,
                              KV<mc_op_replace, cpp2::McReplaceRequest>,
                              KV<mc_op_gets, cpp2::McGetsRequest>,
                              KV<mc_op_cas, cpp2::McCasRequest>,
                              KV<mc_op_incr, cpp2::McIncrRequest>,
                              KV<mc_op_decr, cpp2::McDecrRequest>,
                              KV<mc_op_metaget, cpp2::McMetagetRequest>,
                              KV<mc_op_version, cpp2::McVersionRequest>,
                              KV<mc_op_append, cpp2::McAppendRequest>,
                              KV<mc_op_prepend, cpp2::McPrependRequest>,
                              KV<mc_op_touch, cpp2::McTouchRequest>,
                              KV<mc_op_shutdown, cpp2::McShutdownRequest>,
                              KV<mc_op_quit, cpp2::McQuitRequest>,
                              KV<mc_op_stats, cpp2::McStatsRequest>,
                              KV<mc_op_exec, cpp2::McExecRequest>,
                              KV<mc_op_flushre, cpp2::McFlushReRequest>,
                              KV<mc_op_flushall, cpp2::McFlushAllRequest>>;

using ReplyOpMapping = List<KV<mc_op_get, cpp2::McGetReply>,
                            KV<mc_op_set, cpp2::McSetReply>,
                            KV<mc_op_delete, cpp2::McDeleteReply>,
                            KV<mc_op_lease_get, cpp2::McLeaseGetReply>,
                            KV<mc_op_lease_set, cpp2::McLeaseSetReply>,
                            KV<mc_op_add, cpp2::McAddReply>,
                            KV<mc_op_replace, cpp2::McReplaceReply>,
                            KV<mc_op_gets, cpp2::McGetsReply>,
                            KV<mc_op_cas, cpp2::McCasReply>,
                            KV<mc_op_incr, cpp2::McIncrReply>,
                            KV<mc_op_decr, cpp2::McDecrReply>,
                            KV<mc_op_metaget, cpp2::McMetagetReply>,
                            KV<mc_op_version, cpp2::McVersionReply>,
                            KV<mc_op_append, cpp2::McAppendReply>,
                            KV<mc_op_prepend, cpp2::McPrependReply>,
                            KV<mc_op_touch, cpp2::McTouchReply>,
                            KV<mc_op_shutdown, cpp2::McShutdownReply>,
                            KV<mc_op_quit, cpp2::McQuitReply>,
                            KV<mc_op_stats, cpp2::McStatsReply>,
                            KV<mc_op_exec, cpp2::McExecReply>,
                            KV<mc_op_flushre, cpp2::McFlushReReply>,
                            KV<mc_op_flushall, cpp2::McFlushAllReply>>;

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

template <class M>
class TypedThriftRequest;

using ThriftRequestList = MapT<TypedThriftRequest, Values<TRequestList>>;

template <class T>
using TRequestListContains = ListContains<ThriftRequestList, T>;

}} // facebook::memcache
