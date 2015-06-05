/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "umbrella_protocol.h"

#include <sys/types.h>
#include <sys/uio.h>

#include "mcrouter/lib/fbi/debug.h"
#include "mcrouter/lib/mc/msg.h"
#include "mcrouter/lib/mc/umbrella.h"
#ifndef LIBMC_FBTRACE_DISABLE
#include "mcrouter/lib/mc/mc_fbtrace_info.h"
#endif

uint32_t const umbrella_op_from_mc[UM_NOPS] = {
#define UM_OP(mc, um) [mc] = um,
#include "mcrouter/lib/mc/umbrella_conv.h" /* nolint */
};

uint32_t const umbrella_op_to_mc[UM_NOPS] = {
#define UM_OP(mc, um) [um] = mc,
#include "mcrouter/lib/mc/umbrella_conv.h" /* nolint */
};

uint32_t const umbrella_res_from_mc[mc_nres] = {
#define UM_RES(mc, um) [mc] = um,
#include "mcrouter/lib/mc/umbrella_conv.h" /* nolint */
};

uint32_t const umbrella_res_to_mc[mc_nres] = {
#define UM_RES(mc, um) [um] = mc,
#include "mcrouter/lib/mc/umbrella_conv.h" /* nolint */
};

typedef struct _parse_info_s {
  uint64_t reqid;
  int key_idx;
  int value_idx;
#ifndef LIBMC_FBTRACE_DISABLE
  int fbtrace_idx;
#endif
  int stats_count;
} _parse_info_t;

// Which fields are required for each message?
#define always_required msg_op
#ifndef LIBMC_FBTRACE_DISABLE
#define always_optional msg_flags | msg_fbtrace
#else
#define always_optional msg_flags
#endif
#define store_req (msg_key | msg_flags | msg_exptime | msg_value)
#define arith_req (msg_key | msg_delta)
#define delete_req (msg_key | OPT(msg_exptime) | OPT(msg_flags) | \
                    OPT(msg_value))

/* query masks */
#define QMASKS {                                             \
  [mc_op_unknown] = -1,                                      \
  [mc_op_echo] = -1,                                         \
  [mc_op_quit] = 0,                                          \
  [mc_op_version] = 0,                                       \
  [mc_op_servererr] = -1,                                    \
  [mc_op_get] = msg_key,                                     \
  [mc_op_gets] = msg_key,                                    \
  [mc_op_metaget] = msg_key,                                 \
  [mc_op_set] = store_req,                                   \
  [mc_op_add] = store_req,                                   \
  [mc_op_replace] = store_req,                               \
  [mc_op_append] = store_req,                                \
  [mc_op_prepend] = store_req,                               \
  [mc_op_cas] = store_req | msg_cas,                         \
  [mc_op_delete] = delete_req,                               \
  [mc_op_incr] = arith_req,                                  \
  [mc_op_decr] = arith_req,                                  \
  [mc_op_flushall] = 0,                                      \
  [mc_op_flushre] = msg_key,                                 \
  [mc_op_stats] = OPT(msg_key),                              \
  [mc_op_verbosity] = -1,                                    \
  [mc_op_lease_get] = msg_key,                               \
  [mc_op_lease_set] = store_req | msg_lease_id,              \
  [mc_op_shutdown] = OPT(msg_number),                        \
  [mc_op_end] = 0,                                           \
  [mc_nops] = -1,                                            \
}

#define store_rsp (msg_result | OPT(msg_err_code))
#define get_rsp (msg_result | OPT(msg_value) | OPT(msg_err_code))

