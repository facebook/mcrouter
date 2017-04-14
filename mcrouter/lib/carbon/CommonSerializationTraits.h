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

#include <folly/Optional.h>
#include <vector>

#include "mcrouter/lib/carbon/CarbonProtocolReader.h"
#include "mcrouter/lib/carbon/CarbonProtocolWriter.h"
#include "mcrouter/lib/carbon/Fields.h"
#include "mcrouter/lib/carbon/Keys.h"
#include "mcrouter/lib/carbon/SerializationTraits.h"

namespace carbon {

// SerializationTraits specialization for Keys<>
template <class Storage>
struct SerializationTraits<Keys<Storage>> {
  static constexpr carbon::FieldType kWireType = carbon::FieldType::Binary;

  static Keys<Storage> read(carbon::CarbonProtocolReader& reader) {
    return Keys<Storage>(reader.readRaw<Storage>());
  }

  static void write(
      const Keys<Storage>& key,
      carbon::CarbonProtocolWriter& writer) {
    writer.writeRaw(key.raw());
  }

  static bool isEmpty(const Keys<Storage>& key) {
    return key.empty();
  }
};

// SerializationTraits specialization for folly::Optional<>
template <class T>
struct SerializationTraits<folly::Optional<T>> {
  static constexpr carbon::FieldType kWireType =
      detail::TypeToField<T>::fieldType;

  static folly::Optional<T> read(carbon::CarbonProtocolReader& reader) {
    return folly::Optional<T>(reader.readRaw<T>());
  }

  static void write(
      const folly::Optional<T>& opt,
      carbon::CarbonProtocolWriter& writer) {
    writer.writeRaw(*opt);
  }

  static bool isEmpty(const folly::Optional<T>& opt) {
    return !opt.hasValue();
  }
};

template <class T>
struct SerializationTraits<std::vector<T>> {
  static constexpr carbon::FieldType kWireType = carbon::FieldType::List;

  using inner_type = T;

  static size_t size(const std::vector<T>& vec) {
    return vec.size();
  }

  static void emplace(std::vector<T>& vec, T&& t) {
    vec.emplace_back(std::move(t));
  }

  static void clear(std::vector<T>& vec) {
    vec.clear();
  }

  static void reserve(std::vector<T>& vec, size_t len) {
    vec.reserve(len);
  }

  static auto begin(const std::vector<T>& vec) -> decltype(vec.begin()) {
    return vec.begin();
  }

  static auto end(const std::vector<T>& vec) -> decltype(vec.end()) {
    return vec.end();
  }
};

} // carbon
