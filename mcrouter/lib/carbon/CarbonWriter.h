/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <stdint.h>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>

#include <folly/io/IOBuf.h>
#include <folly/io/IOBufQueue.h>
#include <thrift/lib/cpp2/protocol/CompactProtocol.h>

#include "mcrouter/lib/carbon/CarbonProtocolCommon.h"
#include "mcrouter/lib/carbon/CarbonQueueAppender.h"
#include "mcrouter/lib/carbon/CommonSerializationTraits.h"
#include "mcrouter/lib/carbon/Fields.h"
#include "mcrouter/lib/carbon/Keys.h"
#include "mcrouter/lib/carbon/Result.h"
#include "mcrouter/lib/carbon/Util.h"
#include "mcrouter/lib/fbi/cpp/util.h"

namespace carbon {

class CarbonWriter {
 public:
  CarbonWriter()
      : writer_(apache::thrift::CompactProtocolWriter(
            apache::thrift::SHARE_EXTERNAL_BUFFER)) {}

  template <class T>
  typename std::enable_if<
      folly::IsOneOf<
          T,
          bool,
          char,
          int8_t,
          int16_t,
          int32_t,
          int64_t,
          uint8_t,
          uint16_t,
          uint32_t,
          uint64_t>::value,
      void>::type
  writeField(const int16_t id, const T t) {
    if (!t) {
      return;
    }
    writeFieldAlways(id, t);
  }

  template <class T>
  typename std::enable_if<std::is_enum<T>::value, void>::type writeField(
      const int16_t id,
      const T e) {
    using UnderlyingType = typename std::underlying_type<T>::type;
    writeField(id, static_cast<UnderlyingType>(e));
  }

  template <class T>
  void writeField(const int16_t id, const Keys<T>& key) {
    if (!SerializationTraits<Keys<T>>::isEmpty(key)) {
      writeFieldAlways(id, key);
    }
  }

  template <class T>
  void writeFieldAlways(const int16_t id, const Keys<T>& key) {
    writer_.writeFieldBegin("", apache::thrift::protocol::T_STRING, id);
    SerializationTraits<Keys<T>>::write(key, *this);
  }

  void writeRaw(const bool b) {
    writer_.writeBool(b);
  }

  void writeFieldAlways(const int16_t id, const bool b) {
    writer_.writeFieldBegin("", apache::thrift::protocol::T_BOOL, id);
    writeRaw(b);
  }

  template <class T>
  typename std::
      enable_if<folly::IsOneOf<T, char, int8_t, uint8_t>::value, void>::type
      writeRaw(const T value) {
    writer_.writeByte(value);
  }

  template <class T>
  typename std::
      enable_if<folly::IsOneOf<T, char, int8_t, uint8_t>::value, void>::type
      writeFieldAlways(const int16_t id, const T t) {
    writer_.writeFieldBegin("", apache::thrift::protocol::T_I08, id);
    writeRaw(t);
  }

  template <class T>
  typename std::enable_if<folly::IsOneOf<T, int16_t, uint16_t>::value, void>::
      type
      writeRaw(const T value) {
    writer_.writeI16(value);
  }

  template <class T>
  typename std::enable_if<folly::IsOneOf<T, int16_t, uint16_t>::value, void>::
      type
      writeFieldAlways(const int16_t id, const T t) {
    writer_.writeFieldBegin("", apache::thrift::protocol::T_I16, id);
    writeRaw(t);
  }

  template <class T>
  typename std::enable_if<folly::IsOneOf<T, int32_t, uint32_t>::value, void>::
      type
      writeRaw(const T value) {
    writer_.writeI32(value);
  }

  template <class T>
  typename std::enable_if<folly::IsOneOf<T, int32_t, uint32_t>::value, void>::
      type
      writeFieldAlways(const int16_t id, const T t) {
    writer_.writeFieldBegin("", apache::thrift::protocol::T_I32, id);
    writeRaw(t);
  }

  template <class T>
  typename std::enable_if<folly::IsOneOf<T, int64_t, uint64_t>::value, void>::
      type
      writeRaw(const T value) {
    writer_.writeI64(value);
  }

  template <class T>
  typename std::enable_if<folly::IsOneOf<T, int64_t, uint64_t>::value, void>::
      type
      writeFieldAlways(const int16_t id, const T t) {
    writer_.writeFieldBegin("", apache::thrift::protocol::T_I64, id);
    writeRaw(t);
  }