/* result masks */
#define RMASKS {                                                    \
  [mc_op_unknown] = -1,                                             \
  [mc_op_echo] = -1,                                                \
  [mc_op_quit] = 0,                                                 \
  [mc_op_version] = msg_result | msg_value,                         \
  [mc_op_servererr] = msg_result | OPT(msg_value),                  \
  [mc_op_get] = get_rsp | msg_key | msg_flags,                      \
  [mc_op_metaget] = msg_key | msg_exptime | msg_number |            \
                    msg_flags | msg_value,                          \
  [mc_op_gets] = get_rsp | OPT(msg_cas),                            \
  [mc_op_set] = store_rsp,                                          \
  [mc_op_add] = store_rsp,                                          \
  [mc_op_replace] = store_rsp,                                      \
  [mc_op_append] = store_rsp,                                       \
  [mc_op_prepend] = store_rsp,                                      \
  [mc_op_cas] = store_rsp,                                          \
  [mc_op_delete] = store_rsp,                                       \
  [mc_op_incr] = store_rsp,                                         \
  [mc_op_decr] = store_rsp,                                         \
  [mc_op_flushall] = msg_result,                                    \
  [mc_op_flushre] = msg_result,                                     \
  [mc_op_stats] =                                                   \
    msg_result | msg_stats | msg_number,                            \
  [mc_op_verbosity] = -1,                                           \
  [mc_op_lease_get] = get_rsp,                                      \
  [mc_op_lease_set] = store_rsp,                                    \
  [mc_op_shutdown] = msg_result,                                    \
  [mc_op_end] = 0,                                                  \
  [mc_nops] = -1,                                                   \
}


#define OPT(x) 0
uint64_t const q_req_masks[] = QMASKS;
uint64_t const r_req_masks[] = RMASKS;
#undef OPT

#define OPT(x) x
uint64_t const q_opt_masks[] = QMASKS;
uint64_t const r_opt_masks[] = RMASKS;
#undef OPT


static inline int valid_msg(mc_op_t op, uint64_t field_mask) {
  // For now, let's not check for the proper fields and just throw whatever
  // they gave us into a struct.  It's probably more efficient this way, but
  // relies upon the upper layers doing reasonable things
  return 1;
  //return valid_query(op, field_mask) || valid_result(op, field_mask);
}

static inline void _set_str(nstring_t* str, const uint8_t* body,
                            um_elist_entry_t* entry) {
  str->str = (char*)(body + ntoh32(entry->data.str.offset));
  str->len = ntoh32(entry->data.str.len) - 1;
}

#ifndef LIBMC_FBTRACE_DISABLE

static inline int _cpy_fbtrace_meta(char* dst, const uint8_t* body,
                                     um_elist_entry_t* entry) {
  int meta_len = ntoh32(entry->data.str.len) - 1;
  if (meta_len <= FBTRACE_METADATA_SZ) {
    memcpy(dst, body + ntoh32(entry->data.str.offset), meta_len);
  } else {
    return -1;
  }
  return 0;
}

#endif

static int _fill_msg_strs(mc_msg_t* msg, entry_list_t* elist,
                          const uint8_t* body, _parse_info_t* parse_info) {
  if (parse_info->key_idx >= 0) {
    _set_str(&msg->key, body, &elist->entries[parse_info->key_idx]);
  }
  if (parse_info->value_idx >= 0) {
    _set_str(&msg->value, body, &elist->entries[parse_info->value_idx]);
    if (msg->op == mc_op_metaget) {
      int af = AF_INET;
      if (strchr(msg->value.str, ':') != NULL) {
        af = AF_INET6;
      }
      if (inet_pton(af, msg->value.str, &msg->ip_addr) > 0) {
        msg->ipv = af == AF_INET ? 4 : 6;
      }
      msg->value.str = NULL;
      msg->value.len = 0;
    }
  }

#ifndef LIBMC_FBTRACE_DISABLE

  if (parse_info->fbtrace_idx >= 0) {
    if (msg->fbtrace_info == NULL) {
      msg->fbtrace_info = new_mc_fbtrace_info(0);
      if (msg->fbtrace_info == NULL || msg->fbtrace_info->fbtrace == NULL) {
        return -1;
      }
    } else {
      dbg_error("msg->fbtrace_info was already initialized");
    }
    if (_cpy_fbtrace_meta(msg->fbtrace_info->metadata, body,
             &elist->entries[parse_info->fbtrace_idx])) {
      return -1;
    }
  }

#endif

  return 0;
}


