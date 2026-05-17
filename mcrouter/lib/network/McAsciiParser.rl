/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "mcrouter/lib/network/McAsciiParser.h"

#include "mcrouter/lib/mc/msg.h"
#include "mcrouter/lib/network/gen/MemcacheMessages.h"
#include "mcrouter/lib/network/gen/MemcacheRoutingGroups.h"

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
  // We must ensure message.value_ref() is nonempty for ASCII get-like replies
  message.value_ref() = folly::IOBuf();
  if (!readValue(buffer, *message.value_ref())) {
    fbreak;
  }
}

# Action that initializes/performs data parsing for requests.
action req_value_data {
  if (!readValue(buffer, *message.value_ref())) {
    fbreak;
  }
}

# Resets current value, used for errors.
action reset_value {
  using MsgT = typename std::decay<decltype(message)>::type;
  resetErrorMessage<MsgT>(message);
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
  message.key_ref() = std::move(currentKey_);
};

multi_token = (print+ -- ( '\r' | '\n' )) >key_start %key_end %{
  // Trim string.
  while (currentKey_.length() > 0 && isspace(*currentKey_.data())) {
    currentKey_.trimStart(1);
  }
  while (currentKey_.length() > 0 && isspace(*(currentKey_.tail() - 1))) {
    currentKey_.trimEnd(1);
  }
  message.key_ref() = std::move(currentKey_);
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
  message.flags_ref() = currentUInt_;
};

delay = uint %{
  message.delay_ref() = currentUInt_;
};

exptime = uint %{
  message.exptime_ref() = static_cast<int32_t>(currentUInt_);
};

exptime_req = negative? uint %{
  auto value = static_cast<int32_t>(currentUInt_);
  message.exptime_ref() = negative_ ? -value : value;
  negative_ = false;
};

value_bytes = uint %{
  remainingIOBufLength_ = static_cast<size_t>(currentUInt_);
  // Enforce maximum on value size obtained from parser                       
  if (remainingIOBufLength_ > maxValueBytes) {
    remainingIOBufLength_ = maxValueBytes;                                    
  }
};

cas_id = uint %{
  message.casToken_ref() = currentUInt_;
};

delta = uint %{
  message.delta_ref() = currentUInt_;
};

lease_token = uint %{
  // NOTE: we don't support -1 lease token.
  message.leaseToken_ref() = currentUInt_;
};

error_code = uint %{
  message.appSpecificErrorCode_ref() = static_cast<uint16_t>(currentUInt_);
};

# Common storage replies.
not_found = 'NOT_FOUND' @{ message.result_ref() = carbon::Result::NOTFOUND; };
deleted = 'DELETED' @{ message.result_ref() = carbon::Result::DELETED; };
touched = 'TOUCHED' @{ message.result_ref() = carbon::Result::TOUCHED; };

VALUE = 'VALUE' % { message.result_ref() = carbon::Result::FOUND; };

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

error_message = (any* -- ('\r' | '\n')) >reset_value ${
  using MsgT = typename std::decay<decltype(message)>::type;
  consumeErrorMessage<MsgT>(buffer);
};

server_error = 'SERVER_ERROR' (' ' error_code ' ')? ' '? error_message
               %{
                 uint32_t errorCode = currentUInt_;
                 if (errorCode == SERVER_ERROR_BUSY) {
                   message.result_ref() = carbon::Result::BUSY;
                 } else {
                   message.result_ref() = carbon::Result::REMOTE_ERROR;
                 }
               };

client_error = 'CLIENT_ERROR' (' ' error_code ' ')? ' '? error_message
               %{ message.result_ref() = carbon::Result::CLIENT_ERROR; };

error = command_error | server_error | client_error;
}%%

// McGet reply.
%%{
machine mc_ascii_get_reply;
include mc_ascii_common;

get = hit? >{ message.result_ref() = carbon::Result::NOTFOUND; } 'END';
get_reply := (get | error) msg_end;
write data;
}%%

template <>
void McClientAsciiParser::consumeMessage<McGetRequest>(folly::IOBuf& buffer) {
  auto& message = currentMessage_.get<McGetReply>();
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

gets = gets_hit? >{ message.result_ref() = carbon::Result::NOTFOUND; } 'END';
gets_reply := (gets | error) msg_end;
write data;
}%%

template <>
void McClientAsciiParser::consumeMessage<McGetsRequest>(folly::IOBuf& buffer) {
  auto& message = currentMessage_.get<McGetsReply>();
  %%{
    machine mc_ascii_gets_reply;
    write init nocs;
    write exec;
  }%%
}

// McGat reply.
%%{
machine mc_ascii_gat_reply;
include mc_ascii_common;

gat = hit? >{ message.result_ref() = carbon::Result::NOTFOUND; } 'END';
gat_reply := (gat | error) msg_end;
write data;
}%%