  template <class T>
  typename std::
      enable_if<folly::IsOneOf<T, folly::IOBuf, std::string>::value, void>::type
      writeField(const int16_t id, const T& data) {
    if (!apache::thrift::StringTraits<T>::isEmpty(data)) {
      writeFieldAlways(id, data);
    }
  }

  template <class T>
  typename std::
      enable_if<folly::IsOneOf<T, folly::IOBuf, std::string>::value, void>::type
      writeFieldAlways(const int16_t id, const T& data) {
    writer_.writeFieldBegin("", apache::thrift::protocol::T_STRING, id);
    writeRaw(data);
  }

  void writeRaw(const std::string& data) {
    writer_.writeString(data);
  }

  void writeRaw(const folly::IOBuf& data) {
    writer_.writeBinary(data);
  }

  void writeField(const int16_t id, const Result res) {
    static_assert(
        sizeof(Result) == sizeof(carbon::Result),
        "Carbon currently assumes sizeof(Result) == sizeof(carbon::Result)");
    // Note that this actually narrows carbon::Result from int to int16_t
    writeField(id, static_cast<int16_t>(res));
  }

  template <class T>
  void writeField(const int16_t id, const std::vector<T>& vec) {
    if (!vec.empty()) {
      writeFieldAlways(id, vec);
    }
  }

  template <class T>
  void writeFieldAlways(const int16_t id, const std::vector<T>& vec) {
    writer_.writeFieldBegin("", apache::thrift::protocol::T_LIST, id);
    writeRaw(vec);
  }

  template <class T>
  typename std::enable_if<std::is_enum<T>::value, void>::type writeRaw(
      const T e) {
    using UnderlyingType = typename std::underlying_type<T>::type;
    writeRaw(static_cast<UnderlyingType>(e));
  }

  template <class T>
  void writeRaw(const std::vector<T>& vec) {
    auto index = detail::TypeToField<T>::fieldType;
    writer_.writeListBegin(
        detail::CarbonToThriftFields[static_cast<uint8_t>(index)], vec.size());
    for (auto const& elem : vec) {
      writeRaw(elem);
    }
  }

  template <class T>
  typename std::enable_if<
      (detail::IsSet<T>::value || detail::IsKVContainer<T>::value ||
       detail::IsUserReadWriteDefined<T>::value || IsCarbonStruct<T>::value ||
       folly::IsOneOf<T, double, float>::value) &&
          (!folly::IsOneOf<T, folly::IOBuf, std::string>::value),
      void>::type
  writeField(const int16_t /* id */, const T& /* t */) {
    facebook::memcache::checkLogic(
        false, "Unsupported type with CarbonWriter!");
  }

  template <class T>
  typename std::enable_if<
      (detail::IsSet<T>::value || detail::IsKVContainer<T>::value ||
       detail::IsUserReadWriteDefined<T>::value || IsCarbonStruct<T>::value ||
       folly::IsOneOf<T, double, float>::value) &&
          (!folly::IsOneOf<T, folly::IOBuf, std::string>::value),
      void>::type
  writeFieldAlways(const int16_t /* id */, const T& /* t */) {
    facebook::memcache::checkLogic(
        false, "Unsupported type with CarbonWriter!");
  }

  template <class T>
  typename std::enable_if<
      detail::IsSet<T>::value || detail::IsKVContainer<T>::value ||
          detail::IsUserReadWriteDefined<T>::value ||
          IsCarbonStruct<T>::value || folly::IsOneOf<T, double, float>::value,
      void>::type
  writeRaw(const T& /* data */) {
    facebook::memcache::checkLogic(
        false, "Unsupported type with CarbonWriter!");
  }

  template <class T>
  void writeField(const int16_t /* id */, folly::Optional<T>& /* t */) {
    facebook::memcache::checkLogic(
        false, "Unsupported type with CarbonWriter!");
  }

  template <class T>
  void writeFieldAlways(const int16_t /* id */, folly::Optional<T>& /* t */) {
    facebook::memcache::checkLogic(
        false, "Unsupported type with CarbonWriter!");
  }

  template <class T>
  void writeRaw(const folly::Optional<T>& /* data */) {
    facebook::memcache::checkLogic(
        false, "Unsupported type with CarbonWriter!");
  }

  void writeStructBegin() {
    writer_.writeStructBegin("");
  }

  void writeStructEnd() {
    writer_.writeStructEnd();
  }

  void writeFieldStop() {
    writer_.writeFieldStop();
  }

  void setOutput(folly::IOBufQueue* storage) {
    writer_.setOutput(storage);
  }

 private:
  apache::thrift::CompactProtocolWriter writer_;
};

} // namespace carbon
