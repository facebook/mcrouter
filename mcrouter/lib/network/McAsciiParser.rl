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
variable cs savedCs_;

# Action that initializes/performs data parsing for replies.
action reply_value_data {
  // We have value field, so emplace IOBuf for value.
  message.valueData_.emplace();
  if (!readValue(buffer, message.valueData_.value())) {
    fbreak;
  }
}

# Action that initializes/performs data parsing for requests.
action req_value_data {
  if (!readValue(buffer, message.valueData_)) {
    fbreak;
  }
}

# Value for which we do not know the length (e.g. version, error message).
# Can be used only for replies.
action str_value {
  if (message.valueData_) {
    // Append to the last IOBuf in chain.
    appendCurrentCharTo(buffer, *message.valueData_, p_);
  } else {
    // Emplace IOBuf.
    // TODO: allocate IOBuf and clone it in one operation.
    message.valueData_.emplace();
    initFirstCharIOBuf(buffer, message.valueData_.value(), p_);
  }
}

# Resets current value, used for errors.
action reset_value {
  message.valueData_.clear();
}

# For requests only.
noreply = 'noreply' %{
  noreply_ = true;
};

new_line = '\r'? '\n';

# End of multi op request (get, gets, lease-get, metaget).
multi_op_end = new_line @{
  callback_->multiOpEnd();
  finishReq();
  fbreak;
};

# End of message.
msg_end = new_line @{
  // Message is complete, so exit the state machine and return to the caller.
  state_ = State::COMPLETE;
  fbreak;
};

# Key that we do not want to store.
skip_key = (any+ -- (cntrl | space));

action key_start {
  currentKey_.clear();
  keyPieceStart_ = p_;
}

action key_end {
  appendKeyPiece(buffer, currentKey_, keyPieceStart_, p_);
  keyPieceStart_ = nullptr;
  currentKey_.coalesce();
}

# Key that we want to store.
key = (any+ -- (cntrl | space)) >key_start %key_end %{
  message.setKey(std::move(currentKey_));
};

multi_token = (print+ -- ( '\r' | '\n' )) >key_start %key_end %{
  // Trim string.
  while (currentKey_.length() > 0 && isspace(*currentKey_.data())) {
    currentKey_.trimStart(1);
  }
  while (currentKey_.length() > 0 && isspace(*(currentKey_.tail() - 1))) {
    currentKey_.trimEnd(1);
  }
  message.setKey(std::move(currentKey_));
};

# Unsigned integer value.
uint = digit+ > { currentUInt_ = 0; } ${
  currentUInt_ = currentUInt_ * 10 + (fc - '0');
};

negative = '-' >{
  negative_ = true;
};

# Single fields with in-place parsing.
flags = uint %{
  message.setFlags(currentUInt_);
};

exptime = uint %{
  message.setExptime(static_cast<uint32_t>(currentUInt_));
};

exptime_req = negative? uint %{
  auto value = static_cast<int32_t>(currentUInt_);
  message.setExptime(negative_ ? -value : value);
};

number = uint %{
  message.setNumber(static_cast<uint32_t>(currentUInt_));
};

value_bytes = uint %{
  remainingIOBufLength_ = static_cast<size_t>(currentUInt_);
};

cas_id = uint %{
  message.setCas(currentUInt_);
};

delta = uint %{
  message.setDelta(currentUInt_);
};

lease_token = uint %{
  // NOTE: we don't support -1 lease token.
  message.setLeaseToken(currentUInt_);
};

error_code = uint %{
  message.setAppSpecificErrorCode(static_cast<uint32_t>(currentUInt_));
};

# Common storage replies.
not_found = 'NOT_FOUND' @{ message.setResult(mc_res_notfound); };
deleted = 'DELETED' @{ message.setResult(mc_res_deleted); };
touched = 'TOUCHED' @{ message.setResult(mc_res_touched); };

VALUE = 'VALUE' % { message.setResult(mc_res_found); };

hit = VALUE ' '+ skip_key ' '+ flags ' '+ value_bytes new_line @reply_value_data
      new_line;
gets_hit = VALUE ' '+ skip_key ' '+ flags ' '+ value_bytes ' '+ cas_id
           new_line @reply_value_data new_line;

# Errors
command_error = 'ERROR' @{
  // This is unexpected error reply, just put ourself into error state.
  state_ = State::ERROR;
  currentErrorDescription_ = "ERROR reply received from server.";
  fbreak;
};

