/*
 *  Copyright (c) 2014-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "msg.h"

#include "mcrouter/lib/mc/protocol.h"

mc_op_t mc_op_from_string(const char* str) {
  int i = 0;
  for (i = mc_op_unknown; i < mc_nops; ++i) {
    if (0 == strcmp(mc_op_to_string((mc_op_t)i), str)) {
      return (mc_op_t)i;
    }
  }
  return mc_op_unknown;
}

const char* mc_req_err_to_string(const mc_req_err_t err) {
  switch (err) {
    case mc_req_err_valid: return "valid";
    case mc_req_err_no_key: return "invalid key: missing";
    case mc_req_err_key_too_long: return "invalid key: too long";
    case mc_req_err_space_or_ctrl:
      return "invalid key: space or control character";
  }
  return "unknown";
}

const char* mc_res_to_response_string(const mc_res_t result) {
  switch (result) {
    case mc_res_unknown: return "SERVER_ERROR unknown result\r\n";
    case mc_res_deleted: return "DELETED\r\n";
    case mc_res_touched: return "TOUCHED\r\n";
    case mc_res_found: return "FOUND\r\n";
    case mc_res_foundstale: return "FOUND_STALE\r\n";
    // hostmiss ?
    case mc_res_notfound: return "NOT_FOUND\r\n";
    case mc_res_notfoundhot: return "NOT_FOUND_HOT\r\n";
    case mc_res_notstored: return "NOT_STORED\r\n";
    case mc_res_stalestored: return "STALE_STORED\r\n";
    case mc_res_ok: return "OK\r\n";
    case mc_res_stored: return "STORED\r\n";
    case mc_res_exists: return "EXISTS\r\n";
    /* soft errors -- */
    /* this shouldn't happen as we don't support UDP yet, and when we do
       hopefully we can be more intelligent than this. */
    case mc_res_ooo: return "SERVER_ERROR out of order\r\n";
    case mc_res_timeout: return "SERVER_ERROR timeout\r\n";
    case mc_res_connect_timeout: return "SERVER_ERROR connection timeout\r\n";
    case mc_res_connect_error: return "SERVER_ERROR connection error\r\n";
    case mc_res_busy: return "SERVER_ERROR 307 busy\r\n";
    case mc_res_try_again: return "SERVER_ERROR 302 try again\r\n";
    case mc_res_shutdown: return "SERVER_ERROR 301 shutdown\r\n";
    case mc_res_tko: return "SERVER_ERROR unavailable\r\n";
    /* hard errors -- */
    case mc_res_bad_command: return "CLIENT_ERROR bad command\r\n";
    case mc_res_bad_key: return "CLIENT_ERROR bad key\r\n";
    case mc_res_bad_flags: return "CLIENT_ERROR bad flags\r\n";
    case mc_res_bad_exptime: return "CLIENT_ERROR bad exptime\r\n";
    case mc_res_bad_lease_id: return "CLIENT_ERROR bad lease_id\r\n";
    case mc_res_bad_cas_id: return "CLIENT_ERROR bad cas_id\r\n";
    case mc_res_bad_value: return "SERVER_ERROR bad value\r\n";
    case mc_res_aborted: return "SERVER_ERROR aborted\r\n";
    case mc_res_client_error: return "CLIENT_ERROR\r\n";
    case mc_res_local_error: return "SERVER_ERROR local error\r\n";
    case mc_res_remote_error: return "SERVER_ERROR remote error\r\n";
    /* in progress -- */
    case mc_res_waiting: return "SERVER_ERROR waiting\r\n";
    case mc_nres: return "SERVER_ERROR unknown result\r\n";
  };
  return "SERVER_ERROR unknown result\r\n";
}
