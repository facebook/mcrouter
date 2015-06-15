/* -*- mode: c; -*- */
/* ascii_client.c is built from ascii_client.rl using ragel */

#include <ctype.h>

#include "mcrouter/lib/fbi/debug.h"
#include "mcrouter/lib/mc/_protocol.h"
#include "mcrouter/lib/mc/parser.h"
#include "mcrouter/lib/mc/msg.h"
#include "mcrouter/lib/mc/util.h"

// In libmc.client, only read the first N stats for simplicity of
// implementation. In the future it might make sense to allow an
// arbitrary number of stats, but for now that makes the code far
// easier
#define MAX_NUM_STATS 20

// Are we in the middle of a key being skipped?
#define skipping_key(parser) ((parser)->in_skipped_key &&       \
                              !(parser)->record_skip_key)

%%{

# input file for ragel (http://www.complang.org/ragel/)

# this machine name, which must come first, is used as a prefix for variables
machine ascii;

# named actions. All the actions (named and unnamed) are executed in the
# context of _on_ascii_rx() where "%% write exec;" is called.
action start_token {
  if (!skipping_key(parser)) {
    parser->partial_token = 1;
    parser->te = parser->tbuf;
    ts = p;
  }
}
action finish_token {
  if (parser->partial_token && !skipping_key(parser)) {
    FBI_ASSERT(ts);
    parser->partial_token = 0;
    if (parser->te && parser->te > parser->tbuf) {
      /* we have something stuffed in our cheeks,
         let's put them together and update ts. */
      FBI_ASSERT(ts <= p);
      size_t n = p - ts;
      if(mc_parser_ensure_tbuf(parser, n) != 0) {
        if (parser->in_key) {
          parser->bad_key = 1;
        } else {
          parser->error = parser_malformed_request;
          fgoto error_handling;
        }
      } else {
        strncpy(parser->te, ts, n);
        parser->te += n;
        *parser->te = '\0';
        tlen = parser->te - parser->tbuf;
        ts = parser->tbuf;
      }
    } else {
      tlen = p - ts;
      if (tlen >= MC_TOKEN_MAX_LEN) {
        if (parser->in_key) {
          parser->bad_key = 1;
        } else {
          parser->error = parser_malformed_request;
          fgoto error_handling;
        }
      }
    }
  }
}

action record_key {
  if (!skipping_key(parser)) {
    if (tlen > MC_KEY_MAX_LEN_ASCII) {
      parser->bad_key = 1;
    }
    if (parser->bad_key) {
      parser->msg->result = mc_res_bad_key;
    } else {
      if (mc_msg_grow(&parser->msg, tlen + 1,
                      (void**) &parser->msg->key.str) != 0) {
        parser->error = parser_out_of_memory;
        fgoto error_handling;
      }
      parser->msg->key.len = tlen;
      strncpy(parser->msg->key.str, ts, tlen);
      FBI_ASSERT(!memcmp(parser->msg->key.str, ts, tlen));
      parser->msg->key.str[tlen] = '\0';
    }
  }
  parser->in_skipped_key = 0;
  parser->in_key = 0;
  parser->bad_key = 0;
}

action fin {
  FBI_ASSERT(parser->msg != NULL);

  // end of msg, fake eof
  pe = eof = p + 1;
  parser->parser_state = parser_msg_header;
  /* Note: we need to reset parser->msg first,
     in case mc_parser_reset is called from the callback */
  mc_msg_t* msg = parser->msg;
  parser->msg = NULL;
  parser->msg_ready(parser->context, 0, msg);
  nmsgs++;

  mc_parser_cleanup_tbuf(parser);
}

nl = '\r'? '\n';
fin = nl @fin;

# server-reported errors. We should only ever see SERVER_ERROR. If we see
# CLIENT_ERROR then libmc probably didn't do enough error checking before
# sending to memcache in the first place. ERROR is absolutely a bug in libmc.
error_handling := '' >{
  if (parser->msg != NULL) {
    mc_msg_decref(parser->msg);
  }
  parser->msg = NULL;
  parser->te = NULL;
  parser->in_key = 0;
  parser->bad_key = 0;
  fbreak;
};

error_code = ' ' digit+ ' '
  @{
    parser->msg->err_code = strtoll(ts, NULL, 10);
  };

action capture_error_value {
  // Since the token we are parsing includes the \r\n, we need
  // to invoke this code upon entering the DFA's final state.
  // Only the '\r' is stored in the token buffer, so we only
  // need to remove one character.
  tlen--;
  if (mc_msg_grow(&parser->msg, tlen + 1,
                  (void**) &parser->msg->value.str) != 0) {
    parser->error = parser_out_of_memory;
    fgoto error_handling;
  }
  parser->msg->value.len = tlen;
  memcpy(parser->msg->value.str, ts, tlen);
  parser->msg->value.str[tlen] = '\0';
}

# server error apparently needs to be able to read binary data to support
# TAO, which uses SERVER_ERROR to send the fbid of objects back to the
# client during set requests.  There is no size field, so we need to
# keep parsing the response string until we hit \r\n.  Server error
# 307 is returned by a server that is overloaded or is shutting down;
# it is treated specially so that it can cross the application/transport
# boundary.  (mc_res_busy is an artifact of our use of TCP/IP, of our
# desired to keep the connection open while not accepting requests.
# If there was a reliable request-based protocol it would be less
# necessary.)
server_error = 'SERVER_ERROR' error_code? ' '? (/.*\r\n/) >start_token
  @finish_token
  @capture_error_value
  @{
    if (parser->msg->err_code == SERVER_ERROR_BUSY) {
      parser->msg->result = mc_res_busy;
    } else {
      parser->msg->result = mc_res_remote_error;
    }
  }
  @fin;

# client error message may contain useful stuff, so we also want to save
# it for the caller
client_error = 'CLIENT_ERROR' error_code? ' '? (/.*\r\n/) >start_token
  @finish_token
  @capture_error_value
  @{
    parser->msg->result = mc_res_client_error;
  }
  @fin;

command_error = 'ERROR'
  @{
    parser->msg->result = mc_res_local_error;
    parser->msg->value.str = "unexpected ERROR reply";
    parser->msg->value.len = strlen(parser->msg->value.str);
    dbg_error("%s", parser->msg->value.str);
    isError = true;
  } fin;

error = server_error | client_error | command_error;

# Get request is ready to be passed to the caller
action get_req_is_ready {
  FBI_ASSERT(parser->msg != NULL);

  mc_op_t op = parser->msg->op;
  /* Note: we need to reset parser->msg first,
     in case mc_parser_reset is called from the callback */
  mc_msg_t* msg = parser->msg;
  parser->msg = mc_msg_new(0);
  parser->msg_ready(parser->context, 0, msg);
  if (parser->msg == NULL) {
    parser->error = parser_out_of_memory;
    fgoto error_handling;
  }

  parser->msg->op = op;
}

# building blocks
stored = 'STORED' @{ parser->msg->result = mc_res_stored; };
stale_stored = 'STALE_STORED' @{
  parser->msg->result = mc_res_stalestored;
} fin;
not_stored = 'NOT_STORED' @{ parser->msg->result = mc_res_notstored; };
exists = 'EXISTS' @{ parser->msg->result = mc_res_exists; };
not_found = 'NOT_FOUND' @{ parser->msg->result = mc_res_notfound; };
deleted = 'DELETED' @{ parser->msg->result = mc_res_deleted; };

storage_reply = stored | stale_stored | not_stored | exists | not_found | deleted;
storage = storage_reply fin;
arithmetic_reply = (not_found |
  digit+ >start_token %finish_token %{
    parser->msg->delta = strtoll(ts, NULL, 10);
    parser->msg->result = mc_res_stored;
  } ' '* fin);

key_skip = (any+ -- (cntrl | space))
  >{ parser->in_skipped_key = 1; parser->in_key = 1; }
  >start_token %finish_token
  %record_key;

key = (any+ -- (cntrl | space))
  >{ parser->in_skipped_key = 0; parser->in_key = 1; }
  >start_token %finish_token
  %record_key;

# This is used for stats and exec commands which can have keys with spaces.
multi_token = ' '* (print+ -- ( '\r' | '\n' ))
  >start_token %finish_token %{
  while (tlen > 0 && isspace(ts[tlen-1])) {
    tlen--;
  }
  if (mc_msg_grow(&parser->msg, tlen+1, (void**) &parser->msg->key.str) != 0) {
    parser->error = parser_out_of_memory;
    fgoto error_handling;
  }
  parser->msg->key.len = tlen;
  strncpy(parser->msg->key.str, ts, tlen);
  parser->msg->key.str[tlen] = '\0';
};

lease_token = ('-'? digit+) >start_token %finish_token %{
  parser->msg->lease_id = strtol(ts, NULL, 10);
};
casid = digit+ >start_token %finish_token %{
  parser->msg->cas = strtoll(ts, NULL, 10);
};
flags = digit+ >start_token %finish_token %{
  parser->msg->flags = strtoll(ts, NULL, 10);
};
exptime = ('-'? digit+) >start_token %finish_token %{
  parser->msg->exptime = strtol(ts, NULL, 10);
};
delta = digit+ >start_token %finish_token %{
  parser->msg->delta = strtoll(ts, NULL, 10);
};
number = digit+ >start_token %finish_token %{
  parser->msg->number = strtoll(ts, NULL, 10);
};
noreply = 'noreply' %{
  parser->msg->noreply = true;
};

value_bytes = digit+ >start_token %finish_token %{
  /* prepare for receiving data */
  size_t len = parser->resid = strtol(ts, NULL, 10);
  parser->off = 0;

  if (mc_msg_grow(&parser->msg, len + 1,
                  (void**) &parser->msg->value.str) != 0) {
    parser->error = parser_out_of_memory;
    fgoto error_handling;
  }
  parser->msg->value.str[len] = '\0';
  parser->msg->value.len = len;
};
cas_unique = digit+ >start_token %finish_token %{
  parser->msg->cas = strtoull(ts, NULL, 10);
};
dec_octet = digit{1,3};
ipv4_addr = dec_octet "." dec_octet "." dec_octet "." dec_octet;
ipv6_addr = (":" | xdigit)+ ipv4_addr?;
ip_addr = (ipv4_addr | ipv6_addr) >start_token %finish_token %{
  ts[tlen]='\0';
  parser->msg->ipv = 0;
  if (strchr(ts, ':') == NULL) {
    if (inet_pton(AF_INET, ts, &parser->msg->ip_addr) > 0)
      parser->msg->ipv = 4;
  } else {
    if (inet_pton(AF_INET6, ts, &parser->msg->ip_addr) > 0)
      parser->msg->ipv = 6;
  }
};
age_unknown = 'unknown' %{ parser->msg->number = -1;};
action rx_data {
  /* the binary data of a specified isn't really regular, so we drop out of
     the state machine and read it, then come back in to finish the
     "\r\nEND\r\n". */
  parser->parser_state = parser_body;
  fbreak;
}

# replies

VALUE = 'VALUE' % { parser->msg->result = mc_res_found; };

hit = VALUE ' '+ key_skip ' '+ flags ' '+ value_bytes (' '+ cas_unique)? nl
  @rx_data nl;

get_reply = (hit? >{ parser->msg->result = mc_res_notfound; } 'END' fin);
lease_get = ((hit |
  ('LVALUE' ' '+ key_skip ' '+ lease_token ' '+ flags ' '+ value_bytes (' '+ cas_unique)? nl
   @rx_data nl
   @{ parser->msg->result = mc_res_notfound; }))
  'END' fin);
META = 'META' % {parser->msg->result = mc_res_found;};
mhit = META ' '+ key_skip ' '+ 'age:' ' '* (number | age_unknown) ';' ' '*
  'exptime:' ' '* exptime ';' ' '* 'from:' ' '* (ip_addr|'unknown') ';' ' '*
  'is_transient:' ' '* flags ' '* nl;
metaget_reply = (mhit? >{ parser->msg->result = mc_res_notfound; } 'END' fin);
reply_set = storage;
reply_add = storage;
reply_replace = storage;
reply_lease_set = storage;
reply_append = storage;
reply_prepend = storage;
reply_cas = storage;
delete_reply = deleted | not_found;

stat_name = (any+ -- (cntrl | space)) >start_token %finish_token
%{
  if (parser->msg->stats == NULL) {
    FBI_ASSERT(parser->msg->number == 0);
    size_t stats_size = sizeof(nstring_t) * 2 * MAX_NUM_STATS;
    if (mc_msg_grow(&parser->msg, stats_size, (void**) &parser->msg->stats)) {
      parser->error = parser_out_of_memory;
      fgoto error_handling;
    }
    memset(parser->msg->stats, 0, stats_size);
  }

  if (parser->msg->number < MAX_NUM_STATS) {
    parser->msg->number++;

    int idx = (parser->msg->number * 2) - 2;
    if (mc_msg_grow(&parser->msg, tlen + 1,
                    (void**) &parser->msg->stats[idx].str) != 0) {
      parser->error = parser_out_of_memory;
      fgoto error_handling;
    }
    parser->msg->stats[idx].len = tlen;
    parser->msg->stats[idx].str[tlen] = '\0';
    strncpy(parser->msg->stats[idx].str, ts, tlen);
  }
};

stat_value = (any++ -- cntrl) > start_token %finish_token
%{
  if (parser->msg->number <= MAX_NUM_STATS) {
    int idx = (parser->msg->number * 2) - 1;
    if (mc_msg_grow(&parser->msg, tlen + 1,
                    (void**) &parser->msg->stats[idx].str) != 0) {
      parser->error = parser_out_of_memory;
      fgoto error_handling;
    }

    parser->msg->stats[idx].len = tlen;
    parser->msg->stats[idx].str[tlen] = '\0';
    strncpy(parser->msg->stats[idx].str, ts, tlen);
  }
};

stats_reply = (('STAT ' @{ parser->msg->result = mc_res_ok; }
                stat_name ' '+ stat_value nl)* 'END' fin);
ok = ('OK' @{ parser->msg->result = mc_res_ok; } fin);
version_reply = ('VERSION ' (any* -- ('\r' | '\n')) >start_token %finish_token
  %{
    // use new_msg because msg stays live if realloc fails and can leak
    mc_msg_t *new_msg = mc_msg_realloc(parser->msg,
                                       parser->msg->_extra_size + tlen + 1);
    if (new_msg == NULL) {
      parser->error = parser_out_of_memory;
      fgoto error_handling;
    }
    parser->msg = new_msg;
    parser->msg->result = mc_res_ok;
    char* version = parser->msg->value.str = (char *) &parser->msg[1];
    parser->msg->value.len = tlen;
    strncpy(version, ts, tlen);
    version[tlen] = '\0';
  }
  fin);

reply := get_reply | lease_get | metaget_reply |
        reply_set | reply_add | reply_replace | reply_lease_set |
        reply_append | reply_prepend | reply_cas | delete_reply |
        arithmetic_reply |
        stats_reply | ok | version_reply | error;

# requests

set = 'set' @ { parser->msg->op = mc_op_set; };
add = 'add' @ { parser->msg->op = mc_op_add; };
replace = 'replace' @ { parser->msg->op = mc_op_replace; };
leaseset = 'lease-set' @ { parser->msg->op = mc_op_lease_set; };
append = 'append' @ { parser->msg->op = mc_op_append; };
prepend = 'prepend' @ { parser->msg->op = mc_op_prepend; };

set_op = set | add | replace | append | prepend;

delete_op = 'delete' @ { parser->msg->op = mc_op_delete; };

get_op = 'get' @ { parser->msg->op = mc_op_get; } |
         'gets' @ {parser->msg->op = mc_op_gets; } |
         'lease-get' @ { parser->msg->op = mc_op_lease_get; } |
         'metaget' @ { parser->msg->op = mc_op_metaget; } ;

arithmetic_op = 'incr' @ { parser->msg->op = mc_op_incr; } |
                'decr' @ { parser->msg->op = mc_op_decr; };

version_op = 'version' @ { parser->msg->op = mc_op_version; };

quit_op = 'quit' @ { parser->msg->op = mc_op_quit;
                     parser->msg->noreply = true; };

stats_op = 'stats' @ { parser->msg->op = mc_op_stats; };

exec_op = ('exec' | 'admin') @ { parser->msg->op = mc_op_exec; };

shutdown_op = 'shutdown' @ { parser->msg->op = mc_op_shutdown; };

flush_all_op = 'flush_all' @ { parser->msg->op = mc_op_flushall; };
flush_regex_op = 'flush_regex' @ { parser->msg->op = mc_op_flushre; };

cas = 'cas' %{
  parser->msg->op = mc_op_cas;
} ' '+ key ' '+ flags ' '+ exptime ' '+ value_bytes ' '+ casid ' '* nl
  @rx_data fin;

store = (set_op ' '+ key | leaseset ' '+ key ' '+ lease_token) ' '+ flags ' '+ exptime ' '+ value_bytes (' '+ noreply)? ' '* nl
  @rx_data fin;

get = get_op (' '+ key %get_req_is_ready)+ ' '* fin >{
  parser->msg->op = mc_op_end;
};

arithmetic = arithmetic_op ' '+ key ' '+ delta (' '+ noreply)? ' '* fin;

delete = delete_op ' '+ key (' '+ exptime)? (' '+ noreply)? ' '* fin;

quit = quit_op ' '* fin;

version = version_op ' '* fin;

stats = stats_op (' '+ multi_token)? ' '* fin;

exec = exec_op ' '+ multi_token fin;

shutdown = shutdown_op (' '+ number)? ' '* fin;

flush_all = flush_all_op (' '+ number)? ' '* fin;

flush_regex = flush_regex_op ' ' key ' '* fin;

request := get | store | cas | delete | arithmetic
  | quit | version | stats | shutdown | flush_all | flush_regex | exec;

request_reply := get | store | cas | delete | arithmetic
  | quit | version | stats | shutdown | flush_all | flush_regex | exec |
  get_reply | lease_get | metaget_reply |
  reply_set | reply_add | reply_replace | reply_lease_set |
  reply_append | reply_prepend | reply_cas | delete_reply |
  arithmetic_reply |
  stats_reply | ok | version_reply | error;


write data;
}%%