template <>
void McClientAsciiParser::consumeMessage<McGatRequest>(folly::IOBuf& buffer) {
  auto& message = currentMessage_.get<McGatReply>();
  %%{
    machine mc_ascii_gat_reply;
    write init nocs;
    write exec;
  }%%
}

// McGats reply.
%%{
machine mc_ascii_gats_reply;
include mc_ascii_common;

gats = gets_hit? >{ message.result_ref() = carbon::Result::NOTFOUND; } 'END';
gats_reply := (gats | error) msg_end;
write data;
}%%

template <>
void McClientAsciiParser::consumeMessage<McGatsRequest>(folly::IOBuf& buffer) {
  auto& message = currentMessage_.get<McGatsReply>();
  %%{
    machine mc_ascii_gats_reply;
    write init nocs;
    write exec;
  }%%
}

// McLeaseGet reply.
%%{
machine mc_ascii_lease_get_reply;
include mc_ascii_common;

# FIXME, we should return carbon::Result::FOUNDSTALE or carbon::Result::NOTFOUNDHOT.
lvalue = 'LVALUE' ' '+ skip_key ' '+ lease_token ' '+ flags ' '+ value_bytes
         new_line @reply_value_data new_line
         @{ message.result_ref() = carbon::Result::NOTFOUND; };

lease_get = (hit | lvalue) 'END';
lease_get_reply := (lease_get | error) msg_end;

write data;
}%%

template <>
void McClientAsciiParser::consumeMessage<
    McLeaseGetRequest>(folly::IOBuf& buffer) {
  auto& message = currentMessage_.get<McLeaseGetReply>();
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

stored = 'STORED' @{ message.result_ref() = carbon::Result::STORED; };
stale_stored = 'STALE_STORED' @{ message.result_ref() = carbon::Result::STALESTORED; };
not_stored = 'NOT_STORED' @{ message.result_ref() = carbon::Result::NOTSTORED; };
exists = 'EXISTS' @{ message.result_ref() = carbon::Result::EXISTS; };

storage = stored | stale_stored | not_stored | exists | not_found | deleted;
storage_reply := (storage | error) msg_end;

write data;
}%%

template <class Reply>
void McClientAsciiParser::consumeStorageReplyCommon(folly::IOBuf& buffer) {
  auto& message = currentMessage_.get<Reply>();
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

arithm = not_found | (delta @{ message.result_ref() = carbon::Result::STORED; }) ' '*;
arithm_reply := (arithm | error) msg_end;

write data;
}%%

template <class Reply>
void McClientAsciiParser::consumeArithmReplyCommon(folly::IOBuf& buffer) {
  auto& message = currentMessage_.get<Reply>();
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

version = 'VERSION ' @{ message.result_ref() = carbon::Result::OK; }
          (any* -- ('\r' | '\n')) ${
  using MsgT = std::decay<decltype(message)>::type;
  consumeVersion<MsgT>(buffer);
};
version_reply := (version | error) msg_end;

write data;
}%%

