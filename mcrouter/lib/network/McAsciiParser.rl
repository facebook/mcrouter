/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "mcrouter/lib/network/McAsciiParser.h"

#include <arpa/inet.h>

#include "mcrouter/lib/mc/msg.h"
#include "mcrouter/lib/McOperation.h"
#include "mcrouter/lib/McReply.h"

namespace facebook { namespace memcache {

/**
 * %%{}%% blocks are going to be processed by Ragel.
 * A lot of different building blocks are used here, their complete
 * documentation can be found on the official webpage of Ragel.
 * The most important constructs are actions and code snippets.
 * There are 4 base types of them used here:
 *   >name or >{} - execute action with name 'name', or code snippet before
 *                  entering machine.
 *   $name or ${} - on each transition of machine.
 *   %name or %{} - on each transition from machine via final state.
 *   @name or @{} - on each transition into final state.
 */

%%{
machine mc_ascii_common;

# Define binding to class member variables.
variable p p_;
variable pe pe_;
variable eof eof_;
variable cs savedCs_;

# Action that initializes data parsing.
action value_data {
  // We have value field, so emplace IOBuf for value.
  reply.valueData_.emplace();
  // But we don't need to put anything into it, if the value is empty.
  if (remainingIOBufLength_) {
    // Copy IOBuf for part of (or whole) value.
    size_t offset = p_ - reinterpret_cast<const char*>(buffer.data()) + 1;
    size_t toUse = std::min(buffer.length() - offset, remainingIOBufLength_);
    buffer.cloneOneInto(reply.valueData_.value());
    // Adjust buffer pointers.
    reply.valueData_->trimStart(offset);
    reply.valueData_->trimEnd(buffer.length() - offset - toUse);

    remainingIOBufLength_ -= toUse;
    // Move the state machine to the proper character.
    p_ += toUse;

    // Now if we didn't had enough data, we need to preallocate second piece
    // for remaining buffer, move into proper state and break from machine.
    if (remainingIOBufLength_) {
      auto secondPiece = folly::IOBuf::createCombined(remainingIOBufLength_);
      currentIOBuf_ = secondPiece.get();
      reply.valueData_->appendChain(std::move(secondPiece));
      fbreak;
    }
  }
}

# Value for which we do not know the length (e.g. version, error message).
action str_value {
  if (reply.valueData_) {
    // Take the last IOBuf in chain.
    auto tail = reply.valueData_->prev();
    appendCurrentCharTo(buffer, *tail);
  } else {
    // Emplace IOBuf.
    reply.valueData_.emplace();
    // Copy current IOBuf.
    buffer.cloneOneInto(reply.valueData_.value());
    size_t offset = p_ - reinterpret_cast<const char*>(buffer.data());
    reply.valueData_->trimStart(offset);
    reply.valueData_->trimEnd(buffer.length() - offset - 1 /* current char */);
  }
}

# Resets current value, used for errors.
action reset_value {
  reply.valueData_.clear();
}

new_line = '\r'? '\n';

# End of message.
msg_end = new_line @ {
  // Message is complete, so exit the state machine and return to the caller.
  state_ = State::COMPLETE;
  fbreak;
};

# Key that we do not want to store.
skip_key = (any+ -- (cntrl | space));

# Unsigned integer value.
uint = digit+ > { currentUInt_ = 0; } ${
  currentUInt_ = currentUInt_ * 10 + (fc - '0');
};

# Single fields with in-place parsing.
flags = uint %{
  reply.setFlags(currentUInt_);
};

value_bytes = uint %{
  remainingIOBufLength_ = static_cast<size_t>(currentUInt_);
};

cas_id = uint %{
  reply.setCas(currentUInt_);
};

delta = uint %{
  reply.setDelta(currentUInt_);
};

lease_token = uint %{
  // NOTE: we don't support -1 lease token.
  reply.setLeaseToken(currentUInt_);
};

error_code = uint %{
  reply.setAppSpecificErrorCode(static_cast<uint32_t>(currentUInt_));
};

# Common storage replies.
not_found = 'NOT_FOUND' @{ reply.setResult(mc_res_notfound); };
deleted = 'DELETED' @{ reply.setResult(mc_res_deleted); };

VALUE = 'VALUE' % { reply.setResult(mc_res_found); };

hit = VALUE ' '+ skip_key ' '+ flags ' '+ value_bytes new_line @value_data
      new_line;
gets_hit = VALUE ' '+ skip_key ' '+ flags ' '+ value_bytes ' '+ cas_id
           new_line @value_data new_line;

# Errors
command_error = 'ERROR' @{
  // This is unexpected error reply, just put ourself into error state.
  state_ = State::ERROR;
  currentErrorDescription_ = "ERROR reply received from server.";
  fbreak;
};

error_message = (any* -- ('\r' | '\n')) >reset_value >{ stripped_ = false; }
                $str_value;

server_error = 'SERVER_ERROR' (' ' error_code ' ')? ' '? error_message
               %{
                 if (reply.appSpecificErrorCode() == SERVER_ERROR_BUSY) {
                   reply.setResult(mc_res_busy);
                 } else {
                   reply.setResult(mc_res_remote_error);
                 }
               };

client_error = 'CLIENT_ERROR' (' ' error_code ' ')? ' '? error_message
               %{ reply.setResult(mc_res_client_error); };

error = command_error | server_error | client_error;
}%%

// McGet reply.
%%{
machine mc_ascii_get_reply;
include mc_ascii_common;

get = hit? >{ reply.setResult(mc_res_notfound); } 'END';
get_reply := (get | error) msg_end;
write data;
}%%

template<>
void McAsciiParser::consumeMessage<McReply, McOperation<mc_op_get>>(
    folly::IOBuf& buffer) {
  McReply& reply = currentMessage_.get<McReply>();
  %%{
    machine mc_ascii_get_reply;
    write init nocs;
    write exec;
  }%%
}

// McGets reply.
%%{
machine mc_ascii_gets_reply;
include mc_ascii_common;

gets = gets_hit? >{ reply.setResult(mc_res_notfound); } 'END';
gets_reply := (gets | error) msg_end;
write data;
}%%

template<>
void McAsciiParser::consumeMessage<McReply, McOperation<mc_op_gets>>(
    folly::IOBuf& buffer) {
  McReply& reply = currentMessage_.get<McReply>();
  %%{
    machine mc_ascii_gets_reply;
    write init nocs;
    write exec;
  }%%
}

// McLeaseGet reply.
%%{
machine mc_ascii_lease_get_reply;
include mc_ascii_common;

# FIXME, we should return mc_res_foundstale or mc_res_notfoundhot.
lvalue = 'LVALUE' ' '+ skip_key ' '+ lease_token ' '+ flags ' '+ value_bytes
         new_line @value_data new_line
         @{ reply.setResult(mc_res_notfound); };

lease_get = (hit | lvalue) 'END';
lease_get_reply := (lease_get | error) msg_end;

write data;
}%%

template<>
void McAsciiParser::consumeMessage<McReply, McOperation<mc_op_lease_get>>(
    folly::IOBuf& buffer) {
  McReply& reply = currentMessage_.get<McReply>();
  %%{
    machine mc_ascii_lease_get_reply;
    write init nocs;
    write exec;
  }%%
}

// McStorage reply.
%%{
machine mc_ascii_storage_reply;
include mc_ascii_common;

stored = 'STORED' @{ reply.setResult(mc_res_stored); };
stale_stored = 'STALE_STORED' @{ reply.setResult(mc_res_stalestored); };
not_stored = 'NOT_STORED' @{ reply.setResult(mc_res_notstored); };
exists = 'EXISTS' @{ reply.setResult(mc_res_exists); };

storage = stored | stale_stored | not_stored | exists | not_found | deleted;
storage_reply := (storage | error) msg_end;

write data;
}%%

void McAsciiParser::consumeStorageReplyCommon(folly::IOBuf& buffer) {
  McReply& reply = currentMessage_.get<McReply>();
  %%{
    machine mc_ascii_storage_reply;
    write init nocs;
    write exec;
  }%%
}

// McArithm reply.
%%{
machine mc_ascii_arithm_reply;
include mc_ascii_common;

arithm = not_found | (delta @{ reply.setResult(mc_res_stored); }) ' '*;
arithm_reply := (arithm | error) msg_end;

write data;
}%%

void McAsciiParser::consumeArithmReplyCommon(folly::IOBuf& buffer) {
  McReply& reply = currentMessage_.get<McReply>();
  %%{
    machine mc_ascii_arithm_reply;
    write init nocs;
    write exec;
  }%%
}

// McVersion reply.
%%{
machine mc_ascii_version_reply;
include mc_ascii_common;

version = 'VERSION ' @{ reply.setResult(mc_res_ok); }
          (any* -- ('\r' | '\n')) $str_value;
version_reply := (version | error) msg_end;

write data;
}%%

template<>
void McAsciiParser::consumeMessage<McReply, McOperation<mc_op_version>>(
    folly::IOBuf& buffer) {
  McReply& reply = currentMessage_.get<McReply>();
  %%{
    machine mc_ascii_version_reply;
    write init nocs;
    write exec;
  }%%
}

// McDelete reply.
%%{
machine mc_ascii_delete_reply;
include mc_ascii_common;

delete = deleted | not_found;
delete_reply := (delete | error) msg_end;

write data;
}%%

template<>
void McAsciiParser::consumeMessage<McReply, McOperation<mc_op_delete>>(
    folly::IOBuf& buffer) {
  McReply& reply = currentMessage_.get<McReply>();
  %%{
    machine mc_ascii_delete_reply;
    write init nocs;
    write exec;
  }%%
}

//McMetaget reply.
%%{
machine mc_ascii_metaget_reply;
include mc_ascii_common;

age = uint %{
  reply.setNumber(static_cast<uint32_t>(currentUInt_));
};
age_unknown = 'unknown' %{
  reply.setNumber(static_cast<uint32_t>(-1));
};
exptime = uint %{
  reply.setExptime(static_cast<uint32_t>(currentUInt_));
};
ip_addr = (xdigit | '.' | ':')+ $str_value %{
  // Max ip address length is INET6_ADDRSTRLEN - 1 chars.
  if (reply.valueData_->computeChainDataLength() < INET6_ADDRSTRLEN) {
    char addr[INET6_ADDRSTRLEN] = {0};
    reply.valueData_->coalesce();
    memcpy(addr, reply.valueData_->data(), reply.valueData_->length());
    mcMsgT->ipv = 0;
    if (strchr(addr, ':') == nullptr) {
      if (inet_pton(AF_INET, addr, &mcMsgT->ip_addr) > 0) {
        mcMsgT->ipv = 4;
      }
    } else {
      if (inet_pton(AF_INET6, addr, &mcMsgT->ip_addr) > 0) {
        mcMsgT->ipv = 6;
      }
    }
  }
};
meta = 'META' % { reply.setResult(mc_res_found); };
mhit = meta ' '+ skip_key ' '+ 'age:' ' '* (age | age_unknown) ';' ' '*
  'exptime:' ' '* exptime ';' ' '* 'from:' ' '* (ip_addr|'unknown') ';' ' '*
  'is_transient:' ' '* flags ' '* new_line;
metaget = mhit? >{ reply.setResult(mc_res_notfound); } 'END' msg_end;
metaget_reply := (metaget | error) msg_end;

write data;
}%%
template<>
void McAsciiParser::consumeMessage<McReply, McOperation<mc_op_metaget>>(
    folly::IOBuf& buffer) {
  McReply& reply = currentMessage_.get<McReply>();
  mc_msg_t* mcMsgT = const_cast<mc_msg_t*>(reply.msg_.get());
  %%{
    machine mc_ascii_metaget_reply;
    write init nocs;
    write exec;
  }%%
}

// McFlushAll reply.
%%{
machine mc_ascii_flushall_reply;
include mc_ascii_common;

flushall = 'OK' $ { reply.setResult(mc_res_ok); };
flushall_reply := (flushall | error) msg_end;

write data;
}%%

template<>
void McAsciiParser::consumeMessage<McReply, McOperation<mc_op_flushall>>(
    folly::IOBuf& buffer) {
  McReply& reply = currentMessage_.get<McReply>();
  %%{
    machine mc_ascii_flushall_reply;
    write init nocs;
    write exec;
  }%%
}

template<>
void McAsciiParser::initializeReplyParser<McOperation<mc_op_get>, McRequest>() {
  initializeCommon();
  savedCs_ = mc_ascii_get_reply_en_get_reply;
  errorCs_ = mc_ascii_get_reply_error;
  consumer_ = &McAsciiParser::consumeMessage<McReply, McOperation<mc_op_get>>;
}

template<>
void McAsciiParser::initializeReplyParser<McOperation<mc_op_gets>, McRequest>() {
  initializeCommon();
  savedCs_ = mc_ascii_gets_reply_en_gets_reply;
  errorCs_ = mc_ascii_gets_reply_error;
  consumer_ = &McAsciiParser::consumeMessage<McReply, McOperation<mc_op_gets>>;
}

template<>
void McAsciiParser::initializeReplyParser<McOperation<mc_op_lease_get>, McRequest>() {
  initializeCommon();
  savedCs_ = mc_ascii_lease_get_reply_en_lease_get_reply;
  errorCs_ = mc_ascii_lease_get_reply_error;
  consumer_ = &McAsciiParser::consumeMessage<McReply,
                                             McOperation<mc_op_lease_get>>;
}

template<>
void McAsciiParser::initializeReplyParser<McOperation<mc_op_set>, McRequest>() {
  initializeStorageReplyCommon();
}

template<>
void McAsciiParser::initializeReplyParser<McOperation<mc_op_add>, McRequest>() {
  initializeStorageReplyCommon();
}

template<>
void McAsciiParser::initializeReplyParser<McOperation<mc_op_replace>, McRequest>() {
  initializeStorageReplyCommon();
}

template<>
void McAsciiParser::initializeReplyParser<McOperation<mc_op_lease_set>, McRequest>() {
  initializeStorageReplyCommon();
}

template<>
void McAsciiParser::initializeReplyParser<McOperation<mc_op_cas>, McRequest>() {
  initializeStorageReplyCommon();
}

template<>
void McAsciiParser::initializeReplyParser<McOperation<mc_op_incr>, McRequest>() {
  initializeArithmReplyCommon();
}

template<>
void McAsciiParser::initializeReplyParser<McOperation<mc_op_decr>, McRequest>() {
  initializeArithmReplyCommon();
}

template<>
void McAsciiParser::initializeReplyParser<McOperation<mc_op_version>, McRequest>() {
  initializeCommon();
  savedCs_ = mc_ascii_version_reply_en_version_reply;
  errorCs_ = mc_ascii_version_reply_error;
  consumer_ = &McAsciiParser::consumeMessage<McReply,
                                             McOperation<mc_op_version>>;
}

template<>
void McAsciiParser::initializeReplyParser<McOperation<mc_op_delete>, McRequest>() {
  initializeCommon();
  savedCs_ = mc_ascii_delete_reply_en_delete_reply;
  errorCs_ = mc_ascii_delete_reply_error;
  consumer_ = &McAsciiParser::consumeMessage<McReply,
                                             McOperation<mc_op_delete>>;
}

template<>
void McAsciiParser::initializeReplyParser<McOperation<mc_op_metaget>, McRequest>() {
  initializeCommon();
  // Since mc_op_metaget has A LOT of specific fields, just create McMsgRef for
  // now.
  McReply& reply = currentMessage_.get<McReply>();
  reply.msg_ = createMcMsgRef();
  savedCs_ = mc_ascii_metaget_reply_en_metaget_reply;
  errorCs_ = mc_ascii_metaget_reply_error;
  consumer_ = &McAsciiParser::consumeMessage<McReply,
                                             McOperation<mc_op_metaget>>;
}

template<>
void McAsciiParser::initializeReplyParser<McOperation<mc_op_flushall>, McRequest>() {
  initializeCommon();
  savedCs_ = mc_ascii_flushall_reply_en_flushall_reply;
  errorCs_ = mc_ascii_flushall_reply_error;
  consumer_ = &McAsciiParser::consumeMessage<McReply,
                                             McOperation<mc_op_flushall>>;
}

void McAsciiParser::initializeArithmReplyCommon() {
  initializeCommon();
  savedCs_ = mc_ascii_arithm_reply_en_arithm_reply;
  errorCs_ = mc_ascii_arithm_reply_error;
  consumer_ = &McAsciiParser::consumeArithmReplyCommon;
}

void McAsciiParser::initializeStorageReplyCommon() {
  initializeCommon();
  savedCs_ = mc_ascii_storage_reply_en_storage_reply;
  errorCs_ = mc_ascii_storage_reply_error;
  consumer_ = &McAsciiParser::consumeStorageReplyCommon;
}

void McAsciiParser::initializeCommon() {
  assert(state_ == State::UNINIT);

  currentUInt_ = 0;
  currentIOBuf_ = nullptr;
  remainingIOBufLength_ = 0;
  state_ = State::PARTIAL;

  currentMessage_.emplace<McReply>();
}

}}  // facebook::memcache
