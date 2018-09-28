/*
 *  Copyright (c) 2014-present, Facebook, Inc.
 *
 *  This source code is licensed under the MIT license found in the LICENSE
 *  file in the root directory of this source tree.
 *
 */
#pragma once

#include <inttypes.h>
#include <stdint.h>

#include "mcrouter/lib/fbi/decls.h"

__BEGIN_DECLS

#define SERVER_ERROR_BUSY 307

/*
 * memcache request op
 */

/** memcache request types */
typedef enum mc_op_e {
  mc_op_unknown = 0,
  mc_op_echo,
  mc_op_quit,
  mc_op_version,
  mc_op_servererr, ///< not a real op
  mc_op_get,
  mc_op_set,
  mc_op_add,
  mc_op_replace,
  mc_op_append,
  mc_op_prepend,
  mc_op_cas,
  mc_op_delete,
  mc_op_incr,
  mc_op_decr,
  mc_op_flushall,
  mc_op_flushre,
  mc_op_stats,
  mc_op_verbosity,
  mc_op_lease_get,
  mc_op_lease_set,
  mc_op_shutdown,
  mc_op_end,
  mc_op_metaget,
  mc_op_exec,
  mc_op_gets,
  mc_op_get_service_info, ///< Queries various service state
  mc_op_touch,
  mc_nops // placeholder
} mc_op_t;

mc_op_t mc_op_from_string(const char* str);
static inline const char* mc_op_to_string(const mc_op_t op) {
  switch (op) {
    case mc_op_unknown:
      return "unknown";
    case mc_op_echo:
      return "echo";
    case mc_op_quit:
      return "quit";
    case mc_op_version:
      return "version";
    case mc_op_servererr:
      return "servererr";
    case mc_op_get:
      return "get";
    case mc_op_set:
      return "set";
    case mc_op_add:
      return "add";
    case mc_op_replace:
      return "replace";
    case mc_op_append:
      return "append";
    case mc_op_prepend:
      return "prepend";
    case mc_op_cas:
      return "cas";
    case mc_op_delete:
      return "delete";
    case mc_op_touch:
      return "touch";
    case mc_op_incr:
      return "incr";
    case mc_op_decr:
      return "decr";
    case mc_op_flushall:
      return "flushall";
    case mc_op_flushre:
      return "flushre";
    case mc_op_stats:
      return "stats";
    case mc_op_verbosity:
      return "verbosity";
    case mc_op_lease_get:
      return "lease-get";
    case mc_op_lease_set:
      return "lease-set";
    case mc_op_shutdown:
      return "shutdown";
    case mc_op_end:
      return "end";
    case mc_op_metaget:
      return "metaget";
    case mc_op_exec:
      return "exec";
    case mc_op_gets:
      return "gets";
    case mc_op_get_service_info:
      return "get-service-info";
    case mc_nops:
      return "unknown";
  };
  return "unknown";
}

/*
 * memcache reply result
 */

/** mcc response types. */
typedef enum mc_res_e {
  mc_res_unknown = 0,
  mc_res_deleted,
  mc_res_touched,
  mc_res_found,
  mc_res_foundstale, /* hot-miss w/ stale data */
  mc_res_notfound,
  mc_res_notfoundhot, /* hot-miss w/o stale data */
  mc_res_notstored,
  mc_res_stalestored,
  mc_res_ok,
  mc_res_stored,
  mc_res_exists,
  /* soft errors -- */
  mc_res_ooo, /* out of order (UDP) */
  mc_res_timeout, /* request timeout (connection was already established) */
  mc_res_connect_timeout,
  mc_res_connect_error,
  mc_res_busy, /* the request was refused for load shedding */
  mc_res_try_again, /* this request was refused, but we should keep sending
                       requests to this box */
  mc_res_shutdown, /* Server is shutting down. Use failover destination. */
  mc_res_tko, /* total knock out - the peer is down for the count */
  /* hard errors -- */
  mc_res_bad_command,
  mc_res_bad_key,
  mc_res_bad_flags,
  mc_res_bad_exptime,
  mc_res_bad_lease_id,
  mc_res_bad_cas_id,
  mc_res_bad_value,
  mc_res_aborted,
  mc_res_client_error,
  mc_res_local_error, /* an error internal to libmc */
  /* an error was reported by the remote server.

     TODO I think this can also be triggered by a communications problem or
     disconnect, but I think these should be separate errors. (fugalh) */
  mc_res_remote_error,
  /* in progress -- */
  mc_res_waiting,
  mc_nres // placeholder
} mc_res_t;

