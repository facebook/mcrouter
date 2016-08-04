/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <algorithm>
#include <cstring>

#include <folly/io/IOBuf.h>

namespace carbon {

template <>
inline std::string CarbonProtocolReader::readBinaryField<std::string>() {
  std::string s;
  auto len = readVarint<uint32_t>();
  while (len > 0) {
    const auto bytes = cursor_.peekBytes();
    const auto nappend = std::min(static_cast<uint32_t>(bytes.size()), len);
    s.append(reinterpret_cast<const char*>(bytes.data()), nappend);
    cursor_.skip(nappend);
    len -= nappend;
  }
  return s;
}

template <>
inline folly::IOBuf CarbonProtocolReader::readBinaryField<folly::IOBuf>() {
  auto len = readVarint<uint32_t>();
  folly::IOBuf buf(folly::IOBuf::CREATE, len);
  while (len > 0) {
    auto* dest = buf.writableTail();
    const auto bytes = cursor_.peekBytes();
    const auto nappend = std::min(static_cast<uint32_t>(bytes.size()), len);
    std::memcpy(dest, bytes.data(), nappend);
    cursor_.skip(nappend);
    buf.append(nappend);
    len -= nappend;
  }
  return buf;
}

template <>
inline char CarbonProtocolReader::readRaw() {
  return readCharField();
}

template <>
inline bool CarbonProtocolReader::readRaw() {
  return readBoolField();
}

template <>
inline int8_t CarbonProtocolReader::readRaw() {
  return readInt8Field();
}

template <>
inline int16_t CarbonProtocolReader::readRaw() {
  return readInt16Field();
}

template <>
inline int32_t CarbonProtocolReader::readRaw() {
  return readInt32Field();
}

template <>
inline int64_t CarbonProtocolReader::readRaw() {
  return readInt64Field();
}

template <>
inline uint8_t CarbonProtocolReader::readRaw() {
  return readUInt8Field();
}

template <>
inline uint16_t CarbonProtocolReader::readRaw() {
  return readUInt16Field();
}

template <>
inline uint32_t CarbonProtocolReader::readRaw() {
  return readUInt32Field();
}

template <>
inline uint64_t CarbonProtocolReader::readRaw() {
  return readUInt64Field();
}

template <>
inline float CarbonProtocolReader::readRaw() {
  return readFloatField();
}

template <>
inline double CarbonProtocolReader::readRaw() {
  return readDoubleField();
}

template <>
inline std::string CarbonProtocolReader::readRaw() {
  return readBinaryField<std::string>();
}

template <>
inline folly::IOBuf CarbonProtocolReader::readRaw() {
  return readBinaryField<folly::IOBuf>();
}

} // carbon