template <>
void McClientAsciiParser::consumeMessage<McVersionRequest>(
    folly::IOBuf& buffer) {
  auto& message = currentMessage_.get<McVersionReply>();
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

template <>
void McClientAsciiParser::consumeMessage<McDeleteRequest>(
    folly::IOBuf& buffer) {
  auto& message = currentMessage_.get<McDeleteReply>();
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
void McClientAsciiParser::consumeMessage<McTouchRequest>(
    folly::IOBuf& buffer) {
  auto& message = currentMessage_.get<McTouchReply>();
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

age = negative? uint %{
  auto value = static_cast<int32_t>(currentUInt_);
  message.age_ref() = negative_ ? -value : value;
  negative_ = false;
};
age_unknown = 'unknown' %{
  message.age_ref() = -1;
};

ip_addr = (xdigit | '.' | ':')+ ${
  using MsgT = std::decay<decltype(message)>::type;
  consumeIpAddrHelper<MsgT>(buffer);
} %{
  using MsgT = std::decay<decltype(message)>::type;
  consumeIpAddr<MsgT>(buffer);
};

transient = uint %{
  // We no longer support is_transient with typed requests.
};

#TODO(stuclar): Remove optional parsing of is_transient (T32090075)
meta = 'META' % { message.result_ref() = carbon::Result::FOUND; };
mhit = meta ' '+ skip_key ' '+ 'age:' ' '* (age | age_unknown) ';' ' '*
  'exptime:' ' '* exptime ';' ' '* 'from:' ' '* (ip_addr|'unknown') (';' ' '*
  'is_transient:' ' '* transient ' '*)? new_line;
metaget = mhit? >{ message.result_ref() = carbon::Result::NOTFOUND; } 'END' msg_end;
metaget_reply := (metaget | error) msg_end;

write data;
}%%

template <>
void McClientAsciiParser::consumeMessage<McMetagetRequest>(
    folly::IOBuf& buffer) {
  auto& message = currentMessage_.get<McMetagetReply>();
  %%{
    machine mc_ascii_metaget_reply;
    write init nocs;
    write exec;
  }%%
}

// McMetaCommandsGet reply.
%%{
machine mc_ascii_meta_commands_get_reply;
include mc_ascii_common;

# Response flag tokens (no value)
mg_resp_flag_b = 'b' %{ message.flags_ref() = *message.flags_ref() | MC_META_COMMANDS_FLAG_BASE64_ENCODED_KEY; };
mg_resp_flag_W = 'W' %{ message.flags_ref() = *message.flags_ref() | MC_META_COMMANDS_FLAG_WON_RECACHE; };
mg_resp_flag_X = 'X' %{ message.flags_ref() = *message.flags_ref() | MC_META_COMMANDS_FLAG_ITEM_IS_STALE; };
mg_resp_flag_Z = 'Z' %{ message.flags_ref() = *message.flags_ref() | MC_META_COMMANDS_FLAG_LOST_RECACHE; };

# Response flag tokens (with value)
mg_resp_flag_c = 'c' uint %{ message.casToken_ref() = currentUInt_; };
mg_resp_flag_f = 'f' uint %{ message.clientFlags_ref() = currentUInt_; };
mg_resp_flag_s = 's' uint %{ message.itemSize_ref() = currentUInt_; };
mg_resp_flag_l = 'l' uint %{ message.lastAccessTime_ref() = static_cast<int32_t>(currentUInt_); };
mg_resp_flag_h = 'h' uint %{ message.wasHitBefore_ref() = static_cast<int32_t>(currentUInt_); };
mg_resp_flag_t = 't' negative? uint %{
  auto value = static_cast<int32_t>(currentUInt_);
  message.remainingTTL_ref() = negative_ ? -value : value;
  negative_ = false;
};
mg_resp_flag_k = 'k' key;
mg_resp_flag_O = 'O' (any {1,32} -- (cntrl | space)) >key_start %key_end %{
  currentKey_.coalesce();
  message.opaqueToken_ref() = std::string(
      reinterpret_cast<const char*>(currentKey_.data()), currentKey_.length());
};
# Skip unknown response flags
mg_resp_unknown = (any - cntrl - space - [bcWXZ]) (any - cntrl - space)*;

mg_resp_flag = mg_resp_flag_b | mg_resp_flag_W | mg_resp_flag_X |
               mg_resp_flag_Z | mg_resp_flag_c | mg_resp_flag_O |
               mg_resp_flag_f | mg_resp_flag_s | mg_resp_flag_l |
               mg_resp_flag_h | mg_resp_flag_t | mg_resp_flag_k |
               mg_resp_unknown;

mg_resp_flags = (' '+ mg_resp_flag)*;

# VA <size> <flags>*\r\n<data>\r\n
va_hit = 'VA' %{ message.result_ref() = carbon::Result::FOUND; }
         ' '+ value_bytes mg_resp_flags new_line @reply_value_data;

# HD <flags>*\r\n
hd_hit = 'HD' %{ message.result_ref() = carbon::Result::FOUND; }
         mg_resp_flags;

# EN\r\n
en_miss = 'EN' %{ message.result_ref() = carbon::Result::NOTFOUND; };

mg_reply = va_hit | hd_hit | en_miss;
mg_reply_entry := (mg_reply | error) msg_end;

write data;
}%%

template <>
void McClientAsciiParser::consumeMessage<McMetaCommandsGetRequest>(
    folly::IOBuf& buffer) {
  auto& message = currentMessage_.get<McMetaCommandsGetReply>();
  %%{
    machine mc_ascii_meta_commands_get_reply;
    write init nocs;
    write exec;
  }%%
}

// McFlushAll reply.
%%{
machine mc_ascii_flushall_reply;
include mc_ascii_common;

flushall = 'OK' $ { message.result_ref() = carbon::Result::OK; };
flushall_reply := (flushall | error) msg_end;

write data;
}%%

template <>
void McClientAsciiParser::consumeMessage<McFlushAllRequest>(
    folly::IOBuf& buffer) {
  auto& message = currentMessage_.get<McFlushAllReply>();
  %%{
    machine mc_ascii_flushall_reply;
    write init nocs;
    write exec;
  }%%
}

template <>
void McClientAsciiParser::initializeReplyParser<McGetRequest>() {
  initializeCommon<McGetReply>();
  savedCs_ = mc_ascii_get_reply_en_get_reply;
  errorCs_ = mc_ascii_get_reply_error;
  consumer_ = &McClientAsciiParser::consumeMessage<McGetRequest>;
}

template <>
void McClientAsciiParser::initializeReplyParser<McGetsRequest>() {
  initializeCommon<McGetsReply>();
  savedCs_ = mc_ascii_gets_reply_en_gets_reply;
  errorCs_ = mc_ascii_gets_reply_error;
  consumer_ = &McClientAsciiParser::consumeMessage<McGetsRequest>;
}

template <>
void McClientAsciiParser::initializeReplyParser<McGatRequest>() {
  initializeCommon<McGatReply>();
  savedCs_ = mc_ascii_gat_reply_en_gat_reply;
  errorCs_ = mc_ascii_gat_reply_error;
  consumer_ = &McClientAsciiParser::consumeMessage<McGatRequest>;
}

template <>
void McClientAsciiParser::initializeReplyParser<McGatsRequest>() {
  initializeCommon<McGatsReply>();
  savedCs_ = mc_ascii_gats_reply_en_gats_reply;
  errorCs_ = mc_ascii_gats_reply_error;
  consumer_ = &McClientAsciiParser::consumeMessage<McGatsRequest>;
}

template <>
void McClientAsciiParser::initializeReplyParser<McLeaseGetRequest>() {
  initializeCommon<McLeaseGetReply>();
  savedCs_ = mc_ascii_lease_get_reply_en_lease_get_reply;
  errorCs_ = mc_ascii_lease_get_reply_error;
  consumer_ = &McClientAsciiParser::consumeMessage<McLeaseGetRequest>;
}

template <>
void McClientAsciiParser::initializeReplyParser<McSetRequest>() {
  initializeStorageReplyCommon<McSetReply>();
}

template <>
void McClientAsciiParser::initializeReplyParser<McAddRequest>() {
  initializeStorageReplyCommon<McAddReply>();
}

template <>
void McClientAsciiParser::initializeReplyParser<McReplaceRequest>() {
  initializeStorageReplyCommon<McReplaceReply>();
}

template <>
void McClientAsciiParser::initializeReplyParser<McLeaseSetRequest>() {
  initializeStorageReplyCommon<McLeaseSetReply>();
}

template <>
void McClientAsciiParser::initializeReplyParser<McCasRequest>() {
  initializeStorageReplyCommon<McCasReply>();
}

template <>
void McClientAsciiParser::initializeReplyParser<McAppendRequest>() {
  initializeStorageReplyCommon<McAppendReply>();
}

template <>
void McClientAsciiParser::initializeReplyParser<McPrependRequest>() {
  initializeStorageReplyCommon<McPrependReply>();
}

template <>
void McClientAsciiParser::initializeReplyParser<McIncrRequest>() {
  initializeArithmReplyCommon<McIncrReply>();
}

template <>
void McClientAsciiParser::initializeReplyParser<McDecrRequest>() {
  initializeArithmReplyCommon<McDecrReply>();
}

template <>
void McClientAsciiParser::initializeReplyParser<McVersionRequest>() {
  initializeCommon<McVersionReply>();
  savedCs_ = mc_ascii_version_reply_en_version_reply;
  errorCs_ = mc_ascii_version_reply_error;
  consumer_ = &McClientAsciiParser::consumeMessage<McVersionRequest>;
}

template <>
void McClientAsciiParser::initializeReplyParser<McDeleteRequest>() {
  initializeCommon<McDeleteReply>();
  savedCs_ = mc_ascii_delete_reply_en_delete_reply;
  errorCs_ = mc_ascii_delete_reply_error;
  consumer_ = &McClientAsciiParser::consumeMessage<McDeleteRequest>;
}

template <>
void McClientAsciiParser::initializeReplyParser<McTouchRequest>() {
  initializeCommon<McTouchReply>();
  savedCs_ = mc_ascii_touch_reply_en_touch_reply;
  errorCs_ = mc_ascii_touch_reply_error;
  consumer_ = &McClientAsciiParser::consumeMessage<McTouchRequest>;
}

template <>
void McClientAsciiParser::initializeReplyParser<McMetagetRequest>() {
  initializeCommon<McMetagetReply>();
  savedCs_ = mc_ascii_metaget_reply_en_metaget_reply;
  errorCs_ = mc_ascii_metaget_reply_error;
  consumer_ = &McClientAsciiParser::consumeMessage<McMetagetRequest>;
}

template <>
void McClientAsciiParser::initializeReplyParser<McMetaCommandsGetRequest>() {
  initializeCommon<McMetaCommandsGetReply>();
  savedCs_ = mc_ascii_meta_commands_get_reply_en_mg_reply_entry;
  errorCs_ = mc_ascii_meta_commands_get_reply_error;
  consumer_ = &McClientAsciiParser::consumeMessage<McMetaCommandsGetRequest>;
}

template <>
void McClientAsciiParser::initializeReplyParser<McFlushAllRequest>() {
  initializeCommon<McFlushAllReply>();
  savedCs_ = mc_ascii_flushall_reply_en_flushall_reply;
  errorCs_ = mc_ascii_flushall_reply_error;
  consumer_ = &McClientAsciiParser::consumeMessage<McFlushAllRequest>;
}


template <class Reply>
void McClientAsciiParser::initializeArithmReplyCommon() {
  initializeCommon<Reply>();
  savedCs_ = mc_ascii_arithm_reply_en_arithm_reply;
  errorCs_ = mc_ascii_arithm_reply_error;
  consumer_ = &McClientAsciiParser::consumeArithmReplyCommon<Reply>;
}

template <class Reply>
void McClientAsciiParser::initializeStorageReplyCommon() {
  initializeCommon<Reply>();
  savedCs_ = mc_ascii_storage_reply_en_storage_reply;
  errorCs_ = mc_ascii_storage_reply_error;
  consumer_ = &McClientAsciiParser::consumeStorageReplyCommon<Reply>;
}

template <class Reply>
void McClientAsciiParser::initializeCommon() {
  assert(state_ == State::UNINIT);

  currentUInt_ = 0;
  currentIOBuf_ = nullptr;
  remainingIOBufLength_ = 0;
  state_ = State::PARTIAL;

  currentKey_.clear();

  currentMessage_.emplace<Reply>();
}

// Server parser.

// Get-like requests (get, gets, lease-get, metaget).

%%{
machine mc_ascii_get_like_req_body;
include mc_ascii_common;

action on_full_key {
  callback_->onRequest(std::move(message));
}

req_body := ' '* key % on_full_key (' '+ key % on_full_key)* ' '* multi_op_end;

write data;
}%%

template <class Request>
void McServerAsciiParser::consumeGetLike(folly::IOBuf& buffer) {
  auto& message = currentMessage_.get<Request>();
  %%{
    machine mc_ascii_get_like_req_body;
    write init nocs;
    write exec;
  }%%
}

template <class Request>
void McServerAsciiParser::initGetLike() {
  savedCs_ = mc_ascii_get_like_req_body_en_req_body;
  errorCs_ = mc_ascii_get_like_req_body_error;
  state_ = State::PARTIAL;
  currentMessage_.emplace<Request>();
  consumer_ = &McServerAsciiParser::consumeGetLike<Request>;
}

// Gat-like requests (gat, gats).

%%{
machine mc_ascii_gat_like_req_body;
include mc_ascii_common;

action on_full_key {
  callback_->onRequest(std::move(message));
}

req_body := ' '* exptime_req ' '+ key % on_full_key (' '+ key % on_full_key)* ' '* multi_op_end;

write data;
}%%

template <class Request>
void McServerAsciiParser::consumeGatLike(folly::IOBuf& buffer) {
  auto& message = currentMessage_.get<Request>();
  %%{
    machine mc_ascii_gat_like_req_body;
    write init nocs;
    write exec;
  }%%
}

template <class Request>
void McServerAsciiParser::initGatLike() {
  savedCs_ = mc_ascii_gat_like_req_body_en_req_body;
  errorCs_ = mc_ascii_gat_like_req_body_error;
  state_ = State::PARTIAL;
  currentMessage_.emplace<Request>();
  consumer_ = &McServerAsciiParser::consumeGatLike<Request>;
}

// Set-like requests (set, add, replace, append, prepend).

%%{
machine mc_ascii_set_like_req_body;
include mc_ascii_common;

req_body := ' '* key ' '+ flags ' '+ exptime_req ' '+ value_bytes
            (' '+ noreply)? ' '* new_line @req_value_data new_line @{
              callback_->onRequest(std::move(message), noreply_);
              finishReq();
              fbreak;
            };

write data;
}%%

template <class Request>
void McServerAsciiParser::consumeSetLike(folly::IOBuf& buffer) {
  auto& message = currentMessage_.get<Request>();
  %%{
    machine mc_ascii_set_like_req_body;
    write init nocs;
    write exec;
  }%%
}

template <class Request>
void McServerAsciiParser::initSetLike() {
  savedCs_ = mc_ascii_set_like_req_body_en_req_body;
  errorCs_ = mc_ascii_set_like_req_body_error;
  state_ = State::PARTIAL;
  currentMessage_.emplace<Request>();
  consumer_ = &McServerAsciiParser::consumeSetLike<Request>;
}

// Cas request.

%%{
machine mc_ascii_cas_req_body;
include mc_ascii_common;

req_body := ' '* key ' '+ flags ' '+ exptime_req ' '+ value_bytes ' '+ cas_id
            (' '+ noreply)? ' '* new_line @req_value_data new_line @{
              callback_->onRequest(std::move(message), noreply_);
              finishReq();
              fbreak;
            };

write data;
}%%

void McServerAsciiParser::consumeCas(folly::IOBuf& buffer) {
  auto& message = currentMessage_.get<McCasRequest>();
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
              callback_->onRequest(std::move(message), noreply_);
              finishReq();
              fbreak;
            };

write data;
}%%

void McServerAsciiParser::consumeLeaseSet(folly::IOBuf& buffer) {
  auto& message =
    currentMessage_.get<McLeaseSetRequest>();
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
              callback_->onRequest(std::move(message), noreply_);
              finishReq();
              fbreak;
            };

write data;
}%%

