/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#ifndef UM_OP
#define UM_OP(mc, um)
#endif

#ifndef UM_RES
#define UM_RES(mc, um)
#endif

/**
 * For each op/res we need to know how to represent them on the wire.
 * Note: changing existing values will break backwards compatibility.
 */

UM_OP(mc_op_unknown,            0)
UM_OP(mc_op_echo,               1)
UM_OP(mc_op_quit,               2)
UM_OP(mc_op_version,            3)
UM_OP(mc_op_servererr,          4)
UM_OP(mc_op_get,                5)
UM_OP(mc_op_set,                6)
UM_OP(mc_op_add,                7)
UM_OP(mc_op_replace,            8)
UM_OP(mc_op_append,             9)
UM_OP(mc_op_prepend,           10)
UM_OP(mc_op_cas,               11)
UM_OP(mc_op_delete,            12)
UM_OP(mc_nops,                 13)
UM_OP(mc_op_incr,              14)
UM_OP(mc_op_decr,              15)
UM_OP(mc_op_flushall,          16)
UM_OP(mc_op_flushre,           17)
UM_OP(mc_op_stats,             18)
UM_OP(mc_op_verbosity,         19)
UM_OP(mc_op_lease_get,         20)
UM_OP(mc_op_lease_set,         21)
UM_OP(mc_op_shutdown,          22)
UM_OP(mc_op_end,               23)
UM_OP(mc_op_metaget,           24)
UM_OP(mc_op_exec,              25)
UM_OP(mc_op_gets,              26)
UM_OP(mc_op_get_service_info,  27)

UM_RES(mc_res_unknown,          0)
UM_RES(mc_res_deleted,          1)
UM_RES(mc_res_found,            2)
UM_RES(mc_res_notfound,         3)
UM_RES(mc_res_notstored,        4)
UM_RES(mc_res_stalestored,      5)
UM_RES(mc_res_ok,               6)
UM_RES(mc_res_stored,           7)
UM_RES(mc_res_exists,           8)
UM_RES(mc_res_ooo,              9)
UM_RES(mc_res_timeout,         10)
UM_RES(mc_res_connect_timeout, 11)
UM_RES(mc_res_connect_error,   12)
UM_RES(mc_res_busy,            13)
UM_RES(mc_res_tko,             14)
UM_RES(mc_res_bad_command,     15)
UM_RES(mc_res_bad_key,         16)
UM_RES(mc_res_bad_flags,       17)
UM_RES(mc_res_bad_exptime,     18)
UM_RES(mc_res_bad_lease_id,    19)
UM_RES(mc_res_bad_value,       20)
UM_RES(mc_res_aborted,         21)
UM_RES(mc_res_client_error,    22)
UM_RES(mc_res_local_error,     23)
UM_RES(mc_res_remote_error,    24)
UM_RES(mc_res_waiting,         25)
UM_RES(mc_res_bad_cas_id,      26)
UM_RES(mc_res_try_again,       27)
UM_RES(mc_res_foundstale,      28)
UM_RES(mc_res_notfoundhot,     29)
UM_RES(mc_res_shutdown,        30)

#undef UM_OP
#undef UM_RES
