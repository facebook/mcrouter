/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
/**
 * Cpplint enforces include guards, but this is not a typical include file
 * and we do want to include it multiple times.  This hack fools Cpplint
 * into believing we have a proper include guard.
 */
// %flint: pause
#if 0
// %flint: resume
#ifndef FAKE_INCLUDE_GUARD
#define FAKE_INCLUDE_GUARD
#endif
// %flint: pause
#endif

#ifndef MC_OP
#define MC_OP(Operation)
#endif

MC_OP(McOperation<mc_op_echo>)
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
MC_OP(McOperation<mc_op_get_count>)
MC_OP(McOperation<mc_op_bump_count>)
MC_OP(McOperation<mc_op_get_unique_count>)
MC_OP(McOperation<mc_op_bump_unique_count>)

#undef MC_OP

// %flint: resume
