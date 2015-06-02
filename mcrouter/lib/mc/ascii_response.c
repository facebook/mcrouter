/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "ascii_response.h"

#include <arpa/inet.h>

#include "mcrouter/lib/mc/util.h"

#define ASCII_TERM "\r\n"

#define IOV_WRITE(__s, __l) {                   \
    FBI_ASSERT(niovs < max_niovs);              \
    iovs[niovs].iov_base = (__s);               \
    iovs[niovs].iov_len = (__l);                \
    ++niovs;                                    \
  }

#define IOV_WRITE_STR(s) IOV_WRITE((char *)(s), strlen((char *)(s)))

// only use IOV_WRITE_CONST_STR with hard-coded strings!
#define IOV_WRITE_CONST_STR(s) IOV_WRITE((s), sizeof(s) - 1)

#define IOV_WRITE_NSTRING(s) IOV_WRITE((s).str, (s).len)

#define IOV_FORMAT(scratch, s, ...) {                           \
    char* p = scratch->buffer + scratch->offset;                \
    size_t max_len = SCRATCH_BUFFER_LEN - scratch->offset;      \
    size_t length = snprintf(p, max_len, s, __VA_ARGS__);       \
    length = MIN(max_len - 1, length);                          \
    IOV_WRITE(p, length);                                       \
    scratch->offset += length + 1;                              \
    if (scratch->offset >= SCRATCH_BUFFER_LEN) {                \
      dbg_error("SCRATCH_BUFFER_LEN exceeded with: %s", p);     \
    }                                                           \
  }

#define IOV_WRITE_IP(scratch, v, ip) {                          \
    char* p = scratch->buffer + scratch->offset;                \
    size_t max_len = SCRATCH_BUFFER_LEN - scratch->offset;      \
    int af = (v == 6) ? AF_INET6 : AF_INET;                     \
    inet_ntop(af, (void*)ip, p, max_len);                       \
    size_t length = strlen(p);                                  \
    IOV_WRITE(p, length);                                       \
    scratch->offset += length + 1;                              \
  }