static mc_msg_t *_msg_create(mc_msg_t *base, entry_list_t *elist,
                             _parse_info_t* parse_info) {
  size_t msg_size = 0;
  mc_msg_t *msg = NULL;
  size_t stats_offset = 0;
  size_t body_offset = 0;
  void *body = NULL;

  // Make sure we're not screwing up alignment here
  FBI_ASSERT(sizeof(mc_msg_t) % sizeof(void *) == 0);

  // Construct a message of the following format
  //    __________________
  //   |                   |
  //   |      mc_msg_t     | <- base message
  //   |___________________|
  //   |                   |
  //   |  nstring_t array  | <- stats array (optional)
  //   |___________________|
  //   |                   |
  //   |     key string    | <- key (optional)
  //   |___________________|
  //   |                   |
  //   |    value string   | <- value (optional)
  //   |___________________|
  //   |                   |
  //   |      stats[0]     | <- stats[0] (and so on...)
  //   |___________________|
  //

  msg_size = sizeof(mc_msg_t);

  if (parse_info->stats_count > 0) {
    stats_offset = msg_size;
    msg_size += (sizeof(nstring_t) * parse_info->stats_count);
  }

  body_offset = msg_size;
  msg_size += elist->nbody;

  FBI_ASSERT(msg_size >= sizeof(*base));

  msg = mc_msg_new(msg_size - sizeof(mc_msg_t));
  if (msg == NULL) {
    goto error;
  }

  // Copy base
  mc_msg_copy(msg, base);

  // Copy body
  memcpy((void*)msg + body_offset, elist->body, elist->nbody);
  body = (void*)msg + body_offset;

  if (parse_info->stats_count > 0) {
    FBI_ASSERT(stats_offset > 0);
    nstring_t *stats = (nstring_t*) ((void*)msg + stats_offset);
    uint64_t i;
    int sidx;
    for (i = 0, sidx = 0;
         i < elist->nentries && sidx < parse_info->stats_count; i++) {
      um_elist_entry_t *entry = &elist->entries[i];
      if (ntoh16(entry->tag) == msg_stats) {
        _set_str(&stats[sidx], body, entry);
        sidx++;
      }
    }
    msg->stats = stats;
  }

  _fill_msg_strs(msg, elist, body, parse_info);

  return msg;

error:
  if (msg != NULL) {
    mc_msg_decref(msg);
    msg = NULL;
  }
  return NULL;
}

static int _fill_base_msg(entry_list_t *elist,
                          mc_msg_t* base,
                          _parse_info_t* parse_info) {
  FBI_ASSERT(elist && base && parse_info);
  uint64_t i;
  parse_info->reqid = 0;
  parse_info->key_idx = -1;
  parse_info->value_idx = -1;

#ifndef LIBMC_FBTRACE_DISABLE

  parse_info->fbtrace_idx = -1;

#endif

  parse_info->stats_count = 0;
  uint64_t field_mask = 0;

  for (i = 0; i < elist->nentries; i++) {
    uint16_t tag = ntoh16(elist->entries[i].tag);
    uint64_t val = ntoh64(elist->entries[i].data.val);

    // No dup fields (except for stats)
    if ((tag & field_mask) && (tag != msg_stats)) {
      dbg_error("Duplicate field: field_mask = 0x%lX, field = 0x%X\n",
                field_mask, tag);
      return -1;
    }
    field_mask |= tag;
    switch (tag) {
      case msg_op:
        if (val >= UM_NOPS) {
          return -1;
        }
        base->op = umbrella_op_to_mc[val];
        if (base->op == mc_nops) {
          return -1;
        }
        break;
      case msg_result:
        if (val >= mc_nres) {
          return -1;
        }
        base->result = umbrella_res_to_mc[val];
        break;
      case msg_reqid:
        if (val == 0) {
          return -1;
        }
        parse_info->reqid = val;
        break;
      case msg_err_code:
        base->err_code = val;
        break;
      case msg_flags:
        base->flags = val;
        break;
      case msg_exptime:
        base->exptime = val;
        break;
      case msg_number:
        base->number = val;
        break;
      case msg_delta:
        base->delta = val;
        break;
      case msg_lease_id:
        base->lease_id = val;
        break;
      case msg_cas:
        base->cas = val;
        break;
      case msg_key:
        if (parse_info->key_idx == -1) {
          parse_info->key_idx = i;
        }
        break;
      case msg_value:
        if (parse_info->value_idx != -1) {
          return -1;
        }
        parse_info->value_idx = i;
        break;
      case msg_stats:
        parse_info->stats_count++;
        break;

#ifndef LIBMC_FBTRACE_DISABLE

      case msg_fbtrace:
        if (parse_info->fbtrace_idx != -1) {
          return -1;
        }
        parse_info->fbtrace_idx = i;
        break;

#endif

      default:
        return -1;
    }
  }

  if (parse_info->reqid == 0) {
    return -1;
  }
  if (parse_info->key_idx >= (int64_t)elist->nentries) {
    return -1;
  }
  if (parse_info->value_idx >= (int64_t)elist->nentries) {
    return -1;
  }

  if (!valid_msg(base->op, field_mask)) {
    dbg_error("Invalid message");
    return -1;
  }

  return 0;
}

