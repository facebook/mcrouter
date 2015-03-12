/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "parser.h"

#include <stdio.h>

//#include "generic.h"
#include "mcrouter/lib/mc/ascii_client.h"
#include "mcrouter/lib/mc/umbrella_protocol.h"

static unsigned long _num_partial_messages = 0;
unsigned long mc_parser_num_partial_messages() {
  return _num_partial_messages;
}

void mc_parser_reset_num_partial_messages() {
  _num_partial_messages = 0;
}

void mc_string_new(nstring_t *nstr, const char *buf, size_t len) {
  nstr->str = malloc(len + 1);
  FBI_ASSERT(nstr->str);
  strncpy(nstr->str, buf, len);
  nstr->str[len] = '\0';
  nstr->len = len;
}

void mc_parser_init(mc_parser_t *parser,
                    parser_type_t parser_type,
                    void (*msg_ready)(void *c, uint64_t id, mc_msg_t *req),
                    void (*parse_error)(void *context, parser_error_t error),
                    void *context) {
  FBI_ASSERT(msg_ready);
  FBI_ASSERT(parse_error);
  memset(parser, 0, sizeof(*parser));

  switch (parser_type) {
    case reply_parser:
      parser->ragel_start_state = get_reply_ragel_start_state();
      break;

    case request_parser:
      parser->ragel_start_state = get_request_ragel_start_state();
      break;

    case request_reply_parser:
      parser->ragel_start_state = get_request_reply_ragel_start_state();
      break;
  }

  parser->parser_state = parser_idle;
  parser->msg_ready = msg_ready;
  parser->parse_error = parse_error;
  parser->context = context;
  parser->known_protocol = mc_unknown_protocol;

  parser->record_skip_key = false;
  parser->in_skipped_key = 0;
  parser->in_key = 0;
  parser->bad_key = 0;

  um_parser_init(&parser->um_parser);
}

/**
 * Ensure tbuf can hold n additional bytes, grow the buffer if needed in
 * a multiple of MC_TOKEN_INCREMENT as long as it doesn't exceed
 * MC_TOKEN_MAX_LEN
 * return 0 on success
 */
int mc_parser_ensure_tbuf(mc_parser_t *parser, int n) {
  FBI_ASSERT(parser);

  // tlen could be zero now
  FBI_ASSERT(parser->te >= parser->tbuf);
  size_t tlen = parser->te - parser->tbuf;
  size_t tbuf_len = tlen + n + 1;
  size_t padding = tbuf_len % MC_TOKEN_INCREMENT;
  if(padding != 0) {
    tbuf_len += (MC_TOKEN_INCREMENT - padding);
  }

  if(parser->tbuf_len < tbuf_len && tbuf_len <= MC_TOKEN_MAX_LEN) {
    parser->tbuf = (char*) realloc(parser->tbuf, tbuf_len);
    parser->tbuf_len = tbuf_len;
    parser->te = parser->tbuf + tlen;
  }
  int ret = 0;
  if(tbuf_len > MC_TOKEN_MAX_LEN) {
    ret = -1;
  }
  return ret;
}

void mc_parser_cleanup_tbuf(mc_parser_t *parser) {
  FBI_ASSERT(parser != NULL);
  if (parser->tbuf) {
    free(parser->tbuf);
    parser->tbuf = parser->te = NULL;
    parser->tbuf_len = 0;
  }
}

mc_protocol_t mc_parser_determine_protocol(uint8_t first_byte) {
  return (first_byte == ENTRY_LIST_MAGIC_BYTE)
    ? mc_umbrella_protocol
    : mc_ascii_protocol;
}

void mc_parser_parse(mc_parser_t *parser, const uint8_t *buf, size_t len) {
  FBI_ASSERT(len > 0);

  if (parser->known_protocol == mc_unknown_protocol) {
    parser->known_protocol = mc_parser_determine_protocol(buf[0]);
  }

  int success = 0;
  switch (parser->known_protocol) {
    case mc_ascii_protocol:
      if (parser->parser_state == parser_idle) {
        parser->parser_state = parser_msg_header;
      }
      success = (_on_ascii_rx(parser, (char*)buf, len) == 1);
      break;

    case mc_umbrella_protocol:
      success = !um_consume_buffer(&parser->um_parser, buf, len,
                                   parser->msg_ready, parser->context);
      break;

    default:
      FBI_ASSERT(!"parser->protocol was never set");
  }

  if (!success) {
    parser->parse_error(parser->context,
                        errno == ENOMEM ?
                        parser_out_of_memory :
                        parser_malformed_request);
  }
}

void mc_parser_reset(mc_parser_t *parser) {
  FBI_ASSERT(parser != NULL);
  if (parser->msg != NULL) {
    _num_partial_messages++; // An estimation is good enough here
    mc_msg_decref(parser->msg);
    parser->msg = NULL;
  }

  parser->parser_state = parser_idle;
  parser->off = 0;
  parser->resid = 0;
  parser->known_protocol = mc_unknown_protocol;
  mc_parser_cleanup_tbuf(parser);

  um_parser_reset(&parser->um_parser);
}