static inline const char* mc_res_to_response_string(const mc_res_t result) {
  switch (result) {
    case mc_res_unknown: return "SERVER_ERROR unknown result\r\n";
    case mc_res_deleted: return "DELETED\r\n";
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

static char* stats_reply_to_string(nstring_t *stats,
                                   size_t num_stats,
                                   size_t *len) {
  uint64_t i;
  size_t slen = 0;
  size_t off = 0;
  char *s = NULL;

  for (i = 0; i < num_stats; i++) {
    nstring_t *name = &stats[2 * i];
    nstring_t *value = &stats[2 * i + 1];
    // At least one of them should have non-zero length
    FBI_ASSERT(name->len || value->len);

    // Not as naive as it looks, the compiler precomputes strlen for constants
    slen += strlen("STAT " /* name\s?value */ ASCII_TERM);
    slen += name->len;
    slen += value->len;
    if (name->len > 0 && value->len > 0) slen++; // space between them
  }
  slen += strlen("END\r\n");
  slen += 1; // '\0'

  s = malloc(slen);
  if (s == NULL) {
    return s;
  }

  for (i = 0; i < num_stats; i++) {
    nstring_t *name = &stats[2 * i];
    nstring_t *value = &stats[2 * i + 1];
    off += snprintf(s + off, slen - off, "STAT");
    if (name->len) {
      off += snprintf(s+off, slen-off, " %.*s", (int) name->len, name->str);
    }
    if (value->len) {
      off += snprintf(s+off, slen-off, " %.*s", (int) value->len, value->str);
    }
    memcpy(s+off, ASCII_TERM, strlen(ASCII_TERM));
    off += strlen(ASCII_TERM);
    FBI_ASSERT(off < slen);
  }

  FBI_ASSERT(slen - off == strlen("END\r\n") + 1);
  snprintf(s + off, slen - off, "END\r\n");

  *len = slen - 1;
  return s;
}

void mc_ascii_response_buf_init(mc_ascii_response_buf_t* buf) {
  buf->offset = 0;
  buf->stats = NULL;
}

void mc_ascii_response_buf_cleanup(mc_ascii_response_buf_t* buf) {
  if (buf->stats) {
    free(buf->stats);
  }
}

size_t mc_ascii_response_write_iovs(mc_ascii_response_buf_t* buf,
                                    nstring_t key,
                                    mc_op_t op,
                                    const mc_msg_t* reply,
                                    struct iovec* iovs,
                                    size_t max_niovs) {
  size_t niovs = 0;
  buf->offset = 0;

  if (mc_res_is_err(reply->result)) {
    if (reply->value.len > 0) {
      if (reply->result == mc_res_client_error) {
        IOV_WRITE_CONST_STR("CLIENT_ERROR ");
      } else {
        IOV_WRITE_CONST_STR("SERVER_ERROR ");
      }
      if (reply->err_code != 0) {
        IOV_FORMAT(buf, "%" PRIu32 " ", reply->err_code);
      }
      IOV_WRITE_NSTRING(reply->value);
      IOV_WRITE_CONST_STR("\r\n");
    } else {
      IOV_WRITE_STR(mc_res_to_response_string(reply->result));
    }
    return niovs;
  }

  switch (op) {
    case mc_op_incr:
    case mc_op_decr:
      switch (reply->result) {
        case mc_res_stored:
          IOV_FORMAT(buf, "%" PRIu64 "\r\n", reply->delta);
          break;
        case mc_res_notfound:
          IOV_WRITE_CONST_STR("NOT_FOUND\r\n");
          break;
        default:
          goto UNEXPECTED;
      }
      break;

    case mc_op_set:
    case mc_op_lease_set:
    case mc_op_add:
    case mc_op_replace:
    case mc_op_append:
    case mc_op_prepend:
    case mc_op_cas:
      switch (reply->result) {
        case mc_res_ok:
          IOV_WRITE_STR(mc_res_to_response_string(mc_res_stored));
          break;

        case mc_res_stored:
        case mc_res_stalestored:
        case mc_res_found:
        case mc_res_notstored:
        case mc_res_notfound:
        case mc_res_exists:
          IOV_WRITE_STR(mc_res_to_response_string(reply->result));
          break;

        default:
          goto UNEXPECTED;
      }
      break;

    case mc_op_delete:
      switch (reply->result) {
        case mc_res_deleted:
        case mc_res_notfound:
          IOV_WRITE_STR(mc_res_to_response_string(reply->result));
          break;
        default:
          goto UNEXPECTED;
      }

      break;

    case mc_op_get:
    case mc_op_lease_get:
    case mc_op_gets:
      switch (reply->result) {
        case mc_res_found:
          IOV_WRITE_CONST_STR("VALUE ");
          IOV_WRITE_NSTRING(key);
          IOV_FORMAT(buf, " %" PRIu64 " %lu", reply->flags,
                     reply->value.len);
          if (op == mc_op_gets) {
            IOV_FORMAT(buf, " %" PRIu64, reply->cas);
          }
          IOV_WRITE_CONST_STR("\r\n");
          IOV_WRITE_NSTRING(reply->value);
          IOV_WRITE_CONST_STR("\r\n");
          break;

        case mc_res_notfound:
          if (op != mc_op_lease_get) {
            // misses should have been suppressed!
            goto UNEXPECTED;
          }
          // but lease-get always has a response
          IOV_WRITE_CONST_STR("LVALUE ");
          IOV_WRITE_NSTRING(key);
          IOV_FORMAT(buf, " %" PRIu64 " %"PRIu64 " %zu\r\n",
                     reply->lease_id,
                     reply->flags,
                     reply->value.len);
          IOV_WRITE_NSTRING(reply->value);
          IOV_WRITE_CONST_STR("\r\n");
          break;

        default:
          goto UNEXPECTED;
      }
      break;

    case mc_op_metaget:
      switch (reply->result) {
        case mc_res_found:
          /* (META key age: (unknown|\d+); exptime: \d+;
             from: (\d+\.\d+\.\d+\.\d+|unknown); is_transient: (1|0)\r\n) */
          IOV_WRITE_CONST_STR("META ");
          IOV_WRITE_NSTRING(key);
          IOV_WRITE_CONST_STR(" age: ");
          if (reply->number == (uint32_t) -1) {
            IOV_WRITE_CONST_STR("unknown");
          }
          else {
            IOV_FORMAT(buf, "%d", reply->number);
          }
          IOV_WRITE_CONST_STR("; exptime: ");
          IOV_FORMAT(buf, "%d", reply->exptime);
          IOV_WRITE_CONST_STR("; from: ");
          if (reply->ipv == 0) {
            IOV_WRITE_CONST_STR("unknown");
          }
          else {
            IOV_WRITE_IP(buf, reply->ipv, &(reply->ip_addr));
          }
          IOV_WRITE_CONST_STR("; is_transient: ");
          IOV_FORMAT(buf, "%" PRIu64, reply->flags);
          IOV_WRITE_CONST_STR("\r\n");
          break;
        case mc_res_notfound:
          break;
        default:
          goto UNEXPECTED;
      }
      break;

    case mc_op_end:
      if (reply->result == mc_res_found) {
        IOV_WRITE_CONST_STR("END\r\n");
      }
      else {
        IOV_WRITE_STR(mc_res_to_response_string(reply->result));
      }
      break;

    case mc_op_stats:
      switch (reply->result) {
        case mc_res_ok:
        {
          size_t length = 0;
          char* stats;
          if (reply->stats) {
            /* TODO(agartrell) assert(!reply->value.str)
             *
             * The assert here can't be turned on until
             * mcrouter/stats.c:560 has been fixed to not set both
             * value and stats on the libmc reply
             */
            stats = stats_reply_to_string(reply->stats, reply->number, &length);
            buf->stats = stats;
          } else {
            stats = reply->value.str;
            length = reply->value.len;
          }

          if (!stats) {
            return 0;
          }

          IOV_WRITE(stats, length);
          break;
        }
        default:
          goto UNEXPECTED;
      }
      break;

    case mc_op_flushall:
    case mc_op_flushre:
      IOV_WRITE_CONST_STR("OK\r\n");
      break;

    case mc_op_version:
      IOV_WRITE_CONST_STR("VERSION ");
      IOV_WRITE_NSTRING(reply->value);
      IOV_WRITE_CONST_STR("\r\n");
      break;

    case mc_op_shutdown:
      if (reply->result == mc_res_ok) {
        IOV_WRITE_CONST_STR("OK\r\n");
      }
      else {
        goto UNEXPECTED;
      }
      break;

    case mc_op_exec:
      switch (reply->result) {
        case mc_res_ok:
          IOV_WRITE_NSTRING(reply->value);
          IOV_WRITE_CONST_STR("\r\n");
          break;
        default:
          goto UNEXPECTED;
      }
      break;

    default:
      IOV_WRITE_CONST_STR("SERVER_ERROR unhandled token ");
      IOV_WRITE_STR(mc_op_to_string(op));
      IOV_FORMAT(buf, " (%d)\r\n", (int)op);
      break;
  }

  return niovs;

UNEXPECTED:
  FBI_ASSERT(niovs == 0);
  IOV_WRITE_CONST_STR("SERVER_ERROR unexpected result ");
  IOV_WRITE_STR(mc_res_to_string(reply->result));
  IOV_FORMAT(buf, " (%d) for ", (int)reply->result);
  IOV_WRITE_STR(mc_op_to_string(op));
  IOV_FORMAT(buf, " (%d)\r\n", (int)op);
  return niovs;
}
