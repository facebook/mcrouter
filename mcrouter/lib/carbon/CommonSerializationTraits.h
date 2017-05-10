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

#include <map>
#include <set>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

#include <folly/Optional.h>
#include <folly/Traits.h>

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

  static bool emplace(std::vector<T>& vec, T&& t) {
    vec.emplace_back(std::move(t));
    return true;
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

template <class T>
struct SerializationTraits<
    T,
    typename std::enable_if<folly::IsOneOf<
        T,
        std::set<typename T::key_type>,
        std::unordered_set<typename T::key_type>>::value>::type> {
  static constexpr carbon::FieldType kWireType = carbon::FieldType::Set;

  using inner_type = typename T::key_type;

  static size_t size(const T& set) {
    return set.size();
  }

  template <class... Args>
  static bool emplace(T& set, Args&&... args) {
    return set.emplace(std::forward<Args>(args)...).second;
  }

  static void clear(T& set) {
    set.clear();
  }

  static void reserve(std::set<inner_type>& /* set */, size_t /* len */) {}

  static void reserve(std::unordered_set<inner_type>& set, size_t len) {
    set.reserve(len);
  }

  static auto begin(const T& set) -> decltype(set.begin()) {
    return set.begin();
  }

  static auto end(const T& set) -> decltype(set.end()) {
    return set.end();
  }
};

template <class T>
struct SerializationTraits<
    T,
    typename std::enable_if<folly::IsOneOf<
        T,
        std::map<typename T::key_type, typename T::mapped_type>,
        std::unordered_map<typename T::key_type, typename T::mapped_type>>::
                                value>::type> {
  static constexpr carbon::FieldType kWireType = carbon::FieldType::Map;

  using key_type = typename T::key_type;
  using mapped_type = typename T::mapped_type;

  static size_t size(const T& map) {
    return map.size();
  }

  static void clear(T& map) {
    map.clear();
  }

  static void reserve(
      std::map<key_type, mapped_type>& /* map */,
      size_t /* len */) {}

  static void reserve(
      std::unordered_map<key_type, mapped_type>& map,
      size_t len) {
    map.reserve(len);
  }

  template <class... Args>
  static auto emplace(T& map, Args&&... args)
      -> decltype(map.emplace(std::forward<Args>(args)...)) {
    return map.emplace(std::forward<Args>(args)...);
  }

  static auto begin(const T& map) -> decltype(map.begin()) {
    return map.begin();
  }

  static auto end(const T& map) -> decltype(map.end()) {
    return map.end();
  }
};

} // carbon
