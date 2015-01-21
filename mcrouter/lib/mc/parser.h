/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#ifndef FB_MEMCACHE_MC_PARSER_H
#define FB_MEMCACHE_MC_PARSER_H

#include <inttypes.h>
#include <stdbool.h>

#include "mcrouter/lib/fbi/decls.h"
#include "mcrouter/lib/fbi/nstring.h"
#include "mcrouter/lib/mc/msg.h"
#include "mcrouter/lib/mc/protocol.h"
#include "mcrouter/lib/mc/umbrella.h"
#include "mcrouter/lib/mc/umbrella_protocol.h"

__BEGIN_DECLS

/**
 * TAO SERVER_ERROR 200 responses can be up to ~200K, and are considered
 * a single token in the protocol
 */
#define MC_TOKEN_MAX_LEN (512*1024)
#define MC_TOKEN_INCREMENT (64)

typedef enum _parser_state_e {
  receive_reply_unknown_state = 0,
  parser_msg_header,
  receive_sync_reply_header,
  receive_reply_vheader,
  parser_partial,
  parser_body,
  reply_received,
  parser_idle,
} _parser_state_t;

typedef enum parser_error_e {
  parser_unspecified_error = 0,
  parser_malformed_request,
  parser_out_of_memory,
} parser_error_t;

static inline const char* parser_state_to_string(const _parser_state_t state) {
  static const char* const strings [] = {
    "unknown-state",
    "parser-msg-header",
    "receive_sync_reply_header",
    "receive-vheader",
    "parser-partial",
    "parser-body",
    "reply-received",
    "reply-idle",
  };

  return strings[state <= reply_received ? state : receive_reply_unknown_state];
}

static inline const char* parser_error_to_string(const parser_error_t error) {
  static const char* const strings [] = {
    "parser_unspecified_error",
    "parser_malformed_request",
    "parser_out_of_memory",
  };

  return strings[error <= parser_out_of_memory ? error :
                 parser_unspecified_error];
}


/*
 * ascii parser state
 */
typedef struct parser_s {
  _parser_state_t parser_state;
  int ragel_start_state;
  int ragel_state;

  size_t off;
  size_t resid;

  mc_msg_t *msg;

  int partial_token; /// < 1 indicate the parser is in the middle of a token
  size_t tbuf_len; ///< token buffer length
  char *tbuf; ///< token buffer
  char* te; ///< token end

  /**
   * Normally a key in "VALUE" response is ignored, since we already know
   * the request.  In certain applications like traffic analysis, we do
   * want to save it though.
   */
  bool record_skip_key;

  int in_skipped_key;
  int in_key;
  int bad_key;
  parser_error_t error;

  mc_protocol_t known_protocol; ///< protocol we are parsing

  um_parser_t um_parser;

  void (*msg_ready)(void *context, uint64_t reqid, mc_msg_t *msg);
  void (*parse_error)(void *context, parser_error_t error);

  void *context;
} mc_parser_t;

typedef enum {
  request_parser,
  reply_parser,
  request_reply_parser, ///< Useful for network sniffing
} parser_type_t;

void mc_parser_init(mc_parser_t *parser,
                    parser_type_t parser_type,
                    void (*msg_ready)(void *context, uint64_t reqid,
                                      mc_msg_t *msg),
                    void (*parse_error)(void *context, parser_error_t error),
                    void *context);

int mc_parser_ensure_tbuf(mc_parser_t *parser, int n);
void mc_parser_cleanup_tbuf(mc_parser_t *parser);

/**
 * Given a beginning of input stream, figures out what protocol it's using.
 * Will not call any callbacks.
 *
 * @param first_byte  The first byte from a newly opened stream
 */
mc_protocol_t mc_parser_determine_protocol(uint8_t first_byte);

/**
 * Process [buf, buf+len). Will call msg_ready callback on any complete messages
 * and parse_error on any errors.
 *
 * @param buf  Must point to a valid buffer of size len.
 * @param len  Must be at least 1
 */
void mc_parser_parse(mc_parser_t *parser, const uint8_t *buf, size_t len);

/**
 * Cleans up internal state, freeing any allocated memory.
 * After the call, the parser is in the same state as after mc_parser_init().
 */
void mc_parser_reset(mc_parser_t *parser);

int get_request_ragel_start_state();
int get_reply_ragel_start_state();
int get_request_reply_ragel_start_state();

unsigned long mc_parser_num_partial_messages();

void mc_parser_reset_num_partial_messages();

__END_DECLS

#endif
