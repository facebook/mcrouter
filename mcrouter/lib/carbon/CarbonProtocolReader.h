/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <stdint.h>
#include <algorithm>
#include <cstring>
#include <limits>
#include <type_traits>
#include <utility>
#include <vector>

#include <folly/io/Cursor.h>
#include <folly/io/IOBuf.h>
#include <folly/small_vector.h>

#include "mcrouter/lib/carbon/CarbonProtocolCommon.h"
#include "mcrouter/lib/carbon/CarbonProtocolWriter.h"
#include "mcrouter/lib/carbon/Fields.h"
#include "mcrouter/lib/carbon/Result.h"
#include "mcrouter/lib/carbon/SerializationTraits.h"
#include "mcrouter/lib/carbon/Util.h"
#include "mcrouter/lib/fbi/cpp/util.h"

namespace carbon {

using CarbonCursor = folly::io::Cursor;

class CarbonProtocolReader {
 public:
  explicit CarbonProtocolReader(const CarbonCursor& c) : cursor_(c) {}

  void setCursor(const CarbonCursor& c) {
    cursor_ = c;
  }

  const CarbonCursor& cursor() const {
    return cursor_;
  }

  template <class T>
  void readRawInto(std::vector<T>& v) {
    v.clear();
    const auto pr = readVectorFieldSizeAndInnerType();
    const auto len = pr.second;
    v.reserve(len);
    for (size_t i = 0; i < len; ++i) {
      v.emplace_back(readRaw<T>());
    }
  }

  template <
      class T,
      decltype(std::declval<CarbonProtocolWriter>().writeRaw(
          std::declval<T>()))* = nullptr>
  T readRaw() {
    T t = T();
    readRawInto(t);
    return t;
  }

  template <class T>
  typename std::enable_if<std::is_enum<T>::value, void>::type readRawInto(
      T& t) {
    using UnderlyingType = typename std::underlying_type<T>::type;
    t = static_cast<T>(readRaw<UnderlyingType>());
  }

  template <class T>
  typename std::enable_if<IsCarbonStruct<T>::value, void>::type readRawInto(
      T& data) {
    data.deserialize(*this);
  }

  template <class T>
  typename std::enable_if<detail::SerializationTraitsDefined<T>::value, void>::
      type
      readRawInto(T& data) {
    data = SerializationTraits<T>::read(*this);
  }

  // The API of readRawInto() is different than other readRawInto() member
  // functions in order to avoid keeping state in the Reader, which would entail
  // maintaining some extra rarely executed branches.
  void readRawInto(bool& b, const FieldType fieldType) {
    DCHECK(fieldType == FieldType::True || fieldType == FieldType::False);
    b = fieldType == FieldType::True;
  }

  void readRawInto(bool& b) {
    const auto fieldType = static_cast<FieldType>(readByte());
    DCHECK(fieldType == FieldType::True || fieldType == FieldType::False);
    b = fieldType == FieldType::True;
  }

  template <class T>
  typename std::
      enable_if<folly::IsOneOf<T, char, int8_t, uint8_t>::value, void>::type
      readRawInto(T& t) {
    t = readByte();
  }

  template <class T>
  typename std::enable_if<
      folly::
          IsOneOf<T, int16_t, int32_t, int64_t, uint16_t, uint32_t, uint64_t>::
              value,
      void>::type
  readRawInto(T& t) {
    t = readZigzagVarint<T>();
  }

  void readRawInto(float& f) {
    static_assert(
        sizeof(float) == sizeof(uint32_t),
        "Carbon doubles can only be used on platforms where sizeof(float)"
        " == sizeof(uint32_t)");
    static_assert(
        std::numeric_limits<float>::is_iec559,
        "Carbon floats may only be used on platforms using IEC 559 floats");

    const auto bits = cursor_.template readBE<uint32_t>();
    std::memcpy(std::addressof(f), std::addressof(bits), sizeof(f));
  }