void McServerAsciiParser::consumeDelete(folly::IOBuf& buffer) {
  auto& message =
    currentMessage_.get<McDeleteRequest>();
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
              callback_->onRequest(std::move(message), noreply_);
              finishReq();
              fbreak;
            };

write data;
}%%

void McServerAsciiParser::consumeTouch(folly::IOBuf& buffer) {
  auto& message =
    currentMessage_.get<McTouchRequest>();
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
req_body := (' '+ digit+)? ' '* new_line @{
              callback_->onRequest(std::move(message), noreply_);
              finishReq();
              fbreak;
            };

write data;
}%%

void McServerAsciiParser::consumeShutdown(folly::IOBuf&) {
  auto& message =
    currentMessage_.get<McShutdownRequest>();
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
              callback_->onRequest(std::move(message), noreply_);
              finishReq();
              fbreak;
            };

write data;
}%%

template <class Request>
void McServerAsciiParser::consumeArithmetic(folly::IOBuf& buffer) {
  auto& message = currentMessage_.get<Request>();
  %%{
    machine mc_ascii_arithmetic_req_body;
    write init nocs;
    write exec;
  }%%
}

template <class Request>
void McServerAsciiParser::initArithmetic() {
  savedCs_ = mc_ascii_arithmetic_req_body_en_req_body;
  errorCs_ = mc_ascii_arithmetic_req_body_error;
  state_ = State::PARTIAL;
  currentMessage_.emplace<Request>();
  consumer_ = &McServerAsciiParser::consumeArithmetic<Request>;
}