error_message = (any* -- ('\r' | '\n')) >reset_value
                $str_value;

server_error = 'SERVER_ERROR' (' ' error_code ' ')? ' '? error_message
               %{
                 if (message.appSpecificErrorCode() == SERVER_ERROR_BUSY) {
                   message.setResult(mc_res_busy);
                 } else {
                   message.setResult(mc_res_remote_error);
                 }
               };

client_error = 'CLIENT_ERROR' (' ' error_code ' ')? ' '? error_message
               %{ message.setResult(mc_res_client_error); };

error = command_error | server_error | client_error;
}%%

namespace {

/**
 * Trim IOBuf to reference only data from range [posStart, posEnd).
 */
inline void trimIOBufToRange(folly::IOBuf& buffer, const char* posStart,
                             const char* posEnd) {
  buffer.trimStart(posStart - reinterpret_cast<const char*>(buffer.data()));
  buffer.trimEnd(buffer.length() - (posEnd - posStart));
}

/**
 * Append piece of IOBuf in range [posStart, posEnd) to destination IOBuf.
 */
inline void appendKeyPiece(const folly::IOBuf& from, folly::IOBuf& to,
                           const char* posStart, const char* posEnd) {
  // No need to process empty piece.
  if (UNLIKELY(posEnd == posStart)) {
    return;
  }

  if (LIKELY(to.length() == 0)) {
    from.cloneOneInto(to);
    trimIOBufToRange(to, posStart, posEnd);
  } else {
    auto nextPiece = from.cloneOne();
    trimIOBufToRange(*nextPiece, posStart, posEnd);
    to.prependChain(std::move(nextPiece));
  }
}

inline void initFirstCharIOBuf(const folly::IOBuf& from, folly::IOBuf& to,
                               const char* pos) {
  // Copy current IOBuf.
  from.cloneOneInto(to);
  trimIOBufToRange(to, pos, pos + 1);
}

inline void appendCurrentCharTo(const folly::IOBuf& from, folly::IOBuf& to,
                                const char* pos) {
  // If it is just a next char in the same memory chunk, just append it.
  // Otherwise we need to append new IOBuf.
  if (to.prev()->data() + to.prev()->length() ==
        reinterpret_cast<const void*>(pos) && to.prev()->tailroom() > 0) {
    to.prev()->append(1);
  } else {
    auto nextPiece = from.cloneOne();
    trimIOBufToRange(*nextPiece, pos, pos + 1);
    to.prependChain(std::move(nextPiece));
  }
}

}  // anonymous

// McGet reply.
%%{
machine mc_ascii_get_reply;
include mc_ascii_common;

get = hit? >{ message.setResult(mc_res_notfound); } 'END';
get_reply := (get | error) msg_end;
write data;
}%%

template<>
void McClientAsciiParser::consumeMessage<McReply, McOperation<mc_op_get>>(
    folly::IOBuf& buffer) {
  McReply& message = currentMessage_.get<McReply>();
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

gets = gets_hit? >{ message.setResult(mc_res_notfound); } 'END';
gets_reply := (gets | error) msg_end;
write data;
}%%

template<>
void McClientAsciiParser::consumeMessage<McReply, McOperation<mc_op_gets>>(
    folly::IOBuf& buffer) {
  McReply& message = currentMessage_.get<McReply>();
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
         new_line @reply_value_data new_line
         @{ message.setResult(mc_res_notfound); };

lease_get = (hit | lvalue) 'END';
lease_get_reply := (lease_get | error) msg_end;

write data;
}%%

template<>
void McClientAsciiParser::consumeMessage<McReply, McOperation<mc_op_lease_get>>(
    folly::IOBuf& buffer) {
  McReply& message = currentMessage_.get<McReply>();
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

stored = 'STORED' @{ message.setResult(mc_res_stored); };
stale_stored = 'STALE_STORED' @{ message.setResult(mc_res_stalestored); };
not_stored = 'NOT_STORED' @{ message.setResult(mc_res_notstored); };
exists = 'EXISTS' @{ message.setResult(mc_res_exists); };

storage = stored | stale_stored | not_stored | exists | not_found | deleted;
storage_reply := (storage | error) msg_end;

write data;
}%%