int um_parser_init(um_parser_t* um_parser) {
  FBI_ASSERT(um_parser);
  entry_list_preparer_init(&um_parser->prep);
  return 0;
}

int um_parser_reset(um_parser_t* um_parser) {
  entry_list_preparer_reset_after_failure(&um_parser->prep);
  return 0;
}

ssize_t um_consume_one_message(um_parser_t* um_parser,
                               const uint8_t* buf, size_t nbuf,
                               uint64_t* reqid_out,
                               mc_msg_t** msg_out) {
  FBI_ASSERT(um_parser && buf && nbuf > 0 && reqid_out && msg_out);
  *msg_out = NULL;

  ssize_t consumed = entry_list_preparer_read(&um_parser->prep,
                                              (const char*)buf,
                                              nbuf);
  if (consumed <= 0) {
    goto error;
  }

  /* Because the rank of the unsigned integer is equal to the rank of the
   * signed integer, the signed integer is converted to the type of the
   * unsigned integer, and this assertion triggers on error.  To get around
   * this, we do the cast ourselves.
   */
  FBI_ASSERT(consumed <= (ssize_t)nbuf);

  if (entry_list_preparer_finished(&um_parser->prep)) {
    entry_list_t elist;
    if (entry_list_consume_preparer(&elist, &um_parser->prep) < 0) {
      goto error;
    }
    mc_msg_t base;
    mc_msg_init_not_refcounted(&base);
    _parse_info_t parse_info;
    if (_fill_base_msg(&elist, &base, &parse_info) != 0) {
      goto error;
    }
    *reqid_out = parse_info.reqid;

    *msg_out = _msg_create(&base, &elist, &parse_info);
    if (*msg_out == NULL) {
      dbg_error("msg_create failed");
      goto error;
    }

    FBI_ASSERT((*msg_out)->op != mc_op_end);

    entry_list_cleanup(&elist);
  } else {
    FBI_ASSERT(consumed == nbuf);
  }

  return consumed;

error:
  entry_list_preparer_reset_after_failure(&um_parser->prep);

  // Function that return an error must have not left any messages around
  FBI_ASSERT(*msg_out == NULL);
  return -1;
}

int um_consume_buffer(um_parser_t* um_parser,
                      const uint8_t* buf, size_t nbuf,
                      msg_ready_cb* msg_ready,
                      void* context) {
  while (nbuf > 0) {
    uint64_t reqid;
    mc_msg_t* msg;
    ssize_t consumed = um_consume_one_message(um_parser,
                                              buf, nbuf, &reqid, &msg);
    if (consumed <= 0) {
      return -1;
    }
    FBI_ASSERT(consumed <= nbuf);
    if (msg != NULL) {
      msg_ready(context, reqid, msg);
      buf += consumed;
      nbuf -= consumed;
    } else {
      FBI_ASSERT(consumed == nbuf);
      break;
    }
  }
  return 0;
}

static void _msg_to_elist(entry_list_t* elist,
                          uint64_t reqid,
                          const mc_msg_t* msg,
                          struct iovec* value_iovs,
                          size_t n_value_iovs) {
  entry_list_append_I32(elist, msg_op, umbrella_op_from_mc[msg->op]);

#define append_non_zero(type, field) {                                  \
    if ((msg->field) != 0) {                                            \
      entry_list_append_##type(elist, msg_##field, msg->field);         \
    }                                                                   \
  }

  if (reqid != 0) {
    entry_list_append_U64(elist, msg_reqid, reqid);
  }
  if (msg->result != 0) {
    entry_list_append_I32(elist, msg_result, umbrella_res_from_mc[msg->result]);
  }
  append_non_zero(I32, err_code);
  append_non_zero(U64, flags);
  append_non_zero(U32, exptime);
  append_non_zero(U32, number);
  append_non_zero(U64, delta);
  append_non_zero(U64, lease_id);
  append_non_zero(U64, cas);