// Stats request.

%%{
machine mc_ascii_stats_req_body;
include mc_ascii_common;

req_body := ' '* (' ' multi_token)? new_line @{
              callback_->onRequest(std::move(message), noreply_);
              finishReq();
              fbreak;
            };

write data;
}%%

void McServerAsciiParser::consumeStats(folly::IOBuf& buffer) {
  auto& message =
    currentMessage_.get<McStatsRequest>();
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
              callback_->onRequest(std::move(message), noreply_);
              finishReq();
              fbreak;
            };

write data;
}%%

void McServerAsciiParser::consumeExec(folly::IOBuf& buffer) {
  auto& message =
    currentMessage_.get<McExecRequest>();
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
              callback_->onRequest(std::move(message), noreply_);
              finishReq();
              fbreak;
            };

write data;
}%%

void McServerAsciiParser::consumeFlushRe(folly::IOBuf& buffer) {
  auto& message =
    currentMessage_.get<McFlushReRequest>();
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

req_body := (' '* delay)? ' '* new_line @{
              callback_->onRequest(std::move(message), noreply_);
              finishReq();
              fbreak;
            };

write data;
}%%

void McServerAsciiParser::consumeFlushAll(folly::IOBuf&) {
  auto& message =
    currentMessage_.get<McFlushAllRequest>();
  %%{
    machine mc_ascii_flush_all_req_body;
    write init nocs;
    write exec;
  }%%
}