static inline const char* mc_res_to_string(const mc_res_t result) {
  switch (result) {
    case mc_res_unknown:
      return "mc_res_unknown";
    case mc_res_deleted:
      return "mc_res_deleted";
    case mc_res_touched:
      return "mc_res_touched";
    case mc_res_found:
      return "mc_res_found";
    case mc_res_foundstale:
      return "mc_res_foundstale";
    case mc_res_notfound:
      return "mc_res_notfound";
    case mc_res_notfoundhot:
      return "mc_res_notfoundhot";
    case mc_res_notstored:
      return "mc_res_notstored";
    case mc_res_stalestored:
      return "mc_res_stalestored";
    case mc_res_ok:
      return "mc_res_ok";
    case mc_res_stored:
      return "mc_res_stored";
    case mc_res_exists:
      return "mc_res_exists";
    /* soft errors -- */
    case mc_res_ooo:
      return "mc_res_ooo";
    case mc_res_timeout:
      return "mc_res_timeout";
    case mc_res_connect_timeout:
      return "mc_res_connect_timeout";
    case mc_res_connect_error:
      return "mc_res_connect_error";
    case mc_res_busy:
      return "mc_res_busy";
    case mc_res_try_again:
      return "mc_res_try_again";
    case mc_res_shutdown:
      return "mc_res_shutdown";
    case mc_res_tko:
      return "mc_res_tko";
    /* hard errors -- */
    case mc_res_bad_command:
      return "mc_res_bad_command";
    case mc_res_bad_key:
      return "mc_res_bad_key";
    case mc_res_bad_flags:
      return "mc_res_bad_flags";
    case mc_res_bad_exptime:
      return "mc_res_bad_exptime";
    case mc_res_bad_lease_id:
      return "mc_res_bad_lease_id";
    case mc_res_bad_cas_id:
      return "mc_res_bad_cas_id";
    case mc_res_bad_value:
      return "mc_res_bad_value";
    case mc_res_aborted:
      return "mc_res_aborted";
    case mc_res_client_error:
      return "mc_res_client_error";
    case mc_res_local_error:
      return "mc_res_local_error";
    case mc_res_remote_error:
      return "mc_res_remote_error";
    /* in progress -- */
    case mc_res_waiting:
      return "mc_res_waiting";
    case mc_nres:
      return "mc_res_unknown";
  }
  return "mc_res_unknown";
}

/* Flags are up to 48 bits in memcached server.
 * <= 32bits uses 32bits of storage, else 16 bits stored elsewhere.
 */
enum mc_msg_flags_t {
  MC_MSG_FLAG_PHP_SERIALIZED = 0x1,
  MC_MSG_FLAG_COMPRESSED = 0x2,
  MC_MSG_FLAG_FB_SERIALIZED = 0x4,
  MC_MSG_FLAG_FB_COMPACT_SERIALIZED = 0x8,
  MC_MSG_FLAG_ASCII_INT_SERIALIZED = 0x10,
  MC_MSG_FLAG_SIZE_SPLIT = 0x20,
  MC_MSG_FLAG_NZLIB_COMPRESSED = 0x800,
  MC_MSG_FLAG_QUICKLZ_COMPRESSED = 0x2000,
  MC_MSG_FLAG_SNAPPY_COMPRESSED = 0x4000,
  MC_MSG_FLAG_BIG_VALUE = 0x8000,
  MC_MSG_FLAG_NEGATIVE_CACHE = 0x10000,
  MC_MSG_FLAG_HOT_KEY = 0x20000,
  MC_MSG_FLAG_ZSTD_COMPRESSED = 0x40000,
  MC_MSG_FLAG_MANAGED_COMPRESSION_COMPRESSED = 0x80000,
  /* Bits reserved for application-specific extension flags: */
  MC_MSG_FLAG_USER_1 = 0x100000000LL,
  MC_MSG_FLAG_USER_2 = 0x200000000LL,
  MC_MSG_FLAG_USER_3 = 0x400000000LL,
  MC_MSG_FLAG_USER_4 = 0x800000000LL,
  MC_MSG_FLAG_USER_5 = 0x1000000000LL,
  MC_MSG_FLAG_USER_6 = 0x2000000000LL,
  MC_MSG_FLAG_USER_7 = 0x4000000000LL,
  MC_MSG_FLAG_USER_8 = 0x8000000000LL,
  MC_MSG_FLAG_USER_9 = 0x10000000000LL,
  MC_MSG_FLAG_USER_10 = 0x20000000000LL,
  MC_MSG_FLAG_USER_11 = 0x40000000000LL,
  MC_MSG_FLAG_USER_12 = 0x80000000000LL,
  MC_MSG_FLAG_USER_13 = 0x100000000000LL,
  MC_MSG_FLAG_USER_14 = 0x200000000000LL,
  MC_MSG_FLAG_USER_15 = 0x400000000000LL,
  MC_MSG_FLAG_USER_16 = 0x800000000000LL
};