#undef append_non_zero

  if (msg->op == mc_op_metaget) {
    if (msg->ipv == 4 || msg->ipv == 6) {
      char addr[INET6_ADDRSTRLEN];
      entry_list_append_CSTRING(elist, msg_value,
                                inet_ntop(msg->ipv == 4 ? AF_INET : AF_INET6,
                                &msg->ip_addr, addr, INET6_ADDRSTRLEN));
    }
  }
  if (msg->key.str != NULL) {
    entry_list_lazy_append_BSTRING(elist, msg_key, msg->key.str,
                                   msg->key.len + 1);
  }

  /* either iovec or string, but not both */
  assert(!(msg->value.str != NULL && n_value_iovs != 0));
  if (msg->value.str != NULL) {
    entry_list_lazy_append_BSTRING(elist, msg_value,
                                   msg->value.str, msg->value.len + 1);
  } else if (n_value_iovs != 0) {
    entry_list_lazy_append_IOVEC(elist, msg_value, value_iovs, n_value_iovs);
  }

  if (msg->op == mc_op_stats && msg->number > 0) {
    int i, stats_count = msg->number * 2;
    for (i = 0; i < stats_count; i++) {
      entry_list_lazy_append_BSTRING(elist, msg_stats,
                                     msg->stats[i].str, msg->stats[i].len + 1);
    }
  }

#ifndef LIBMC_FBTRACE_DISABLE

  if (msg->fbtrace_info) {
    entry_list_append_CSTRING(elist, msg_fbtrace,
                              msg->fbtrace_info->metadata);
  }

#endif
}

static void _backing_msg_fill(um_backing_msg_t* bmsg,
                              uint64_t reqid, mc_msg_t* msg,
                              struct iovec* value_iovs,
                              size_t n_value_iovs) {
  FBI_ASSERT(bmsg->msg == NULL);
  if (msg->_refcount != MSG_NOT_REFCOUNTED) {
    bmsg->msg = mc_msg_incref(msg);
  } else {
    bmsg->msg = NULL;
  }
  _msg_to_elist(&bmsg->elist, reqid, msg, value_iovs, n_value_iovs);
  bmsg->inuse = 1;
}

int um_backing_msg_init(um_backing_msg_t* bmsg) {
  entry_list_init(&bmsg->elist);
  bmsg->elist.entries = bmsg->entries_array;
  bmsg->elist.entries_size = BMSG_ENTRIES_ARRAY_SIZE;
  bmsg->msg = NULL;
  bmsg->inuse = 0;
  return 0;
}

void um_backing_msg_cleanup(um_backing_msg_t* bmsg) {
  entry_list_cleanup(&bmsg->elist);
  bmsg->elist.entries = bmsg->entries_array;
  bmsg->elist.entries_size = BMSG_ENTRIES_ARRAY_SIZE;
  if (bmsg->msg != NULL) {
    mc_msg_decref(bmsg->msg);
    bmsg->msg = NULL;
  }
  bmsg->inuse = 0;
}

int um_emit_iovs_extended(um_backing_msg_t* bmsg,
                          uint64_t reqid,
                          mc_msg_t* msg,
                          struct iovec* value_iovs,
                          size_t n_value_iovs,
                          emit_iov_cb* emit_iov,
                          void* context) {

  FBI_ASSERT(bmsg && !bmsg->inuse && msg && msg->op != mc_op_end && emit_iov);
  _backing_msg_fill(bmsg, reqid, msg, value_iovs, n_value_iovs);

  if (entry_list_emit_iovs(&bmsg->elist, emit_iov, context)) {
    um_backing_msg_cleanup(bmsg);
    return -1;
  }

  return 0;
}

ssize_t um_write_iovs_extended(um_backing_msg_t* bmsg,
                               uint64_t reqid,
                               mc_msg_t* msg,
                               struct iovec* value_iovs,
                               size_t n_value_iovs,
                               struct iovec* iovs,
                               size_t n_iovs) {

  FBI_ASSERT(bmsg && !bmsg->inuse && msg && msg->op != mc_op_end && iovs);
  _backing_msg_fill(bmsg, reqid, msg, value_iovs, n_value_iovs);

  int iovs_used = entry_list_to_iovecs(&bmsg->elist, iovs, n_iovs);
  if (iovs_used <= 0) {
    um_backing_msg_cleanup(bmsg);
    return -1;
  }

  return iovs_used;
}