// Meta commands get (mg) request.

%%{
machine mc_ascii_mg_req_body;
include mc_ascii_common;

# Request flag tokens (no value) - set bitfield
mg_req_flag_b = 'b' %{ message.flags_ref() = *message.flags_ref() | MC_META_COMMANDS_FLAG_BASE64_ENCODED_KEY; };
mg_req_flag_c = 'c' %{ message.flags_ref() = *message.flags_ref() | MC_META_COMMANDS_FLAG_RETURN_CAS_TOKEN; };
mg_req_flag_f = 'f' %{ message.flags_ref() = *message.flags_ref() | MC_META_COMMANDS_FLAG_RETURN_CLIENT_FLAGS; };
mg_req_flag_h = 'h' %{ message.flags_ref() = *message.flags_ref() | MC_META_COMMANDS_FLAG_RETURN_WAS_HIT; };
mg_req_flag_k = 'k' %{ message.flags_ref() = *message.flags_ref() | MC_META_COMMANDS_FLAG_RETURN_KEY; };
mg_req_flag_l = 'l' %{ message.flags_ref() = *message.flags_ref() | MC_META_COMMANDS_FLAG_RETURN_TIME_SINCE_LAST_ACCESS; };
mg_req_flag_q = 'q' %{
  message.flags_ref() = *message.flags_ref() | MC_META_COMMANDS_FLAG_USE_NOREPLY_SEMANTICS;
  noreply_ = true;
};
mg_req_flag_s = 's' %{ message.flags_ref() = *message.flags_ref() | MC_META_COMMANDS_FLAG_RETURN_ITEM_SIZE; };
mg_req_flag_t = 't' %{ message.flags_ref() = *message.flags_ref() | MC_META_COMMANDS_FLAG_RETURN_ITEM_TTL; };
mg_req_flag_u = 'u' %{ message.flags_ref() = *message.flags_ref() | MC_META_COMMANDS_FLAG_DO_NOT_BUMP_LRU; };
mg_req_flag_v = 'v' %{ message.flags_ref() = *message.flags_ref() | MC_META_COMMANDS_FLAG_RETURN_VALUE; };

# Request flag tokens (with value)
mg_req_flag_C = 'C' uint %{ message.casToken_ref() = currentUInt_; };
mg_req_flag_E = 'E' uint %{ message.newCasToken_ref() = currentUInt_; };
mg_req_flag_N = 'N' uint %{ message.vivifyOnMissTTL_ref() = static_cast<int32_t>(currentUInt_); };
mg_req_flag_R = 'R' uint %{ message.refreshIfTTLLessThan_ref() = static_cast<int32_t>(currentUInt_); };
mg_req_flag_T = 'T' uint %{ message.newTTL_ref() = static_cast<int32_t>(currentUInt_); };
mg_req_flag_O = 'O' (any {1,32} -- (cntrl | space)) >key_start %key_end %{
  currentKey_.coalesce();
  message.opaqueToken_ref() = std::string(
      reinterpret_cast<const char*>(currentKey_.data()), currentKey_.length());
};

# Skip unknown flags
mg_req_unknown = (any - cntrl - space - [bcfhklqstuvCNORT]) (any - cntrl - space)*;

mg_req_flag = mg_req_flag_b | mg_req_flag_c | mg_req_flag_f |
              mg_req_flag_h | mg_req_flag_k | mg_req_flag_l |
              mg_req_flag_q | mg_req_flag_s | mg_req_flag_t |
              mg_req_flag_u | mg_req_flag_v |
              mg_req_flag_C | mg_req_flag_E |mg_req_flag_N | mg_req_flag_R |
              mg_req_flag_T | mg_req_flag_O |
              mg_req_unknown;

req_body := ' '* key (' '+ mg_req_flag)* ' '* new_line @{
              callback_->onRequest(std::move(message), noreply_);
              finishReq();
              fbreak;
            };

write data;
}%%