void McClientAsciiParser::consumeStorageReplyCommon(folly::IOBuf& buffer) {
  McReply& message = currentMessage_.get<McReply>();
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

arithm = not_found | (delta @{ message.setResult(mc_res_stored); }) ' '*;
arithm_reply := (arithm | error) msg_end;

write data;
}%%

void McClientAsciiParser::consumeArithmReplyCommon(folly::IOBuf& buffer) {
  McReply& message = currentMessage_.get<McReply>();
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

version = 'VERSION ' @{ message.setResult(mc_res_ok); }
          (any* -- ('\r' | '\n')) $str_value;
version_reply := (version | error) msg_end;

write data;
}%%

template<>
void McClientAsciiParser::consumeMessage<McReply, McOperation<mc_op_version>>(
    folly::IOBuf& buffer) {
  McReply& message = currentMessage_.get<McReply>();
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
void McClientAsciiParser::consumeMessage<McReply, McOperation<mc_op_delete>>(
    folly::IOBuf& buffer) {
  McReply& message = currentMessage_.get<McReply>();
  %%{
    machine mc_ascii_delete_reply;
    write init nocs;
    write exec;
  }%%
}

// McTouch reply.
%%{
machine mc_ascii_touch_reply;
include mc_ascii_common;

touch = touched | not_found;
touch_reply := (touch | error) msg_end;

write data;
}%%

template <>
void McClientAsciiParser::consumeMessage<McReply, McOperation<mc_op_touch>>(
    folly::IOBuf& buffer) {
  McReply& message = currentMessage_.get<McReply>();
  %%{
    machine mc_ascii_touch_reply;
    write init nocs;
    write exec;
  }%%
}

//McMetaget reply.
%%{
machine mc_ascii_metaget_reply;
include mc_ascii_common;

age = uint %{
  message.setNumber(static_cast<uint32_t>(currentUInt_));
};
age_unknown = 'unknown' %{
  message.setNumber(static_cast<uint32_t>(-1));
};

ip_addr = (xdigit | '.' | ':')+ $str_value %{
  // Max ip address length is INET6_ADDRSTRLEN - 1 chars.
  if (message.valueData_->computeChainDataLength() < INET6_ADDRSTRLEN) {
    char addr[INET6_ADDRSTRLEN] = {0};
    message.valueData_->coalesce();
    memcpy(addr, message.valueData_->data(), message.valueData_->length());
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
meta = 'META' % { message.setResult(mc_res_found); };
mhit = meta ' '+ skip_key ' '+ 'age:' ' '* (age | age_unknown) ';' ' '*
  'exptime:' ' '* exptime ';' ' '* 'from:' ' '* (ip_addr|'unknown') ';' ' '*
  'is_transient:' ' '* flags ' '* new_line;
metaget = mhit? >{ message.setResult(mc_res_notfound); } 'END' msg_end;
metaget_reply := (metaget | error) msg_end;

write data;
}%%
template<>
void McClientAsciiParser::consumeMessage<McReply, McOperation<mc_op_metaget>>(
    folly::IOBuf& buffer) {
  McReply& message = currentMessage_.get<McReply>();
  mc_msg_t* mcMsgT = const_cast<mc_msg_t*>(message.msg_.get());
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

flushall = 'OK' $ { message.setResult(mc_res_ok); };
flushall_reply := (flushall | error) msg_end;

write data;
}%%

template<>
void McClientAsciiParser::consumeMessage<McReply, McOperation<mc_op_flushall>>(
    folly::IOBuf& buffer) {
  McReply& message = currentMessage_.get<McReply>();
  %%{
    machine mc_ascii_flushall_reply;
    write init nocs;
    write exec;
  }%%
}

template<>
void McClientAsciiParser::initializeReplyParser<
    McRequestWithMcOp<mc_op_get>>() {

  initializeCommon();
  savedCs_ = mc_ascii_get_reply_en_get_reply;
  errorCs_ = mc_ascii_get_reply_error;
  consumer_ =
      &McClientAsciiParser::consumeMessage<McReply, McOperation<mc_op_get>>;
}

template<>
void McClientAsciiParser::initializeReplyParser<
    McRequestWithMcOp<mc_op_gets>>() {

  initializeCommon();
  savedCs_ = mc_ascii_gets_reply_en_gets_reply;
  errorCs_ = mc_ascii_gets_reply_error;
  consumer_ =
      &McClientAsciiParser::consumeMessage<McReply, McOperation<mc_op_gets>>;
}

template<>
void McClientAsciiParser::initializeReplyParser<
    McRequestWithMcOp<mc_op_lease_get>>() {

  initializeCommon();
  savedCs_ = mc_ascii_lease_get_reply_en_lease_get_reply;
  errorCs_ = mc_ascii_lease_get_reply_error;
  consumer_ =
      &McClientAsciiParser::consumeMessage<McReply,
                                           McOperation<mc_op_lease_get>>;
}

template<>
void McClientAsciiParser::initializeReplyParser<
    McRequestWithMcOp<mc_op_set>>() {

  initializeStorageReplyCommon();
}

template<>
void McClientAsciiParser::initializeReplyParser<
    McRequestWithMcOp<mc_op_add>>() {

  initializeStorageReplyCommon();
}

template<>
void McClientAsciiParser::initializeReplyParser<
    McRequestWithMcOp<mc_op_replace>>() {

  initializeStorageReplyCommon();
}

template<>
void McClientAsciiParser::initializeReplyParser<
    McRequestWithMcOp<mc_op_lease_set>>() {

  initializeStorageReplyCommon();
}

template<>
void McClientAsciiParser::initializeReplyParser<
    McRequestWithMcOp<mc_op_cas>>() {

  initializeStorageReplyCommon();
}

template<>
void McClientAsciiParser::initializeReplyParser<
    McRequestWithMcOp<mc_op_append>>() {

  initializeStorageReplyCommon();
}

template<>
void McClientAsciiParser::initializeReplyParser<
    McRequestWithMcOp<mc_op_prepend>>() {

  initializeStorageReplyCommon();
}

template<>
void McClientAsciiParser::initializeReplyParser<
    McRequestWithMcOp<mc_op_incr>>() {

  initializeArithmReplyCommon();
}

template<>
void McClientAsciiParser::initializeReplyParser<
    McRequestWithMcOp<mc_op_decr>>() {

  initializeArithmReplyCommon();
}

template<>
void McClientAsciiParser::initializeReplyParser<
    McRequestWithMcOp<mc_op_version>>() {

  initializeCommon();
  savedCs_ = mc_ascii_version_reply_en_version_reply;
  errorCs_ = mc_ascii_version_reply_error;
  consumer_ =
      &McClientAsciiParser::consumeMessage<McReply, McOperation<mc_op_version>>;
}

template<>
void McClientAsciiParser::initializeReplyParser<
    McRequestWithMcOp<mc_op_delete>>() {

  initializeCommon();
  savedCs_ = mc_ascii_delete_reply_en_delete_reply;
  errorCs_ = mc_ascii_delete_reply_error;
  consumer_ =
      &McClientAsciiParser::consumeMessage<McReply, McOperation<mc_op_delete>>;
}

template<>
void McClientAsciiParser::initializeReplyParser<
    McRequestWithMcOp<mc_op_touch>>() {

  initializeCommon();
  savedCs_ = mc_ascii_touch_reply_en_touch_reply;
  errorCs_ = mc_ascii_touch_reply_error;
  consumer_ =
      &McClientAsciiParser::consumeMessage<McReply, McOperation<mc_op_touch>>;
}

template<>
void McClientAsciiParser::initializeReplyParser<
    McRequestWithMcOp<mc_op_metaget>>() {

  initializeCommon();
  // Since mc_op_metaget has A LOT of specific fields, just create McMsgRef for
  // now.
  McReply& reply = currentMessage_.get<McReply>();
  reply.msg_ = createMcMsgRef();
  savedCs_ = mc_ascii_metaget_reply_en_metaget_reply;
  errorCs_ = mc_ascii_metaget_reply_error;
  consumer_ =
      &McClientAsciiParser::consumeMessage<McReply, McOperation<mc_op_metaget>>;
}

template<>
void McClientAsciiParser::initializeReplyParser<
    McRequestWithMcOp<mc_op_flushall>>() {

  initializeCommon();
  savedCs_ = mc_ascii_flushall_reply_en_flushall_reply;
  errorCs_ = mc_ascii_flushall_reply_error;
  consumer_ = &McClientAsciiParser::consumeMessage<McReply,
                                                   McOperation<mc_op_flushall>>;
}

void McClientAsciiParser::initializeArithmReplyCommon() {
  initializeCommon();
  savedCs_ = mc_ascii_arithm_reply_en_arithm_reply;
  errorCs_ = mc_ascii_arithm_reply_error;
  consumer_ = &McClientAsciiParser::consumeArithmReplyCommon;
}

void McClientAsciiParser::initializeStorageReplyCommon() {
  initializeCommon();
  savedCs_ = mc_ascii_storage_reply_en_storage_reply;
  errorCs_ = mc_ascii_storage_reply_error;
  consumer_ = &McClientAsciiParser::consumeStorageReplyCommon;
}

void McClientAsciiParser::initializeCommon() {
  assert(state_ == State::UNINIT);

  currentUInt_ = 0;
  currentIOBuf_ = nullptr;
  remainingIOBufLength_ = 0;
  state_ = State::PARTIAL;

  currentMessage_.emplace<McReply>();
}

// Server parser.

// Get-like requests (get, gets, lease-get, metaget).

%%{
machine mc_ascii_get_like_req_body;
include mc_ascii_common;

action on_full_key {
  callback_->onRequest(Operation(),
                       std::move(currentMessage_.get<Request>()));
}

req_body := ' '* key % on_full_key (' '+ key % on_full_key)* ' '* multi_op_end;

write data;
}%%

template <class Operation, class Request>
void McServerAsciiParser::consumeGetLike(folly::IOBuf& buffer) {
  Request& message = currentMessage_.get<Request>();
  %%{
    machine mc_ascii_get_like_req_body;
    write init nocs;
    write exec;
  }%%
}

template <class Operation, class Request>
void McServerAsciiParser::initGetLike() {
  savedCs_ = mc_ascii_get_like_req_body_en_req_body;
  errorCs_ = mc_ascii_get_like_req_body_error;
  state_ = State::PARTIAL;
  currentMessage_.emplace<Request>();
  consumer_ = &McServerAsciiParser::consumeGetLike<Operation, Request>;
}

// Set-like requests (set, add, replace, append, prepend).

%%{
machine mc_ascii_set_like_req_body;
include mc_ascii_common;

req_body := ' '* key ' '+ flags ' '+ exptime_req ' '+ value_bytes
            (' '+ noreply)? ' '* new_line @req_value_data new_line @{
              callback_->onRequest(Operation(),
                                   std::move(currentMessage_.get<Request>()),
                                   noreply_);
              finishReq();
              fbreak;
            };

write data;
}%%

template <class Operation, class Request>
void McServerAsciiParser::consumeSetLike(folly::IOBuf& buffer) {
  Request& message = currentMessage_.get<Request>();
  %%{
    machine mc_ascii_set_like_req_body;
    write init nocs;
    write exec;
  }%%
}

template <class Operation, class Request>
void McServerAsciiParser::initSetLike() {
  savedCs_ = mc_ascii_set_like_req_body_en_req_body;
  errorCs_ = mc_ascii_set_like_req_body_error;
  state_ = State::PARTIAL;
  currentMessage_.emplace<Request>();
  consumer_ = &McServerAsciiParser::consumeSetLike<Operation, Request>;
}

// Cas request.

%%{
machine mc_ascii_cas_req_body;
include mc_ascii_common;

req_body := ' '* key ' '+ flags ' '+ exptime_req ' '+ value_bytes ' '+ cas_id
            (' '+ noreply)? ' '* new_line @req_value_data new_line @{
              callback_->onRequest(McOperation<mc_op_cas>(),
                                   std::move(currentMessage_.get<McRequest>()),
                                   noreply_);
              finishReq();
              fbreak;
            };

write data;
}%%

void McServerAsciiParser::consumeCas(folly::IOBuf& buffer) {
  McRequest& message = currentMessage_.get<McRequest>();
  %%{
    machine mc_ascii_cas_req_body;
    write init nocs;
    write exec;
  }%%
}

// Lease-set request.

%%{
machine mc_ascii_lease_set_req_body;
include mc_ascii_common;

req_body := ' '* key ' '+ lease_token ' '+ flags ' '+ exptime_req ' '+
            value_bytes (' '+ noreply)? ' '* new_line @req_value_data
            new_line @{
              callback_->onRequest(McOperation<mc_op_lease_set>(),
                                   std::move(currentMessage_.get<McRequest>()),
                                   noreply_);
              finishReq();
              fbreak;
            };

write data;
}%%

void McServerAsciiParser::consumeLeaseSet(folly::IOBuf& buffer) {
  McRequest& message = currentMessage_.get<McRequest>();
  %%{
    machine mc_ascii_lease_set_req_body;
    write init nocs;
    write exec;
  }%%
}

// Delete request.

%%{
machine mc_ascii_delete_req_body;
include mc_ascii_common;

req_body := ' '* key (' '+ exptime_req)? (' '+ noreply)? ' '* new_line @{
              callback_->onRequest(McOperation<mc_op_delete>(),
                                   std::move(currentMessage_.get<McRequest>()),
                                   noreply_);
              finishReq();
              fbreak;
            };

write data;
}%%

void McServerAsciiParser::consumeDelete(folly::IOBuf& buffer) {
  McRequest& message = currentMessage_.get<McRequest>();
  %%{
    machine mc_ascii_delete_req_body;
    write init nocs;
    write exec;
  }%%
}

// Touch request.

%%{
machine mc_ascii_touch_req_body;
include mc_ascii_common;

req_body := ' '* key ' '+ exptime_req (' '+ noreply)? ' '* new_line @{
              callback_->onRequest(McOperation<mc_op_touch>(),
                                   std::move(currentMessage_.get<McRequest>()),
                                   noreply_);
              finishReq();
              fbreak;
            };

write data;
}%%

void McServerAsciiParser::consumeTouch(folly::IOBuf& buffer) {
  McRequest& message = currentMessage_.get<McRequest>();
  %%{
    machine mc_ascii_touch_req_body;
    write init nocs;
    write exec;
  }%%
}

// Shutdown request.

%%{
machine mc_ascii_shutdown_req_body;
include mc_ascii_common;

# Note we ignore shutdown delay, since mcrouter does not honor it anyway.
req_body := (' '+ number)? ' '* new_line @{
              callback_->onRequest(McOperation<mc_op_shutdown>(),
                                   std::move(currentMessage_.get<McRequest>()),
                                   noreply_);
              finishReq();
              fbreak;
            };

write data;
}%%

void McServerAsciiParser::consumeShutdown(folly::IOBuf& buffer) {
  McRequest& message = currentMessage_.get<McRequest>();
  %%{
    machine mc_ascii_shutdown_req_body;
    write init nocs;
    write exec;
  }%%
}

// Arithmetic request.

%%{
machine mc_ascii_arithmetic_req_body;
include mc_ascii_common;

req_body := ' '* key ' '+ delta (' '* noreply)? ' '* new_line @{
              callback_->onRequest(Operation(),
                                   std::move(currentMessage_.get<Request>()),
                                   noreply_);
              finishReq();
              fbreak;
            };

write data;
}%%

template <class Operation, class Request>
void McServerAsciiParser::consumeArithmetic(folly::IOBuf& buffer) {
  Request& message = currentMessage_.get<Request>();
  %%{
    machine mc_ascii_arithmetic_req_body;
    write init nocs;
    write exec;
  }%%
}

template <class Operation, class Request>
void McServerAsciiParser::initArithmetic() {
  savedCs_ = mc_ascii_arithmetic_req_body_en_req_body;
  errorCs_ = mc_ascii_arithmetic_req_body_error;
  state_ = State::PARTIAL;
  currentMessage_.emplace<Request>();
  consumer_ = &McServerAsciiParser::consumeArithmetic<Operation, Request>;
}

// Stats request.

%%{
machine mc_ascii_stats_req_body;
include mc_ascii_common;

req_body := (' ' multi_token)? new_line @{
              callback_->onRequest(McOperation<mc_op_stats>(),
                                   std::move(currentMessage_.get<McRequest>()),
                                   noreply_);
              finishReq();
              fbreak;
            };

write data;
}%%

void McServerAsciiParser::consumeStats(folly::IOBuf& buffer) {
  McRequest& message = currentMessage_.get<McRequest>();
  %%{
    machine mc_ascii_stats_req_body;
    write init nocs;
    write exec;
  }%%
}

// Exec request.

%%{
machine mc_ascii_exec_req_body;
include mc_ascii_common;

req_body := multi_token new_line @{
              callback_->onRequest(McOperation<mc_op_exec>(),
                                   std::move(currentMessage_.get<McRequest>()),
                                   noreply_);
              finishReq();
              fbreak;
            };

write data;
}%%

void McServerAsciiParser::consumeExec(folly::IOBuf& buffer) {
  McRequest& message = currentMessage_.get<McRequest>();
  %%{
    machine mc_ascii_exec_req_body;
    write init nocs;
    write exec;
  }%%
}

// Flush_regex request.

%%{
machine mc_ascii_flush_re_req_body;
include mc_ascii_common;

req_body := ' '* key ' '* new_line @{
              callback_->onRequest(McOperation<mc_op_flushre>(),
                                   std::move(currentMessage_.get<McRequest>()),
                                   noreply_);
              finishReq();
              fbreak;
            };

write data;
}%%

void McServerAsciiParser::consumeFlushRe(folly::IOBuf& buffer) {
  McRequest& message = currentMessage_.get<McRequest>();
  %%{
    machine mc_ascii_flush_re_req_body;
    write init nocs;
    write exec;
  }%%
}

// Flush_all request.

%%{
machine mc_ascii_flush_all_req_body;
include mc_ascii_common;

req_body := ' '* number? ' '* new_line @{
              callback_->onRequest(McOperation<mc_op_flushall>(),
                                   std::move(currentMessage_.get<McRequest>()),
                                   noreply_);
              finishReq();
              fbreak;
            };

write data;
}%%

void McServerAsciiParser::consumeFlushAll(folly::IOBuf& buffer) {
  McRequest& message = currentMessage_.get<McRequest>();
  %%{
    machine mc_ascii_flush_all_req_body;
    write init nocs;
    write exec;
  }%%
}

// Operation keyword parser.

%%{
machine mc_ascii_req_op_type;

# Define binding to class member variables.
variable p p_;
variable pe pe_;
variable cs savedCs_;

new_line = '\r'? '\n';

get = 'get ' @{
  initGetLike<McOperation<mc_op_get>, McRequest>();
  fbreak;
};

gets = 'gets ' @{
  initGetLike<McOperation<mc_op_gets>, McRequest>();
  fbreak;
};

lease_get = 'lease-get ' @{
  initGetLike<McOperation<mc_op_lease_get>, McRequest>();
  fbreak;
};

metaget = 'metaget ' @{
  initGetLike<McOperation<mc_op_metaget>, McRequest>();
  fbreak;
};

set = 'set ' @{
  initSetLike<McOperation<mc_op_set>, McRequest>();
  fbreak;
};

add = 'add ' @{
  initSetLike<McOperation<mc_op_add>, McRequest>();
  fbreak;
};

replace = 'replace ' @{
  initSetLike<McOperation<mc_op_replace>, McRequest>();
  fbreak;
};

append = 'append ' @{
  initSetLike<McOperation<mc_op_append>, McRequest>();
  fbreak;
};

prepend = 'prepend ' @{
  initSetLike<McOperation<mc_op_prepend>, McRequest>();
  fbreak;
};

cas = 'cas ' @{
  savedCs_ = mc_ascii_cas_req_body_en_req_body;
  errorCs_ = mc_ascii_cas_req_body_error;
  state_ = State::PARTIAL;
  currentMessage_.emplace<McRequest>();
  consumer_ = &McServerAsciiParser::consumeCas;
  fbreak;
};

lease_set = 'lease-set ' @{
  savedCs_ = mc_ascii_lease_set_req_body_en_req_body;
  errorCs_ = mc_ascii_lease_set_req_body_error;
  state_ = State::PARTIAL;
  currentMessage_.emplace<McRequest>();
  consumer_ = &McServerAsciiParser::consumeLeaseSet;
  fbreak;
};

delete = 'delete ' @{
  savedCs_ = mc_ascii_delete_req_body_en_req_body;
  errorCs_ = mc_ascii_delete_req_body_error;
  state_ = State::PARTIAL;
  currentMessage_.emplace<McRequest>();
  consumer_ = &McServerAsciiParser::consumeDelete;
  fbreak;
};

touch = 'touch ' @{
  savedCs_ = mc_ascii_touch_req_body_en_req_body;
  errorCs_ = mc_ascii_touch_req_body_error;
  state_ = State::PARTIAL;
  currentMessage_.emplace<McRequest>();
  consumer_ = &McServerAsciiParser::consumeTouch;
  fbreak;
};

shutdown = 'shutdown' @{
  savedCs_ = mc_ascii_shutdown_req_body_en_req_body;
  errorCs_ = mc_ascii_shutdown_req_body_error;
  state_ = State::PARTIAL;
  currentMessage_.emplace<McRequest>();
  consumer_ = &McServerAsciiParser::consumeShutdown;
  fbreak;
};

incr = 'incr ' @{
  initArithmetic<McOperation<mc_op_incr>, McRequest>();
  fbreak;
};

decr = 'decr ' @{
  initArithmetic<McOperation<mc_op_decr>, McRequest>();
  fbreak;
};

version = 'version' ' '* new_line @{
  callback_->onRequest(McOperation<mc_op_version>(), McRequest());
  finishReq();
  fbreak;
};

quit = 'quit' ' '* new_line @{
  callback_->onRequest(McOperation<mc_op_quit>(), McRequest(),
                       true /* noReply */);
  finishReq();
  fbreak;
};

stats = 'stats' @{
  savedCs_ = mc_ascii_stats_req_body_en_req_body;
  errorCs_ = mc_ascii_stats_req_body_error;
  state_ = State::PARTIAL;
  currentMessage_.emplace<McRequest>();
  consumer_ = &McServerAsciiParser::consumeStats;
  fbreak;
};

exec = ('exec ' | 'admin ') @{
  savedCs_ = mc_ascii_exec_req_body_en_req_body;
  errorCs_ = mc_ascii_exec_req_body_error;
  state_ = State::PARTIAL;
  currentMessage_.emplace<McRequest>();
  consumer_ = &McServerAsciiParser::consumeExec;
  fbreak;
};

flush_re = 'flush_regex ' @{
  savedCs_ = mc_ascii_flush_re_req_body_en_req_body;
  errorCs_ = mc_ascii_flush_re_req_body_error;
  state_ = State::PARTIAL;
  currentMessage_.emplace<McRequest>();
  consumer_ = &McServerAsciiParser::consumeFlushRe;
  fbreak;
};

flush_all = 'flush_all' @{
  savedCs_ = mc_ascii_flush_all_req_body_en_req_body;
  errorCs_ = mc_ascii_flush_all_req_body_error;
  state_ = State::PARTIAL;
  currentMessage_.emplace<McRequest>();
  consumer_ = &McServerAsciiParser::consumeFlushAll;
  fbreak;
};

command := get | gets | lease_get | metaget | set | add | replace | append |
           prepend | cas | lease_set | delete | shutdown | incr | decr |
           version | quit | stats | exec | flush_re | flush_all | touch;

write data;
}%%

void McServerAsciiParser::opTypeConsumer(folly::IOBuf& buffer) {
  %%{
    machine mc_ascii_req_op_type;
    write init nocs;
    write exec;
  }%%
}

void McServerAsciiParser::finishReq() {
  state_ = State::UNINIT;
}

McAsciiParserBase::State McServerAsciiParser::consume(folly::IOBuf& buffer) {
  assert(state_ != State::ERROR && state_ != State::COMPLETE &&
         !hasReadBuffer());
  p_ = reinterpret_cast<const char*>(buffer.data());
  pe_ = p_ + buffer.length();

  while (p_ != pe_) {
    // Initialize operation parser.
    if (state_ == State::UNINIT) {
      savedCs_ = mc_ascii_req_op_type_en_command;
      errorCs_ = mc_ascii_req_op_type_error;

      // Reset all fields.
      currentUInt_ = 0;
      currentIOBuf_ = nullptr;
      remainingIOBufLength_ = 0;
      currentKey_.clear();
      noreply_ = false;
      negative_ = false;

      state_ = State::PARTIAL;

      consumer_ = &McServerAsciiParser::opTypeConsumer;
    } else {
      // In case we're currently parsing a key, set keyPieceStart_ to the
      // beginning of the current buffer.
      if (keyPieceStart_ != nullptr) {
        keyPieceStart_ = p_;
      }
    }

    (this->*consumer_)(buffer);

    // If we're parsing a key, append current piece of buffer to it.
    if (keyPieceStart_ != nullptr) {
      appendKeyPiece(buffer, currentKey_, keyPieceStart_, p_);
    }

    if (savedCs_ == errorCs_) {
      handleError(buffer);
      break;
    }

    buffer.trimStart(p_ - reinterpret_cast<const char*>(buffer.data()));
  }

  return state_;
}

}}  // facebook::memcache