static inline const char* mc_flag_to_string(const enum mc_msg_flags_t flag) {
  switch (flag) {
    case MC_MSG_FLAG_PHP_SERIALIZED:
      return "PHP_SERIALIZED";
    case MC_MSG_FLAG_COMPRESSED:
      return "COMPRESSED";
    case MC_MSG_FLAG_FB_SERIALIZED:
      return "FB_SERIALIZED";
    case MC_MSG_FLAG_FB_COMPACT_SERIALIZED:
      return "FB_COMPACT_SERIALIZED";
    case MC_MSG_FLAG_ASCII_INT_SERIALIZED:
      return "ASCII_INT_SERIALIZED";
    case MC_MSG_FLAG_SIZE_SPLIT:
      return "SIZE_SPLIT";
    case MC_MSG_FLAG_NZLIB_COMPRESSED:
      return "NZLIB_COMPRESSED";
    case MC_MSG_FLAG_QUICKLZ_COMPRESSED:
      return "QUICKLZ_COMPRESSED";
    case MC_MSG_FLAG_SNAPPY_COMPRESSED:
      return "SNAPPY_COMPRESSED";
    case MC_MSG_FLAG_BIG_VALUE:
      return "BIG_VALUE";
    case MC_MSG_FLAG_NEGATIVE_CACHE:
      return "NEGATIVE_CACHE";
    case MC_MSG_FLAG_ZSTD_COMPRESSED:
      return "ZSTD_COMPRESSED";
    case MC_MSG_FLAG_MANAGED_COMPRESSION_COMPRESSED:
      return "MANAGED_COMPRESSION_COMPRESSED";
    case MC_MSG_FLAG_HOT_KEY:
      return "HOT_KEY";
    case MC_MSG_FLAG_USER_1:
      return "USER_1";
    case MC_MSG_FLAG_USER_2:
      return "USER_2";
    case MC_MSG_FLAG_USER_3:
      return "USER_3";
    case MC_MSG_FLAG_USER_4:
      return "USER_4";
    case MC_MSG_FLAG_USER_5:
      return "USER_5";
    case MC_MSG_FLAG_USER_6:
      return "USER_6";
    case MC_MSG_FLAG_USER_7:
      return "USER_7";
    case MC_MSG_FLAG_USER_8:
      return "USER_8";
    case MC_MSG_FLAG_USER_9:
      return "USER_9";
    case MC_MSG_FLAG_USER_10:
      return "USER_10";
    case MC_MSG_FLAG_USER_11:
      return "USER_11";
    case MC_MSG_FLAG_USER_12:
      return "USER_12";
    case MC_MSG_FLAG_USER_13:
      return "USER_13";
    case MC_MSG_FLAG_USER_14:
      return "USER_14";
    case MC_MSG_FLAG_USER_15:
      return "USER_15";
    case MC_MSG_FLAG_USER_16:
      return "USER_16";
  }
  return "UNKNOWN";
}

/*
 * memcache request functions
 */

