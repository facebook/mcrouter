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

#include <string>
#include <vector>

#include "mcrouter/lib/carbon/SerializationTraits.h"

namespace folly {
class IOBuf;
} // folly

namespace carbon {

enum class FieldType : uint8_t {
  Stop = 0x0,
  True = 0x1,
  False = 0x2,
  Int8 = 0x3,
  Int16 = 0x4,
  Int32 = 0x5,
  Int64 = 0x6,
  Double = 0x7,
  Binary = 0x8,
  List = 0x9,
  // TODO
  // Set    = 0xa,
  // Map    = 0xb,
  Struct = 0xc,
  Float = 0xd
};

template <class T>
class IsCarbonStruct {
  template <class C>
  static constexpr decltype(&C::serialize, std::true_type()) check(int);

  template <class C>
  static constexpr std::false_type check(...);

 public:
  static constexpr bool value{decltype(check<T>(0))::value};
};

namespace detail {

template <class T>
class SerializationTraitsDefined {
  template <class C>
  static constexpr decltype(
      SerializationTraits<C>::read,
      SerializationTraits<C>::write,
      std::true_type())
  check(int);

  template <class C>
  static constexpr std::false_type check(...);

 public:
  static constexpr bool value{decltype(check<T>(0))::value};
};

template <class T, class Enable = void>
struct TypeToField {};

template <>
struct TypeToField<bool> {
  static constexpr FieldType fieldType{FieldType::True};
};

template <>
struct TypeToField<char> {
  static constexpr FieldType fieldType{FieldType::Int8};
};

template <>
struct TypeToField<int8_t> {
  static constexpr FieldType fieldType{FieldType::Int8};
};

template <>
struct TypeToField<int16_t> {
  static constexpr FieldType fieldType{FieldType::Int16};
};

template <>
struct TypeToField<int32_t> {
  static constexpr FieldType fieldType{FieldType::Int32};
};

template <>
struct TypeToField<int64_t> {
  static constexpr FieldType fieldType{FieldType::Int64};
};

template <>
struct TypeToField<uint8_t> {
  static constexpr FieldType fieldType{FieldType::Int8};
};

template <>
struct TypeToField<uint16_t> {
  static constexpr FieldType fieldType{FieldType::Int16};
};

template <>
struct TypeToField<uint32_t> {
  static constexpr FieldType fieldType{FieldType::Int32};
};

template <>
struct TypeToField<uint64_t> {
  static constexpr FieldType fieldType{FieldType::Int64};
};

template <>
struct TypeToField<float> {
  static constexpr FieldType fieldType{FieldType::Float};
};

template <>
struct TypeToField<double> {
  static constexpr FieldType fieldType{FieldType::Double};
};

template <>
struct TypeToField<std::string> {
  static constexpr FieldType fieldType{FieldType::Binary};
};

template <>
struct TypeToField<folly::IOBuf> {
  static constexpr FieldType fieldType{FieldType::Binary};
};

template <class T>
struct TypeToField<T, typename std::enable_if<std::is_enum<T>::value>::type> {
  static constexpr FieldType fieldType{
      TypeToField<typename std::underlying_type<T>::type>::fieldType};
};

template <class T>
struct TypeToField<T, typename std::enable_if<IsCarbonStruct<T>::value>::type> {
  static constexpr FieldType fieldType{FieldType::Struct};
};

template <class T>
struct TypeToField<
    T,
    typename std::enable_if<SerializationTraitsDefined<T>::value>::type> {
  static constexpr FieldType fieldType{FieldType::Binary};
};

template <class T>
struct TypeToField<std::vector<T>> {
  static constexpr FieldType fieldType{FieldType::List};
};

} // detail
} // carbon