void McServerAsciiParser::consumeMetaCommandsGet(folly::IOBuf& buffer) {
  auto& message = currentMessage_.get<McMetaCommandsGetRequest>();
  %%{
    machine mc_ascii_mg_req_body;
    write init nocs;
    write exec;
  }%%
}

// Operation keyword parser.

%%{
machine mc_ascii_req_type;

# Define binding to class member variables.
variable p p_;
variable pe pe_;
variable cs savedCs_;

new_line = '\r'? '\n';

get = 'get ' @{
  initGetLike<McGetRequest>();
  fbreak;
};

gets = 'gets ' @{
  initGetLike<McGetsRequest>();
  fbreak;
};

lease_get = 'lease-get ' @{
  initGetLike<McLeaseGetRequest>();
  fbreak;
};

metaget = 'metaget ' @{
  initGetLike<McMetagetRequest>();
  fbreak;
};

mg = 'mg ' @{
  savedCs_ = mc_ascii_mg_req_body_en_req_body;
  errorCs_ = mc_ascii_mg_req_body_error;
  state_ = State::PARTIAL;
  currentMessage_.emplace<McMetaCommandsGetRequest>();
  consumer_ = &McServerAsciiParser::consumeMetaCommandsGet;
  fbreak;
};

gat = 'gat ' @{
  initGatLike<McGatRequest>();
  fbreak;
};

gats = 'gats ' @{
  initGatLike<McGatsRequest>();
  fbreak;
};

set = 'set ' @{
  initSetLike<McSetRequest>();
  fbreak;
};

add = 'add ' @{
  initSetLike<McAddRequest>();
  fbreak;
};

replace = 'replace ' @{
  initSetLike<McReplaceRequest>();
  fbreak;
};

append = 'append ' @{
  initSetLike<McAppendRequest>();
  fbreak;
};

prepend = 'prepend ' @{
  initSetLike<McPrependRequest>();
  fbreak;
};

cas = 'cas ' @{
  savedCs_ = mc_ascii_cas_req_body_en_req_body;
  errorCs_ = mc_ascii_cas_req_body_error;
  state_ = State::PARTIAL;
  currentMessage_.emplace<McCasRequest>();
  consumer_ = &McServerAsciiParser::consumeCas;
  fbreak;
};

lease_set = 'lease-set ' @{
  savedCs_ = mc_ascii_lease_set_req_body_en_req_body;
  errorCs_ = mc_ascii_lease_set_req_body_error;
  state_ = State::PARTIAL;
  currentMessage_.emplace<McLeaseSetRequest>();
  consumer_ = &McServerAsciiParser::consumeLeaseSet;
  fbreak;
};

delete = 'delete ' @{
  savedCs_ = mc_ascii_delete_req_body_en_req_body;
  errorCs_ = mc_ascii_delete_req_body_error;
  state_ = State::PARTIAL;
  currentMessage_.emplace<McDeleteRequest>();
  consumer_ = &McServerAsciiParser::consumeDelete;
  fbreak;
};

touch = 'touch ' @{
  savedCs_ = mc_ascii_touch_req_body_en_req_body;
  errorCs_ = mc_ascii_touch_req_body_error;
  state_ = State::PARTIAL;
  currentMessage_.emplace<McTouchRequest>();
  consumer_ = &McServerAsciiParser::consumeTouch;
  fbreak;
};

shutdown = 'shutdown' @{
  savedCs_ = mc_ascii_shutdown_req_body_en_req_body;
  errorCs_ = mc_ascii_shutdown_req_body_error;
  state_ = State::PARTIAL;
  currentMessage_.emplace<McShutdownRequest>();
  consumer_ = &McServerAsciiParser::consumeShutdown;
  fbreak;
};

incr = 'incr ' @{
  initArithmetic<McIncrRequest>();
  fbreak;
};

decr = 'decr ' @{
  initArithmetic<McDecrRequest>();
  fbreak;
};

version = 'version' ' '* new_line @{
  callback_->onRequest(McVersionRequest());
  finishReq();
  fbreak;
};

quit = 'quit' ' '* new_line @{
  callback_->onRequest(McQuitRequest(),
                       true /* noReply */);
  finishReq();
  fbreak;
};

stats = 'stats' @{
  savedCs_ = mc_ascii_stats_req_body_en_req_body;
  errorCs_ = mc_ascii_stats_req_body_error;
  state_ = State::PARTIAL;
  currentMessage_.emplace<McStatsRequest>();
  consumer_ = &McServerAsciiParser::consumeStats;
  fbreak;
};

exec = ('exec ' | 'admin ') @{
  savedCs_ = mc_ascii_exec_req_body_en_req_body;
  errorCs_ = mc_ascii_exec_req_body_error;
  state_ = State::PARTIAL;
  currentMessage_.emplace<McExecRequest>();
  consumer_ = &McServerAsciiParser::consumeExec;
  fbreak;
};

flush_re = 'flush_regex ' @{
  savedCs_ = mc_ascii_flush_re_req_body_en_req_body;
  errorCs_ = mc_ascii_flush_re_req_body_error;
  state_ = State::PARTIAL;
  currentMessage_.emplace<McFlushReRequest>();
  consumer_ = &McServerAsciiParser::consumeFlushRe;
  fbreak;
};

flush_all = 'flush_all' @{
  savedCs_ = mc_ascii_flush_all_req_body_en_req_body;
  errorCs_ = mc_ascii_flush_all_req_body_error;
  state_ = State::PARTIAL;
  currentMessage_.emplace<McFlushAllRequest>();
  consumer_ = &McServerAsciiParser::consumeFlushAll;
  fbreak;
};

command := get | gets | lease_get | metaget | mg | set | add | replace | append |
           prepend | cas | lease_set | delete | shutdown | incr | decr |
           version | quit | stats | exec | flush_re | flush_all | touch | gat | gats;

write data;
}%%

void McServerAsciiParser::opTypeConsumer(folly::IOBuf&) {
  %%{
    machine mc_ascii_req_type;
    write init nocs;
    write exec;
  }%%
}

void McServerAsciiParser::finishReq() {
  state_ = State::UNINIT;
}

McAsciiParserBase::State McServerAsciiParser::consume(folly::IOBuf& buffer) {
  assert(state_ != State::ERROR);
  assert(state_ != State::COMPLETE);
  assert(!hasReadBuffer());
  p_ = reinterpret_cast<const char*>(buffer.data());
  pe_ = p_ + buffer.length();

  while (p_ != pe_) {
    // Initialize operation parser.
    if (state_ == State::UNINIT) {
      savedCs_ = mc_ascii_req_type_en_command;
      errorCs_ = mc_ascii_req_type_error;

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