static inline int mc_op_has_key(mc_op_t op) {
  switch (op) {
    case mc_op_get:
    case mc_op_set:
    case mc_op_add:
    case mc_op_replace:
    case mc_op_append:
    case mc_op_prepend:
    case mc_op_cas:
    case mc_op_delete:
    case mc_op_incr:
    case mc_op_decr:
    case mc_op_metaget:
    case mc_op_gets:
      return 1;

    default:
      return 0;
  }
}

/** Does given op accept value or not */
static inline int mc_op_has_value(mc_op_t op) {
  switch (op) {
    case mc_op_set:
    case mc_op_add:
    case mc_op_replace:
    case mc_op_append:
    case mc_op_lease_set:
    case mc_op_cas:
      return 1;

    default:
      return 0;
  }
}

static inline int mc_res_is_err(const mc_res_t result) {
  return result >= mc_res_ooo && result < mc_res_waiting;
}

typedef enum mc_req_err_s {
  mc_req_err_valid,
  mc_req_err_no_key,
  mc_req_err_key_too_long,
  mc_req_err_space_or_ctrl
} mc_req_err_t;

const char* mc_req_err_to_string(const mc_req_err_t err);

/**
 * @param result Result code
 *
 * @return Human-readable ASCII string for result.
 */
const char* mc_res_to_response_string(const mc_res_t result);

typedef enum mc_opcode_e : uint8_t {
  mc_opcode_noop               = 0x0a,
  mc_opcode_set                = 0x01,
  mc_opcode_setq               = 0x11,
  mc_opcode_add                = 0x02,
  mc_opcode_addq               = 0x12,
  mc_opcode_replace            = 0x03,
  mc_opcode_replaceq           = 0x13,
  mc_opcode_append             = 0x0e,
  mc_opcode_appendq            = 0x19,
  mc_opcode_prepend            = 0x0f,
  mc_opcode_prependq           = 0x1a,
  mc_opcode_get                = 0x00,
  mc_opcode_getq               = 0x09,
  mc_opcode_getk               = 0x0c,
  mc_opcode_getkq              = 0x0d,
  mc_opcode_delete             = 0x04,
  mc_opcode_deleteq            = 0x14,
  mc_opcode_increment          = 0x05,
  mc_opcode_incrementq         = 0x15,
  mc_opcode_decrement          = 0x06,
  mc_opcode_decrementq         = 0x16,
  mc_opcode_touch              = 0x1c,
  mc_opcode_gat                = 0x1d,
  mc_opcode_gatq               = 0x1e,
  mc_opcode_stat               = 0x10,
  mc_opcode_version            = 0x0b,
  mc_opcode_quit               = 0x07,
  mc_opcode_quitq              = 0x17,
  mc_opcode_flush              = 0x08,
  mc_opcode_flushq             = 0x18,
  // SASL commands
  mc_opcode_sasllistmechs      = 0x20,
  mc_opcode_saslauth           = 0x21,
  mc_opcode_saslstep           = 0x22,
  // Range commands
  mc_opcode_rset               = 0x31,
  mc_opcode_rsetq              = 0x32,
  mc_opcode_rappend            = 0x33,
  mc_opcode_rappendq           = 0x34,
  mc_opcode_rprepend           = 0x35,
  mc_opcode_rprependq          = 0x36,
  mc_opcode_rget               = 0x30,
  mc_opcode_rdelete            = 0x37,
  mc_opcode_rdeleteq           = 0x38,
  mc_opcode_rincr              = 0x39,
  mc_opcode_rincrq             = 0x3a,
  mc_opcode_rdecr              = 0x3b,
  mc_opcode_rdecrq             = 0x3c,
  // v1.6 proposed commands
  mc_opcode_setvbucket         = 0x3d,
  mc_opcode_tapvbucketset      = 0x45,
  mc_opcode_getvbucket         = 0x3e,
  mc_opcode_tapdelete          = 0x42,
  mc_opcode_verbosity          = 0x1b,
  mc_opcode_tapflush           = 0x43,
  mc_opcode_delvbucket         = 0x3f,
  mc_opcode_tapconnect         = 0x40,
  mc_opcode_tapmutation        = 0x41,
  mc_opcode_tapopaque          = 0x44,
  mc_opcode_tapcheckpointstart = 0x46,
  mc_opcode_tapcheckpointend   = 0x47,
} mc_opcode_t;


__END_DECLS
