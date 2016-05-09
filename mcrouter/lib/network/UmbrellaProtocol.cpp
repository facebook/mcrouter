/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "UmbrellaProtocol.h"

#include <folly/Bits.h>
#include <folly/GroupVarint.h>
#include <folly/io/IOBuf.h>
#include <folly/Varint.h>

#include "mcrouter/lib/mc/umbrella.h"
#include "mcrouter/lib/McReply.h"
#include "mcrouter/lib/McRequest.h"

#ifndef LIBMC_FBTRACE_DISABLE
#include "mcrouter/lib/mc/mc_fbtrace_info.h"
#endif

static_assert(
  mc_nops == 28,
  "If you add a new mc_op, make sure to update lib/mc/umbrella_conv.h");

static_assert(
  UM_NOPS == 29,
  "If you add a new mc_op, make sure to update lib/mc/umbrella_conv.h");

static_assert(
  mc_nres == 32,
  "If you add a new mc_res, make sure to update lib/mc/umbrella_conv.h");

namespace facebook { namespace memcache {

namespace {

/**
 * Gets the value of the tag specified.
 *
 * @param header, nheader [header, header + nheader) must point to a valid
 *                        Umbrella header.
 * @param tag             Desired tag.
 * @param val             Output parameter containing the value of the tag.
 * @return                True if the tag was found. False otherwise.
 * @throw                 std::runtime_error on any parse error.
 */
bool umbrellaGetTagValue(const uint8_t* header, size_t nheader,
                         size_t tag, uint64_t& val) {
  auto msg = reinterpret_cast<const entry_list_msg_t*>(header);
  size_t nentries = folly::Endian::big((uint16_t)msg->nentries);
  if (reinterpret_cast<const uint8_t*>(&msg->entries[nentries])
      != header + nheader) {
    throw std::runtime_error("Invalid number of entries");
  }
  for (size_t i = 0; i < nentries; ++i) {
    auto& entry = msg->entries[i];
    size_t curTag = folly::Endian::big((uint16_t)entry.tag);
    if (curTag == tag) {
      val = folly::Endian::big((uint64_t)entry.data.val);
      return true;
    }
  }
  return false;
}

void resetAdditionalFields(UmbrellaMessageInfo& info) {
  info.traceId = 0;
  info.supportedCodecsFirstId = 0;
  info.supportedCodecsSize = 0;
  info.usedCodecId = 0;
  info.uncompressedBodySize = 0;
}

size_t getNumAdditionalFields(const UmbrellaMessageInfo& info) {
  size_t nAdditionalFields = 0;
  if (info.traceId != 0) {
    ++nAdditionalFields;
  }
  if (info.supportedCodecsFirstId != 0) {
    ++nAdditionalFields;
  }
  if (info.supportedCodecsSize != 0) {
    ++nAdditionalFields;
  }
  if (info.usedCodecId != 0) {
    ++nAdditionalFields;
  }
  if (info.uncompressedBodySize != 0) {
    ++nAdditionalFields;
  }
  return nAdditionalFields;
}

/**
 * Serialize the additional field and return the number of bytes used to
 * serialize it.
 */
size_t serializeAdditionalFieldIfNonZero(
    uint8_t* destination,
    CaretAdditionalFieldType type,
    uint64_t value) {
  uint8_t* buf = destination;
  if (value > 0) {
    buf += folly::encodeVarint(static_cast<uint64_t>(type), buf);
    buf += folly::encodeVarint(value, buf);
  }
  return buf - destination;
}

size_t serializeAdditionalFields(
    uint8_t* destination,
    const UmbrellaMessageInfo& info) {
  uint8_t* buf = destination;

  buf += serializeAdditionalFieldIfNonZero(
      buf,
      CaretAdditionalFieldType::TRACE_ID,
      info.traceId);
  buf += serializeAdditionalFieldIfNonZero(
      buf,
      CaretAdditionalFieldType::SUPPORTED_CODECS_FIRST_ID,
      info.supportedCodecsFirstId);
  buf += serializeAdditionalFieldIfNonZero(
      buf,
      CaretAdditionalFieldType::SUPPORTED_CODECS_SIZE,
      info.supportedCodecsSize);
  buf += serializeAdditionalFieldIfNonZero(
      buf,
      CaretAdditionalFieldType::USED_CODEC_ID,
      info.usedCodecId);
  buf += serializeAdditionalFieldIfNonZero(
      buf,
      CaretAdditionalFieldType::UNCOMPRESSED_BODY_SIZE,
      info.uncompressedBodySize);

  return buf - destination;
}

} // anonymous namespace

UmbrellaParseStatus umbrellaParseHeader(const uint8_t* buf, size_t nbuf,
                                        UmbrellaMessageInfo& infoOut) {

  if (nbuf == 0) {
    return UmbrellaParseStatus::NOT_ENOUGH_DATA;
  }

  if (buf[0] == ENTRY_LIST_MAGIC_BYTE) {
    infoOut.version = UmbrellaVersion::BASIC;
  } else {
    return UmbrellaParseStatus::MESSAGE_PARSE_ERROR;
  }

  if (infoOut.version == UmbrellaVersion::BASIC) {
    /* Basic version layout:
         }0NNSSSS, <um_elist_entry_t>*nentries, body
       Where N is nentries and S is message size, both big endian */
    entry_list_msg_t* header = (entry_list_msg_t*)buf;
    if (nbuf < sizeof(entry_list_msg_t)) {
      return UmbrellaParseStatus::NOT_ENOUGH_DATA;
    }
    size_t messageSize = folly::Endian::big<uint32_t>(header->total_size);
    uint16_t nentries = folly::Endian::big<uint16_t>(header->nentries);

    infoOut.headerSize = sizeof(entry_list_msg_t) +
      sizeof(um_elist_entry_t) * nentries;
    if (infoOut.headerSize > messageSize) {
      return UmbrellaParseStatus::MESSAGE_PARSE_ERROR;
    }
    infoOut.bodySize = messageSize - infoOut.headerSize;
  } else {
    return UmbrellaParseStatus::MESSAGE_PARSE_ERROR;
  }

  return UmbrellaParseStatus::OK;
}

UmbrellaParseStatus caretParseHeader(const uint8_t* buff,
                                     size_t nbuf,
                                     UmbrellaMessageInfo& headerInfo) {

  /* we need the magic byte and the first byte of encoded header
     to determine if we have enough data in the buffer to get the
     entire header */
  if (nbuf < 2) {
    return UmbrellaParseStatus::NOT_ENOUGH_DATA;
  }

  if (buff[0] != kCaretMagicByte) {
    return UmbrellaParseStatus::MESSAGE_PARSE_ERROR;
  }

  const char* buf = reinterpret_cast<const char*>(buff);
  size_t encodedLength = folly::GroupVarint32::encodedSize(buf + 1);

  if (nbuf < encodedLength + 1) {
    return UmbrellaParseStatus::NOT_ENOUGH_DATA;
  }

  uint32_t additionalFields;
  folly::GroupVarint32::decode_simple(
      buf + 1,
      &headerInfo.bodySize,
      &headerInfo.typeId,
      &headerInfo.reqId,
      &additionalFields);

  folly::StringPiece range(buf, nbuf);
  range.advance(encodedLength + 1);

  // Additional fields are sequence of (key,value) pairs
  resetAdditionalFields(headerInfo);
  for (uint32_t i = 0; i < additionalFields; i++) {
    try {
      uint64_t fieldType = folly::decodeVarint(range);
      uint64_t fieldValue = folly::decodeVarint(range);

      if (fieldType > static_cast<uint64_t>(
                          CaretAdditionalFieldType::UNCOMPRESSED_BODY_SIZE)) {
        // Additional Field Type not recognized, ignore.
        continue;
      }

      switch (static_cast<CaretAdditionalFieldType>(fieldType)) {
        case CaretAdditionalFieldType::TRACE_ID:
          headerInfo.traceId = fieldValue;
          break;
        case CaretAdditionalFieldType::SUPPORTED_CODECS_FIRST_ID:
          headerInfo.supportedCodecsFirstId = fieldValue;
          break;
        case CaretAdditionalFieldType::SUPPORTED_CODECS_SIZE:
          headerInfo.supportedCodecsSize = fieldValue;
          break;
        case CaretAdditionalFieldType::USED_CODEC_ID:
          headerInfo.usedCodecId = fieldValue;
          break;
        case CaretAdditionalFieldType::UNCOMPRESSED_BODY_SIZE:
          headerInfo.uncompressedBodySize = fieldValue;
          break;
      }
    } catch (const std::invalid_argument& e) {
      // buffer was not sufficient for additional fields
      return UmbrellaParseStatus::NOT_ENOUGH_DATA;
    }
  }

  headerInfo.headerSize = range.cbegin() - buf;

  return UmbrellaParseStatus::OK;
}

size_t caretPrepareHeader(const UmbrellaMessageInfo& info, char* headerBuf) {
  // Header is at most kMaxHeaderLength without extra fields.

  uint32_t bodySize = info.bodySize;
  uint32_t typeId = info.typeId;
  uint32_t reqId = info.reqId;

  headerBuf[0] = kCaretMagicByte;

  // Header
  char* additionalFields = folly::GroupVarint32::encode(
      headerBuf + 1,
      bodySize,
      typeId,
      reqId,
      getNumAdditionalFields(info));

  // Additional fields
  additionalFields += serializeAdditionalFields(
      reinterpret_cast<uint8_t*>(additionalFields), info);

  return additionalFields - headerBuf;
}

uint64_t umbrellaDetermineReqId(const uint8_t* header, size_t nheader) {
  uint64_t id;
  if (!umbrellaGetTagValue(header, nheader, msg_reqid, id)) {
    throw std::runtime_error("missing reqid");
  }
  if (id == 0) {
    throw std::runtime_error("invalid reqid");
  }
  return id;
}

mc_op_t umbrellaDetermineOperation(const uint8_t* header, size_t nheader) {
  uint64_t op;
  if (!umbrellaGetTagValue(header, nheader, msg_op, op)) {
    throw std::runtime_error("missing op");
  }
  if (op >= UM_NOPS) {
    throw std::runtime_error("invalid operation");
  }
  return static_cast<mc_op_t>(detail::kUmbrellaOpToMc[op]);
}

bool umbrellaIsReply(const uint8_t* header, size_t nheader) {
  uint64_t res;
  return umbrellaGetTagValue(header, nheader, msg_result, res);
}

McRequest umbrellaParseRequest(const folly::IOBuf& source,
                               const uint8_t* header, size_t nheader,
                               const uint8_t* body, size_t nbody,
                               mc_op_t& opOut, uint64_t& reqidOut) {
  McRequest req;
  opOut = mc_op_unknown;
  reqidOut = 0;

  auto msg = reinterpret_cast<const entry_list_msg_t*>(header);
  size_t nentries = folly::Endian::big((uint16_t)msg->nentries);
  if (reinterpret_cast<const uint8_t*>(&msg->entries[nentries])
      != header + nheader) {
    throw std::runtime_error("Invalid number of entries");
  }
  for (size_t i = 0; i < nentries; ++i) {
    auto& entry = msg->entries[i];
    size_t tag = folly::Endian::big((uint16_t)entry.tag);
    size_t val = folly::Endian::big((uint64_t)entry.data.val);
    switch (tag) {
      case msg_op:
        if (val >= UM_NOPS) {
          throw std::runtime_error("op out of range");
        }
        opOut = static_cast<mc_op_t>(umbrella_op_to_mc[val]);
        break;

      case msg_reqid:
        if (val == 0) {
          throw std::runtime_error("invalid reqid");
        }
        reqidOut = val;
        break;

      case msg_flags:
        req.setFlags(val);
        break;

      case msg_exptime:
        req.setExptime(val);
        break;

      case msg_delta:
        req.setDelta(val);
        break;

      case msg_cas:
        req.setCas(val);
        break;

      case msg_lease_id:
        req.setLeaseToken(val);
        break;

      case msg_key:
        if (!req.setKeyFrom(
              source, body +
              folly::Endian::big((uint32_t)entry.data.str.offset),
              folly::Endian::big((uint32_t)entry.data.str.len) - 1)) {
          throw std::runtime_error("Key: invalid offset/length");
        }
        break;

      case msg_value:
        if (!req.setValueFrom(
              source, body +
              folly::Endian::big((uint32_t)entry.data.str.offset),
              folly::Endian::big((uint32_t)entry.data.str.len) - 1)) {
          throw std::runtime_error("Value: invalid offset/length");
        }
        break;

#ifndef LIBMC_FBTRACE_DISABLE
      case msg_fbtrace:
      {
        auto off = folly::Endian::big((uint32_t)entry.data.str.offset);
        auto len = folly::Endian::big((uint32_t)entry.data.str.len) - 1;

        if (len > FBTRACE_METADATA_SZ) {
          throw std::runtime_error("Fbtrace metadata too large");
        }
        if (off + len > nbody || off + len < off) {
          throw std::runtime_error("Fbtrace metadata field invalid");
        }
        auto fbtraceInfo = new_mc_fbtrace_info(0);
        memcpy(fbtraceInfo->metadata, body + off, len);
        req.setFbtraceInfo(fbtraceInfo);
        break;
      }
#endif

      default:
        /* Ignore unknown tags silently */
        break;
    }
  }

  if (opOut == mc_op_unknown) {
    throw std::runtime_error("Request missing operation");
  }

  if (!reqidOut) {
    throw std::runtime_error("Request missing reqid");
  }

  return req;
}

UmbrellaSerializedMessage::UmbrellaSerializedMessage() noexcept {
  /* These will not change from message to message */
  msg_.msg_header.magic_byte = ENTRY_LIST_MAGIC_BYTE;
  msg_.msg_header.version = UMBRELLA_VERSION_BASIC;

  iovs_[0].iov_base = &msg_;
  iovs_[0].iov_len = sizeof(msg_);

  iovs_[1].iov_base = entries_;
}

void UmbrellaSerializedMessage::clear() {
  nEntries_ = nStrings_ = offset_ = 0;
  error_ = false;

  iobuf_.clear();
  auxString_.clear();
}

void UmbrellaSerializedMessage::appendInt(
  entry_type_t type, int32_t tag, uint64_t val) {

  if (nEntries_ >= kInlineEntries) {
    error_ = true;
    return;
  }

  um_elist_entry_t& entry = entries_[nEntries_++];
  entry.type = folly::Endian::big((uint16_t)type);
  entry.tag = folly::Endian::big((uint16_t)tag);
  entry.data.val = folly::Endian::big((uint64_t)val);
}

void UmbrellaSerializedMessage::appendString(
  int32_t tag, const uint8_t* data, size_t len, entry_type_t type) {

  if (nStrings_ >= kInlineStrings) {
    error_ = true;
    return;
  }

  strings_[nStrings_++] = folly::StringPiece((const char*)data, len);

  um_elist_entry_t& entry = entries_[nEntries_++];
  entry.type = folly::Endian::big((uint16_t)type);
  entry.tag = folly::Endian::big((uint16_t)tag);
  entry.data.str.offset = folly::Endian::big((uint32_t)offset_);
  entry.data.str.len = folly::Endian::big((uint32_t)(len + 1));
  offset_ += len + 1;
}

size_t UmbrellaSerializedMessage::finalizeMessage() {
  static char nul = '\0';

  size_t size = sizeof(entry_list_msg_t) +
    sizeof(um_elist_entry_t) * nEntries_ +
    offset_;

  msg_.total_size = folly::Endian::big((uint32_t)size);
  msg_.nentries = folly::Endian::big((uint16_t)nEntries_);

  iovs_[1].iov_len = sizeof(um_elist_entry_t) * nEntries_;
  size_t niovOut = 2;

  for (size_t i = 0; i < nStrings_; i++) {
    iovs_[niovOut].iov_base = (char *)strings_[i].begin();
    iovs_[niovOut].iov_len = strings_[i].size();
    niovOut++;

    iovs_[niovOut].iov_base = &nul;
    iovs_[niovOut].iov_len = 1;
    niovOut++;
  }
  return niovOut;
}

}} // facebook::memcache
