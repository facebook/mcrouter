/*
 *  Copyright (c) 2014-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
// @nolint
#ifndef MC_OP
#define MC_OP(Operation)
#endif

MC_OP(McOperation<mc_op_quit>)
MC_OP(McOperation<mc_op_version>)
MC_OP(McOperation<mc_op_get>)
MC_OP(McOperation<mc_op_set>)
MC_OP(McOperation<mc_op_add>)
MC_OP(McOperation<mc_op_replace>)
MC_OP(McOperation<mc_op_append>)
MC_OP(McOperation<mc_op_prepend>)
MC_OP(McOperation<mc_op_cas>)
MC_OP(McOperation<mc_op_delete>)
MC_OP(McOperation<mc_op_incr>)
MC_OP(McOperation<mc_op_decr>)
MC_OP(McOperation<mc_op_flushall>)
MC_OP(McOperation<mc_op_flushre>)
MC_OP(McOperation<mc_op_stats>)
MC_OP(McOperation<mc_op_verbosity>)
MC_OP(McOperation<mc_op_lease_get>)
MC_OP(McOperation<mc_op_lease_set>)
MC_OP(McOperation<mc_op_shutdown>)
MC_OP(McOperation<mc_op_end>)
MC_OP(McOperation<mc_op_metaget>)
MC_OP(McOperation<mc_op_exec>)
MC_OP(McOperation<mc_op_gets>)
MC_OP(McOperation<mc_op_get_service_info>)
MC_OP(McOperation<mc_op_touch>)

#undef MC_OP

/**
 * List of mc_op_t operations that have Caret counterparts, e.g., mc_op_get
 * has the Caret counterpart pair McGetRequest/McGetReply.
 */
#ifndef THRIFT_OP
#define THRIFT_OP(Operation)
#endif

THRIFT_OP(McOperation<mc_op_quit>)
THRIFT_OP(McOperation<mc_op_version>)
THRIFT_OP(McOperation<mc_op_get>)
THRIFT_OP(McOperation<mc_op_set>)
THRIFT_OP(McOperation<mc_op_add>)
THRIFT_OP(McOperation<mc_op_replace>)
THRIFT_OP(McOperation<mc_op_append>)
THRIFT_OP(McOperation<mc_op_prepend>)
THRIFT_OP(McOperation<mc_op_cas>)
THRIFT_OP(McOperation<mc_op_delete>)
THRIFT_OP(McOperation<mc_op_incr>)
THRIFT_OP(McOperation<mc_op_decr>)
THRIFT_OP(McOperation<mc_op_flushall>)
THRIFT_OP(McOperation<mc_op_flushre>)
THRIFT_OP(McOperation<mc_op_stats>)
THRIFT_OP(McOperation<mc_op_lease_get>)
THRIFT_OP(McOperation<mc_op_lease_set>)
THRIFT_OP(McOperation<mc_op_shutdown>)
THRIFT_OP(McOperation<mc_op_metaget>)
THRIFT_OP(McOperation<mc_op_exec>)
THRIFT_OP(McOperation<mc_op_gets>)
THRIFT_OP(McOperation<mc_op_touch>)

#undef THRIFT_OP