int get_reply_ragel_start_state() {
  return ascii_en_reply;
}

int get_request_ragel_start_state() {
  return ascii_en_request;
}

int get_request_reply_ragel_start_state() {
  return ascii_en_request_reply;
}

/** ascii reply parsing
    This is the real workhorse. It consumes buf, resuming the partial reply if
    one exists. As each reply is fully received, we call mcc_req_complete().
    If there is a parse error or remote error, we handle that as well. If
    there is a parse error or unrecoverable remote error, we call
    mcc_on_down() and the rest of the buffer is ignored.
    @return 0 on error */
int _on_ascii_rx(mc_parser_t* parser, char* buf, size_t nbuf) {
  FBI_ASSERT(parser);
  FBI_ASSERT(buf);
  dbg_fentry("parser=%p nbuf=%lu", parser, nbuf);

  int success = 0;
  bool isError = false;

  // ragel variables
  int cs = ascii_error; // current state
  char* p = buf;
  char* pe = buf + nbuf;
  char* eof = NULL;
  %% write init nocs;

  char* ts = NULL; // token start
  size_t tlen = 0; // token length

  int nmsgs = 0;

  /* each iteration of this loop processes one msg, or in the case of value
     msgs the header of the msg, the body of the msg, or the tail of
     the msg. If we reach the end of buf before the end of a msg (whether
     in data or header/tail), we have a partial read and will resume when the
     next packet arrives. */
  while (p < pe) {
    /* sanity check: if memcached has a bug, or we somehow lost a msg, log
       the whatever's left and disconnect */
    if (parser->parser_state == parser_idle) {
      dbg_error("Unexpected msg '%s'", p);
      parser->parse_error(parser->context, parser->error);
      break;
    }

    FBI_ASSERT(parser->parser_state == parser_msg_header ||
           parser->parser_state == parser_body ||
           parser->parser_state == parser_partial);

    /* prepare to resume. If we're not in the middle of a msg, these will
       not be used */
    cs = parser->ragel_state;

    if (parser->parser_state == parser_msg_header) {
      if (parser->msg != NULL) {
        dbg_error("parser->msg already exists at start of new message. "
                  "Probable memory leak.");
      }

      // new msg
      parser->msg = mc_msg_new(0);
      if (parser->msg == NULL) {
        parser->error = parser_out_of_memory;
        parser->resid = parser->off = 0;
        parser->parse_error(parser->context, parser->error);
        success = 0;
        goto epilogue;
      }

      cs = parser->ragel_start_state;
      parser->parser_state = parser_partial;
    }
    FBI_ASSERT(parser->msg);

    dbg_low("resid=%lu msg=%p parser_state=%s cs=%d",
            pe - p, parser->msg,
            parser_state_to_string(parser->parser_state), cs);

    if (parser->parser_state == parser_body) {
      FBI_ASSERT(pe >= p);
      size_t n = MIN(parser->resid, (size_t)(pe - p));
      memcpy(&parser->msg->value.str[parser->off], p, n);
      p += n;
      parser->off += n;
      FBI_ASSERT(parser->resid >= n);
      parser->resid -= n;
      if (parser->resid == 0) {
        FBI_ASSERT(parser->off == parser->msg->value.len);
        parser->parser_state = parser_partial;
      }
    } else {
      if (parser->partial_token) {
        // we're in the middle of a token
        ts = p;
      } else {
        ts = NULL;
      }

      /* this is where ragel emits the state machine code */
      %% write exec;

      // If we encountered 'ERROR' reply, fail all further data.
      if (isError) {
        goto epilogue;
      }

      /* if msg == NULL, we finished the msg */
      if (parser->msg) {
        parser->ragel_state = cs;
      } else {
        /* sentinel values */
        parser->ragel_state = ascii_error;
      }

      if (ts && parser->partial_token) {
        // we're in the middle of a token, stuff it in our cheeks
        int n = p - ts;
        if(mc_parser_ensure_tbuf(parser, n) == 0) {
          strncpy(parser->te, ts, n);
          parser->te += n;
        } else {
          if (parser->in_key) {
            parser->bad_key = 1;
          } else {
            // fake fgoto error here
            parser->error = parser_malformed_request;
            cs = ascii_error;
          }
        }
      }

      if (cs == ascii_error) {
        if (parser->msg != NULL) {
          mc_msg_decref(parser->msg);
          parser->msg = NULL;
        }
        parser->resid = parser->off = 0;
        parser->parse_error(parser->context, parser->error);
        goto epilogue;
      }
    }

    /* We may have adjusted pe and eof to simulate EOF at the end of a msg.
       Restore them for continued parsing of additional msgs. */
    pe = buf + nbuf;
    eof = NULL;
  }

  success = 1;

epilogue:
  dbg_fexit("%d msgs finished. parser_state=%s cs=%d",
            nmsgs, parser_state_to_string(parser->parser_state), cs);
  return success;
}

%%{
# vim: ft=ragel
}%%