  void readRawInto(double& d) {
    static_assert(
        sizeof(double) == sizeof(uint64_t),
        "Carbon doubles can only be used on platforms where sizeof(double)"
        " == sizeof(uint64_t)");
    static_assert(
        std::numeric_limits<double>::is_iec559,
        "Carbon doubles may only be used on platforms using IEC 559 doubles");

    const auto bits = cursor_.template readBE<uint64_t>();
    std::memcpy(std::addressof(d), std::addressof(bits), sizeof(d));
  }

  void readRawInto(Result& r) {
    static_assert(
        sizeof(Result) == sizeof(mc_res_t),
        "Carbon currently assumes sizeof(Result) == sizeof(int16_t)");
    r = static_cast<Result>(readRaw<int16_t>());
  }

  void readRawInto(std::string& s) {
    s = cursor_.readFixedString(readVarint<uint32_t>());
  }

  void readRawInto(folly::IOBuf& buf) {
    cursor_.clone(buf, readVarint<uint32_t>());
  }

  void readStructBegin() {
    nestedStructFieldIds_.push_back(lastFieldId_);
    lastFieldId_ = 0;
  }

  void readStructEnd() {
    lastFieldId_ = nestedStructFieldIds_.back();
    nestedStructFieldIds_.pop_back();
  }

  std::pair<FieldType, uint32_t> readVectorFieldSizeAndInnerType() {
    std::pair<FieldType, uint32_t> pr;
    const uint8_t byte = readByte();
    pr.first = static_cast<FieldType>(byte & 0x0f);
    if ((byte & 0xf0) == 0xf0) {
      pr.second = readVarint<uint32_t>();
    } else {
      pr.second = static_cast<uint32_t>(byte >> 4);
    }
    return pr;
  }

  std::pair<FieldType, int16_t> readFieldHeader() {
    std::pair<FieldType, int16_t> rv;
    const uint8_t byte = readByte();
    if (byte & 0xf0) {
      rv.first = static_cast<FieldType>(byte & 0x0f);
      rv.second = static_cast<int16_t>(byte >> 4) + lastFieldId_;
    } else {
      rv.first = static_cast<FieldType>(byte);
      if (rv.first != FieldType::Stop) {
        rv.second = cursor_.read<int16_t>();
      }
    }
    lastFieldId_ = rv.second;
    return rv;
  }

  void skip(const FieldType fieldType);

 private:
  uint8_t readByte() {
    return cursor_.template read<uint8_t>();
  }

  template <class T>
  typename std::enable_if<std::numeric_limits<T>::is_integer, T>::type
  readZigzagVarint() {
    static_assert(
        sizeof(T) <= sizeof(uint64_t),
        "argument to readZigzagVarint() can be no larger than uint64_t");
    using UnsignedT = typename std::make_unsigned<T>::type;

    return util::unzigzag(readVarint<UnsignedT>());
  }

  template <class T>
  typename std::enable_if<std::numeric_limits<T>::is_integer, T>::type
  readVarint() {
    using UnsignedT = typename std::make_unsigned<T>::type;
    constexpr uint8_t kShift = 7;
    constexpr uint8_t kMaxIters = (sizeof(T) * 8 + 6) / 7;

    static_assert(
        sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8,
        "readVarint() may only be used with 16-, 32-, or 64-bit integers");

    UnsignedT urv = 0;
    uint8_t iter = 0;
    uint8_t byte;
    do {
      byte = readByte();
      urv |= static_cast<UnsignedT>(byte & 0x7f) << (kShift * iter++);
    } while (byte & 0x80 && iter <= kMaxIters);

    return static_cast<T>(urv);
  }

  CarbonCursor cursor_;
  folly::small_vector<int16_t, detail::kDefaultStackSize> nestedStructFieldIds_;
  int16_t lastFieldId_{0};
  FieldType boolFieldType_;
};

} // carbon
