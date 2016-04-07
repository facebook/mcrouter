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

#include "mcrouter/lib/mc/msg.h"
#include "mcrouter/lib/McOperation.h"
#include "mcrouter/lib/McReply.h"
#include "mcrouter/lib/McRequest.h"
#include "mcrouter/lib/network/gen-cpp2/mc_caret_protocol_types.h"
#include "mcrouter/lib/network/TypedThriftMessage.h"

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
  // TODO(jmswen) We can remove this empty setValue hack once we've totally
  // moved to Caret
  message.setValue(folly::IOBuf());
  if (!readValue(buffer, *message.valuePtrUnsafe())) {
    fbreak;
  }
}

# Action that initializes/performs data parsing for requests.
action req_value_data {
  // Note: order is important here. Must set __isset.value = true before
  // possibly breaking.
  message->__isset.value = true;
  if (!readValue(buffer, message->value)) {
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

delay = uint %{
  message->set_delay(currentUInt_);
};

exptime = uint %{
  message->set_exptime(static_cast<int32_t>(currentUInt_));
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
  message->set_casToken(currentUInt_);
};

delta = uint %{
  message->set_delta(currentUInt_);
};

lease_token = uint %{
  // NOTE: we don't support -1 lease token.
  message->set_leaseToken(currentUInt_);
};

error_code = uint %{
  message.setAppSpecificErrorCode(static_cast<uint16_t>(currentUInt_));
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

error_message = (any* -- ('\r' | '\n')) >reset_value ${
  using MsgT = typename std::decay<decltype(message)>::type;
  consumeErrorMessage<MsgT>(buffer);
};

server_error = 'SERVER_ERROR' (' ' error_code ' ')? ' '? error_message
               %{
                 uint32_t errorCode = currentUInt_;
                 if (errorCode == SERVER_ERROR_BUSY) {
                   message.setResult(mc_res_busy);
                 } else {
                   message.setResult(mc_res_remote_error);
                 }
               };

client_error = 'CLIENT_ERROR' (' ' error_code ' ')? ' '? error_message
               %{ message.setResult(mc_res_client_error); };

error = command_error | server_error | client_error;
}%%

// McGet reply.
%%{
machine mc_ascii_get_reply;
include mc_ascii_common;

get = hit? >{ message.setResult(mc_res_notfound); } 'END';
get_reply := (get | error) msg_end;
write data;
}%%

template <>
void McClientAsciiParser::consumeMessage<McRequestWithMcOp<mc_op_get>>(
    folly::IOBuf& buffer) {
  McReply& message = currentMessage_.get<McReply>();
  %%{
    machine mc_ascii_get_reply;
    write init nocs;
    write exec;
  }%%
}

template <>
void McClientAsciiParser::consumeMessage<
    TypedThriftRequest<cpp2::McGetRequest>>(folly::IOBuf& buffer) {
  using Request = TypedThriftRequest<cpp2::McGetRequest>;

  auto& message = currentMessage_.get<ReplyT<Request>>();
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

template <>
void McClientAsciiParser::consumeMessage<McRequestWithMcOp<mc_op_gets>>(
    folly::IOBuf& buffer) {
  McReply& message = currentMessage_.get<McReply>();
  %%{
    machine mc_ascii_gets_reply;
    write init nocs;
    write exec;
  }%%
}

template <>
void McClientAsciiParser::consumeMessage<
    TypedThriftRequest<cpp2::McGetsRequest>>(folly::IOBuf& buffer) {
  using Request = TypedThriftRequest<cpp2::McGetsRequest>;

  auto& message = currentMessage_.get<ReplyT<Request>>();
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

template <>
void McClientAsciiParser::consumeMessage<McRequestWithMcOp<mc_op_lease_get>>(
    folly::IOBuf& buffer) {
  McReply& message = currentMessage_.get<McReply>();
  %%{
    machine mc_ascii_lease_get_reply;
    write init nocs;
    write exec;
  }%%
}

template <>
void McClientAsciiParser::consumeMessage<
    TypedThriftRequest<cpp2::McLeaseGetRequest>>(folly::IOBuf& buffer) {
  using Request = TypedThriftRequest<cpp2::McLeaseGetRequest>;

  auto& message = currentMessage_.get<ReplyT<Request>>();
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

arithm = not_found | (delta @{ message.setResult(mc_res_stored); }) ' '*;
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

version = 'VERSION ' @{ message.setResult(mc_res_ok); }
          (any* -- ('\r' | '\n')) ${
  using MsgT = std::decay<decltype(message)>::type;
  consumeVersion<MsgT>(buffer);
};
version_reply := (version | error) msg_end;

write data;
}%%

template <>
void McClientAsciiParser::consumeMessage<McRequestWithMcOp<mc_op_version>>(
    folly::IOBuf& buffer) {
  McReply& message = currentMessage_.get<McReply>();
  %%{
    machine mc_ascii_version_reply;
    write init nocs;
    write exec;
  }%%
}

template <>
void McClientAsciiParser::consumeMessage<
    TypedThriftRequest<cpp2::McVersionRequest>>(folly::IOBuf& buffer) {
  using Request = TypedThriftRequest<cpp2::McVersionRequest>;

  auto& message = currentMessage_.get<ReplyT<Request>>();
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
void McClientAsciiParser::consumeMessage<McRequestWithMcOp<mc_op_delete>>(
    folly::IOBuf& buffer) {
  McReply& message = currentMessage_.get<McReply>();
  %%{
    machine mc_ascii_delete_reply;
    write init nocs;
    write exec;
  }%%
}

template <>
void McClientAsciiParser::consumeMessage<
    TypedThriftRequest<cpp2::McDeleteRequest>>(folly::IOBuf& buffer) {
  using Request = TypedThriftRequest<cpp2::McDeleteRequest>;

  auto& message = currentMessage_.get<ReplyT<Request>>();
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
void McClientAsciiParser::consumeMessage<McRequestWithMcOp<mc_op_touch>>(
    folly::IOBuf& buffer) {
  McReply& message = currentMessage_.get<McReply>();
  %%{
    machine mc_ascii_touch_reply;
    write init nocs;
    write exec;
  }%%
}

template <>
void McClientAsciiParser::consumeMessage<
    TypedThriftRequest<cpp2::McTouchRequest>>(folly::IOBuf& buffer) {
  using Request = TypedThriftRequest<cpp2::McTouchRequest>;

  auto& message = currentMessage_.get<ReplyT<Request>>();
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
  message->set_age(static_cast<uint32_t>(currentUInt_));
};
age_unknown = 'unknown' %{
  message->set_age(static_cast<uint32_t>(-1));
};

ip_addr = (xdigit | '.' | ':')+ ${
  using MsgT = std::decay<decltype(message)>::type;
  consumeIpAddrHelper<MsgT>(buffer);
} %{
  using MsgT = std::decay<decltype(message)>::type;
  consumeIpAddr<MsgT>(buffer);
};

transient = uint %{
  // McReply 'flags' field is used for is_transient result in metaget hit
  // reply. setFlags is a no-op for typed McMetagetReply; we will not support
  // is_transient in Caret.
  message.setFlags(currentUInt_);
};

meta = 'META' % { message.setResult(mc_res_found); };
mhit = meta ' '+ skip_key ' '+ 'age:' ' '* (age | age_unknown) ';' ' '*
  'exptime:' ' '* exptime ';' ' '* 'from:' ' '* (ip_addr|'unknown') ';' ' '*
  'is_transient:' ' '* transient ' '* new_line;
metaget = mhit? >{ message.setResult(mc_res_notfound); } 'END' msg_end;
metaget_reply := (metaget | error) msg_end;

write data;
}%%
template <>
void McClientAsciiParser::consumeMessage<McRequestWithMcOp<mc_op_metaget>>(
    folly::IOBuf& buffer) {
  McReply& message = currentMessage_.get<McReply>();
  %%{
    machine mc_ascii_metaget_reply;
    write init nocs;
    write exec;
  }%%
}

template <>
void McClientAsciiParser::consumeMessage<
    TypedThriftRequest<cpp2::McMetagetRequest>>(folly::IOBuf& buffer) {
  using Request = TypedThriftRequest<cpp2::McMetagetRequest>;

  auto& message = currentMessage_.get<ReplyT<Request>>();
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

template <>
void McClientAsciiParser::consumeMessage<McRequestWithMcOp<mc_op_flushall>>(
    folly::IOBuf& buffer) {
  McReply& message = currentMessage_.get<McReply>();
  %%{
    machine mc_ascii_flushall_reply;
    write init nocs;
    write exec;
  }%%
}

template <>
void McClientAsciiParser::consumeMessage<
    TypedThriftRequest<cpp2::McFlushAllRequest>>(folly::IOBuf& buffer) {
  using Request = TypedThriftRequest<cpp2::McFlushAllRequest>;

  auto& message = currentMessage_.get<ReplyT<Request>>();
  %%{
    machine mc_ascii_flushall_reply;
    write init nocs;
    write exec;
  }%%
}

template <>
void McClientAsciiParser::initializeReplyParser<
    McRequestWithMcOp<mc_op_get>>() {

  using Request = McRequestWithMcOp<mc_op_get>;

  initializeCommon<ReplyT<Request>>();
  savedCs_ = mc_ascii_get_reply_en_get_reply;
  errorCs_ = mc_ascii_get_reply_error;
  consumer_ = &McClientAsciiParser::consumeMessage<Request>;
}

template <>
void McClientAsciiParser::initializeReplyParser<
    TypedThriftRequest<cpp2::McGetRequest>>() {

  using Request = TypedThriftRequest<cpp2::McGetRequest>;

  initializeCommon<ReplyT<Request>>();
  savedCs_ = mc_ascii_get_reply_en_get_reply;
  errorCs_ = mc_ascii_get_reply_error;
  consumer_ = &McClientAsciiParser::consumeMessage<Request>;
}

template <>
void McClientAsciiParser::initializeReplyParser<
    McRequestWithMcOp<mc_op_gets>>() {

  using Request = McRequestWithMcOp<mc_op_gets>;

  initializeCommon<ReplyT<Request>>();
  savedCs_ = mc_ascii_gets_reply_en_gets_reply;
  errorCs_ = mc_ascii_gets_reply_error;
  consumer_ = &McClientAsciiParser::consumeMessage<Request>;
}

template <>
void McClientAsciiParser::initializeReplyParser<
    TypedThriftRequest<cpp2::McGetsRequest>>() {

  using Request = TypedThriftRequest<cpp2::McGetsRequest>;

  initializeCommon<ReplyT<Request>>();
  savedCs_ = mc_ascii_gets_reply_en_gets_reply;
  errorCs_ = mc_ascii_gets_reply_error;
  consumer_ = &McClientAsciiParser::consumeMessage<Request>;
}

template <>
void McClientAsciiParser::initializeReplyParser<
    McRequestWithMcOp<mc_op_lease_get>>() {

  using Request = McRequestWithMcOp<mc_op_lease_get>;

  initializeCommon<ReplyT<Request>>();
  savedCs_ = mc_ascii_lease_get_reply_en_lease_get_reply;
  errorCs_ = mc_ascii_lease_get_reply_error;
  consumer_ = &McClientAsciiParser::consumeMessage<Request>;
}

template <>
void McClientAsciiParser::initializeReplyParser<
    TypedThriftRequest<cpp2::McLeaseGetRequest>>() {

  using Request = TypedThriftRequest<cpp2::McLeaseGetRequest>;

  initializeCommon<ReplyT<Request>>();
  savedCs_ = mc_ascii_lease_get_reply_en_lease_get_reply;
  errorCs_ = mc_ascii_lease_get_reply_error;
  consumer_ = &McClientAsciiParser::consumeMessage<Request>;
}

template <>
void McClientAsciiParser::initializeReplyParser<
    McRequestWithMcOp<mc_op_set>>() {

  using Request = McRequestWithMcOp<mc_op_set>;

  initializeStorageReplyCommon<ReplyT<Request>>();
}

template <>
void McClientAsciiParser::initializeReplyParser<
    TypedThriftRequest<cpp2::McSetRequest>>() {

  using Request = TypedThriftRequest<cpp2::McSetRequest>;

  initializeStorageReplyCommon<ReplyT<Request>>();
}

template <>
void McClientAsciiParser::initializeReplyParser<
    McRequestWithMcOp<mc_op_add>>() {

  using Request = McRequestWithMcOp<mc_op_add>;

  initializeStorageReplyCommon<ReplyT<Request>>();
}

template <>
void McClientAsciiParser::initializeReplyParser<
    TypedThriftRequest<cpp2::McAddRequest>>() {

  using Request = TypedThriftRequest<cpp2::McAddRequest>;

  initializeStorageReplyCommon<ReplyT<Request>>();
}

template <>
void McClientAsciiParser::initializeReplyParser<
    McRequestWithMcOp<mc_op_replace>>() {

  using Request = McRequestWithMcOp<mc_op_replace>;

  initializeStorageReplyCommon<ReplyT<Request>>();
}

template <>
void McClientAsciiParser::initializeReplyParser<
    TypedThriftRequest<cpp2::McReplaceRequest>>() {

  using Request = TypedThriftRequest<cpp2::McReplaceRequest>;

  initializeStorageReplyCommon<ReplyT<Request>>();
}

template <>
void McClientAsciiParser::initializeReplyParser<
    McRequestWithMcOp<mc_op_lease_set>>() {

  using Request = McRequestWithMcOp<mc_op_lease_set>;

  initializeStorageReplyCommon<ReplyT<Request>>();
}

template <>
void McClientAsciiParser::initializeReplyParser<
    TypedThriftRequest<cpp2::McLeaseSetRequest>>() {

  using Request = TypedThriftRequest<cpp2::McLeaseSetRequest>;

  initializeStorageReplyCommon<ReplyT<Request>>();
}

template <>
void McClientAsciiParser::initializeReplyParser<
    McRequestWithMcOp<mc_op_cas>>() {

  using Request = McRequestWithMcOp<mc_op_cas>;

  initializeStorageReplyCommon<ReplyT<Request>>();
}

template <>
void McClientAsciiParser::initializeReplyParser<
    TypedThriftRequest<cpp2::McCasRequest>>() {

  using Request = TypedThriftRequest<cpp2::McCasRequest>;

  initializeStorageReplyCommon<ReplyT<Request>>();
}

template <>
void McClientAsciiParser::initializeReplyParser<
    McRequestWithMcOp<mc_op_append>>() {

  using Request = McRequestWithMcOp<mc_op_append>;

  initializeStorageReplyCommon<ReplyT<Request>>();
}

template <>
void McClientAsciiParser::initializeReplyParser<
    TypedThriftRequest<cpp2::McAppendRequest>>() {

  using Request = TypedThriftRequest<cpp2::McAppendRequest>;

  initializeStorageReplyCommon<ReplyT<Request>>();
}

template <>
void McClientAsciiParser::initializeReplyParser<
    McRequestWithMcOp<mc_op_prepend>>() {

  using Request = McRequestWithMcOp<mc_op_prepend>;

  initializeStorageReplyCommon<ReplyT<Request>>();
}

template <>
void McClientAsciiParser::initializeReplyParser<
    TypedThriftRequest<cpp2::McPrependRequest>>() {

  using Request = TypedThriftRequest<cpp2::McPrependRequest>;

  initializeStorageReplyCommon<ReplyT<Request>>();
}

template <>
void McClientAsciiParser::initializeReplyParser<
    McRequestWithMcOp<mc_op_incr>>() {

  using Request = McRequestWithMcOp<mc_op_incr>;

  initializeArithmReplyCommon<ReplyT<Request>>();
}

template <>
void McClientAsciiParser::initializeReplyParser<
    TypedThriftRequest<cpp2::McIncrRequest>>() {

  using Request = TypedThriftRequest<cpp2::McIncrRequest>;

  initializeArithmReplyCommon<ReplyT<Request>>();
}

template <>
void McClientAsciiParser::initializeReplyParser<
    McRequestWithMcOp<mc_op_decr>>() {

  using Request = McRequestWithMcOp<mc_op_decr>;

  initializeArithmReplyCommon<ReplyT<Request>>();
}

template <>
void McClientAsciiParser::initializeReplyParser<
    TypedThriftRequest<cpp2::McDecrRequest>>() {

  using Request = TypedThriftRequest<cpp2::McDecrRequest>;

  initializeArithmReplyCommon<ReplyT<Request>>();
}

template <>
void McClientAsciiParser::initializeReplyParser<
    McRequestWithMcOp<mc_op_version>>() {

  using Request = McRequestWithMcOp<mc_op_version>;

  initializeCommon<ReplyT<Request>>();
  savedCs_ = mc_ascii_version_reply_en_version_reply;
  errorCs_ = mc_ascii_version_reply_error;
  consumer_ = &McClientAsciiParser::consumeMessage<Request>;
}

template <>
void McClientAsciiParser::initializeReplyParser<
    TypedThriftRequest<cpp2::McVersionRequest>>() {

  using Request = TypedThriftRequest<cpp2::McVersionRequest>;

  initializeCommon<ReplyT<Request>>();
  savedCs_ = mc_ascii_version_reply_en_version_reply;
  errorCs_ = mc_ascii_version_reply_error;
  consumer_ = &McClientAsciiParser::consumeMessage<Request>;
}

template <>
void McClientAsciiParser::initializeReplyParser<
    McRequestWithMcOp<mc_op_delete>>() {

  using Request = McRequestWithMcOp<mc_op_delete>;

  initializeCommon<ReplyT<Request>>();
  savedCs_ = mc_ascii_delete_reply_en_delete_reply;
  errorCs_ = mc_ascii_delete_reply_error;
  consumer_ = &McClientAsciiParser::consumeMessage<Request>;
}

template <>
void McClientAsciiParser::initializeReplyParser<
    TypedThriftRequest<cpp2::McDeleteRequest>>() {

  using Request = TypedThriftRequest<cpp2::McDeleteRequest>;

  initializeCommon<ReplyT<Request>>();
  savedCs_ = mc_ascii_delete_reply_en_delete_reply;
  errorCs_ = mc_ascii_delete_reply_error;
  consumer_ = &McClientAsciiParser::consumeMessage<Request>;
}

template <>
void McClientAsciiParser::initializeReplyParser<
    McRequestWithMcOp<mc_op_touch>>() {

  using Request = McRequestWithMcOp<mc_op_touch>;

  initializeCommon<ReplyT<Request>>();
  savedCs_ = mc_ascii_touch_reply_en_touch_reply;
  errorCs_ = mc_ascii_touch_reply_error;
  consumer_ = &McClientAsciiParser::consumeMessage<Request>;
}

template <>
void McClientAsciiParser::initializeReplyParser<
    TypedThriftRequest<cpp2::McTouchRequest>>() {

  using Request = TypedThriftRequest<cpp2::McTouchRequest>;

  initializeCommon<ReplyT<Request>>();
  savedCs_ = mc_ascii_touch_reply_en_touch_reply;
  errorCs_ = mc_ascii_touch_reply_error;
  consumer_ = &McClientAsciiParser::consumeMessage<Request>;
}

template <>
void McClientAsciiParser::initializeReplyParser<
    McRequestWithMcOp<mc_op_metaget>>() {

  using Request = McRequestWithMcOp<mc_op_metaget>;

  initializeCommon<ReplyT<Request>>();
  // Since mc_op_metaget has A LOT of specific fields, just create McMsgRef for
  // now.
  auto& reply = currentMessage_.get<ReplyT<Request>>();
  reply.msg_ = createMcMsgRef();
  savedCs_ = mc_ascii_metaget_reply_en_metaget_reply;
  errorCs_ = mc_ascii_metaget_reply_error;
  consumer_ = &McClientAsciiParser::consumeMessage<Request>;
}

template <>
void McClientAsciiParser::initializeReplyParser<
    TypedThriftRequest<cpp2::McMetagetRequest>>() {

  using Request = TypedThriftRequest<cpp2::McMetagetRequest>;

  initializeCommon<ReplyT<Request>>();
  savedCs_ = mc_ascii_metaget_reply_en_metaget_reply;
  errorCs_ = mc_ascii_metaget_reply_error;
  consumer_ = &McClientAsciiParser::consumeMessage<Request>;
}

template <>
void McClientAsciiParser::initializeReplyParser<
    McRequestWithMcOp<mc_op_flushall>>() {

  using Request = McRequestWithMcOp<mc_op_flushall>;

  initializeCommon<ReplyT<Request>>();
  savedCs_ = mc_ascii_flushall_reply_en_flushall_reply;
  errorCs_ = mc_ascii_flushall_reply_error;
  consumer_ = &McClientAsciiParser::consumeMessage<Request>;
}

template <>
void McClientAsciiParser::initializeReplyParser<
    TypedThriftRequest<cpp2::McFlushAllRequest>>() {

  using Request = TypedThriftRequest<cpp2::McFlushAllRequest>;

  initializeCommon<ReplyT<Request>>();
  savedCs_ = mc_ascii_flushall_reply_en_flushall_reply;
  errorCs_ = mc_ascii_flushall_reply_error;
  consumer_ = &McClientAsciiParser::consumeMessage<Request>;
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
  auto& message = currentMessage_.get<TypedThriftRequest<cpp2::McCasRequest>>();
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
    currentMessage_.get<TypedThriftRequest<cpp2::McLeaseSetRequest>>();
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
    currentMessage_.get<TypedThriftRequest<cpp2::McDeleteRequest>>();
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
    currentMessage_.get<TypedThriftRequest<cpp2::McTouchRequest>>();
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

void McServerAsciiParser::consumeShutdown(folly::IOBuf& buffer) {
  auto& message =
    currentMessage_.get<TypedThriftRequest<cpp2::McShutdownRequest>>();
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
    currentMessage_.get<TypedThriftRequest<cpp2::McStatsRequest>>();
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
    currentMessage_.get<TypedThriftRequest<cpp2::McExecRequest>>();
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
    currentMessage_.get<TypedThriftRequest<cpp2::McFlushReRequest>>();
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

void McServerAsciiParser::consumeFlushAll(folly::IOBuf& buffer) {
  auto& message =
    currentMessage_.get<TypedThriftRequest<cpp2::McFlushAllRequest>>();
  %%{
    machine mc_ascii_flush_all_req_body;
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
  initGetLike<TypedThriftRequest<cpp2::McGetRequest>>();
  fbreak;
};

gets = 'gets ' @{
  initGetLike<TypedThriftRequest<cpp2::McGetsRequest>>();
  fbreak;
};

lease_get = 'lease-get ' @{
  initGetLike<TypedThriftRequest<cpp2::McLeaseGetRequest>>();
  fbreak;
};

metaget = 'metaget ' @{
  initGetLike<TypedThriftRequest<cpp2::McMetagetRequest>>();
  fbreak;
};

set = 'set ' @{
  initSetLike<TypedThriftRequest<cpp2::McSetRequest>>();
  fbreak;
};

add = 'add ' @{
  initSetLike<TypedThriftRequest<cpp2::McAddRequest>>();
  fbreak;
};

replace = 'replace ' @{
  initSetLike<TypedThriftRequest<cpp2::McReplaceRequest>>();
  fbreak;
};

append = 'append ' @{
  initSetLike<TypedThriftRequest<cpp2::McAppendRequest>>();
  fbreak;
};

prepend = 'prepend ' @{
  initSetLike<TypedThriftRequest<cpp2::McPrependRequest>>();
  fbreak;
};

cas = 'cas ' @{
  savedCs_ = mc_ascii_cas_req_body_en_req_body;
  errorCs_ = mc_ascii_cas_req_body_error;
  state_ = State::PARTIAL;
  currentMessage_.emplace<TypedThriftRequest<cpp2::McCasRequest>>();
  consumer_ = &McServerAsciiParser::consumeCas;
  fbreak;
};

lease_set = 'lease-set ' @{
  savedCs_ = mc_ascii_lease_set_req_body_en_req_body;
  errorCs_ = mc_ascii_lease_set_req_body_error;
  state_ = State::PARTIAL;
  currentMessage_.emplace<TypedThriftRequest<cpp2::McLeaseSetRequest>>();
  consumer_ = &McServerAsciiParser::consumeLeaseSet;
  fbreak;
};

delete = 'delete ' @{
  savedCs_ = mc_ascii_delete_req_body_en_req_body;
  errorCs_ = mc_ascii_delete_req_body_error;
  state_ = State::PARTIAL;
  currentMessage_.emplace<TypedThriftRequest<cpp2::McDeleteRequest>>();
  consumer_ = &McServerAsciiParser::consumeDelete;
  fbreak;
};

touch = 'touch ' @{
  savedCs_ = mc_ascii_touch_req_body_en_req_body;
  errorCs_ = mc_ascii_touch_req_body_error;
  state_ = State::PARTIAL;
  currentMessage_.emplace<TypedThriftRequest<cpp2::McTouchRequest>>();
  consumer_ = &McServerAsciiParser::consumeTouch;
  fbreak;
};

shutdown = 'shutdown' @{
  savedCs_ = mc_ascii_shutdown_req_body_en_req_body;
  errorCs_ = mc_ascii_shutdown_req_body_error;
  state_ = State::PARTIAL;
  currentMessage_.emplace<TypedThriftRequest<cpp2::McShutdownRequest>>();
  consumer_ = &McServerAsciiParser::consumeShutdown;
  fbreak;
};

incr = 'incr ' @{
  initArithmetic<TypedThriftRequest<cpp2::McIncrRequest>>();
  fbreak;
};

decr = 'decr ' @{
  initArithmetic<TypedThriftRequest<cpp2::McDecrRequest>>();
  fbreak;
};

version = 'version' ' '* new_line @{
  callback_->onRequest(TypedThriftRequest<cpp2::McVersionRequest>());
  finishReq();
  fbreak;
};

quit = 'quit' ' '* new_line @{
  callback_->onRequest(TypedThriftRequest<cpp2::McQuitRequest>(), 
                       true /* noReply */);
  finishReq();
  fbreak;
};

stats = 'stats' @{
  savedCs_ = mc_ascii_stats_req_body_en_req_body;
  errorCs_ = mc_ascii_stats_req_body_error;
  state_ = State::PARTIAL;
  currentMessage_.emplace<TypedThriftRequest<cpp2::McStatsRequest>>();
  consumer_ = &McServerAsciiParser::consumeStats;
  fbreak;
};

exec = ('exec ' | 'admin ') @{
  savedCs_ = mc_ascii_exec_req_body_en_req_body;
  errorCs_ = mc_ascii_exec_req_body_error;
  state_ = State::PARTIAL;
  currentMessage_.emplace<TypedThriftRequest<cpp2::McExecRequest>>();
  consumer_ = &McServerAsciiParser::consumeExec;
  fbreak;
};

flush_re = 'flush_regex ' @{
  savedCs_ = mc_ascii_flush_re_req_body_en_req_body;
  errorCs_ = mc_ascii_flush_re_req_body_error;
  state_ = State::PARTIAL;
  currentMessage_.emplace<TypedThriftRequest<cpp2::McFlushReRequest>>();
  consumer_ = &McServerAsciiParser::consumeFlushRe;
  fbreak;
};

flush_all = 'flush_all' @{
  savedCs_ = mc_ascii_flush_all_req_body_en_req_body;
  errorCs_ = mc_ascii_flush_all_req_body_error;
  state_ = State::PARTIAL;
  currentMessage_.emplace<TypedThriftRequest<cpp2::McFlushAllRequest>>();
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
    machine mc_ascii_req_type;
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
