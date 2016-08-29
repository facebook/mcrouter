/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <limits>
#include <stdint.h>
#include <type_traits>
#include <utility>
#include <vector>

#include <folly/io/Cursor.h>
#include <folly/small_vector.h>

#include "mcrouter/lib/carbon/CarbonProtocolCommon.h"
#include "mcrouter/lib/carbon/Fields.h"
#include "mcrouter/lib/carbon/Result.h"
#include "mcrouter/lib/carbon/SerializationTraits.h"
#include "mcrouter/lib/carbon/Util.h"
#include "mcrouter/lib/fbi/cpp/util.h"

namespace carbon {

using CarbonCursor = folly::io::Cursor;

class CarbonProtocolReader {
 private:
  template <class T>
  T readRaw();

  template <class>
  struct Enable {
    using type = int;
  };

 public:
  explicit CarbonProtocolReader(CarbonCursor& cursor) : cursor_(cursor) {}

  // The API of readBoolField() is different than other read*Field() member
  // functions in order to avoid keeping state in the Reader, which would entail
  // maintaining some extra rarely executed branches.
  bool readBoolField(const FieldType fieldType) {
    DCHECK(fieldType == FieldType::True || fieldType == FieldType::False);
    return fieldType == FieldType::True ? true : false;
  }

  bool readBoolField() {
    const auto fieldType = static_cast<FieldType>(readByte());
    DCHECK(fieldType == FieldType::True || fieldType == FieldType::False);
    return fieldType == FieldType::True ? true : false;
  }

  char readCharField() {
    return readByte();
  }

  int8_t readInt8Field() {
    return readByte();
  }

  int16_t readInt16Field() {
    return readZigzagVarint<int16_t>();
  }

  int32_t readInt32Field() {
    return readZigzagVarint<int32_t>();
  }

  int64_t readInt64Field() {
    return readZigzagVarint<int64_t>();
  }

  uint8_t readUInt8Field() {
    return readByte();
  }

  uint16_t readUInt16Field() {
    return readZigzagVarint<uint16_t>();
  }

  uint32_t readUInt32Field() {
    return readZigzagVarint<uint32_t>();
  }

  uint64_t readUInt64Field() {
    return readZigzagVarint<uint64_t>();
  }

  float readFloatField() {
    static_assert(
        sizeof(float) == sizeof(uint32_t),
        "Carbon doubles can only be used on platforms where sizeof(float)"
        " == sizeof(uint32_t)");
    static_assert(
        std::numeric_limits<float>::is_iec559,
        "Carbon floats may only be used on platforms using IEC 559 floats");

    const auto bits = cursor_.template readBE<uint32_t>();
    float rv;
    std::memcpy(std::addressof(rv), std::addressof(bits), sizeof(rv));
    return rv;
  }

  double readDoubleField() {
    static_assert(
        sizeof(double) == sizeof(uint64_t),
        "Carbon doubles can only be used on platforms where sizeof(double)"
        " == sizeof(uint64_t)");
    static_assert(
        std::numeric_limits<double>::is_iec559,
        "Carbon doubles may only be used on platforms using IEC 559 doubles");

    const auto bits = cursor_.template readBE<uint64_t>();
    double rv;
    std::memcpy(std::addressof(rv), std::addressof(bits), sizeof(rv));
    return rv;
  }

  Result readResultField() {
    static_assert(
        sizeof(Result) == sizeof(mc_res_t),
        "Carbon currently assumes sizeof(Result) == sizeof(int16_t)");
    return static_cast<Result>(readInt16Field());
  }

  template <class T>
  T readBinaryField();

  // Deserialize user-provided types that have suitable specializations of
  // carbon::SerializationTraits<>.
  template <class T>
  T readUserTypeField() {
    return SerializationTraits<T>::read(*this);
  }

  void readStructBegin() {
    nestedStructFieldIds_.push_back(lastFieldId_);
    lastFieldId_ = 0;
  }

  void readStructEnd() {
    lastFieldId_ = nestedStructFieldIds_.back();
    nestedStructFieldIds_.pop_back();
  }

  template <class T, typename Enable<decltype(&T::deserialize)>::type = 0>
  std::vector<T> readVectorField() {
    const auto pr = readVectorFieldSizeAndInnerType();
    const auto len = pr.second;
    // TODO Validate type?
    std::vector<T> v(len);
    for (auto& e : v) {
      e.deserialize(*this);
    }
    return v;
  }

  // Hack to enable only for basic types
  template <class T, FieldType F = detail::TypeToField<T>::fieldType>
  std::vector<T> readVectorField() {
    const auto pr = readVectorFieldSizeAndInnerType();
    const auto len = pr.second;
    // TODO Validate type?
    std::vector<T> v(len);
    for (auto& e : v) {
      e = readRaw<T>();
    }
    return v;
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
      rv.second = (byte >> 4) + lastFieldId_;
      lastFieldId_ = rv.second;
    } else {
      rv.first = static_cast<FieldType>(byte);
      if (rv.first != FieldType::Stop) {
        rv.second = cursor_.read<uint16_t>();
      }
    }
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

    facebook::memcache::checkRuntime(
        !(byte & 0x80), "readVarint got invalid varint");

    return static_cast<T>(urv);
  }

  CarbonCursor& cursor_;
  folly::small_vector<int16_t, detail::kDefaultStackSize> nestedStructFieldIds_;
  int16_t lastFieldId_{0};
  FieldType boolFieldType_;
};

} // carbon

// TODO Move more to inl
#include "CarbonProtocolReader-inl.h"
