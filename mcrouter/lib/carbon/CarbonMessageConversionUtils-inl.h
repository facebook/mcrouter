/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <string>

#include "mcrouter/lib/carbon/CommonSerializationTraits.h"
#include "mcrouter/lib/carbon/Fields.h"
#include "mcrouter/lib/carbon/Keys.h"

namespace carbon {

namespace detail {

class ToDynamicVisitor {
 public:
  explicit ToDynamicVisitor(FollyDynamicConversionOptions opts)
      : value_(folly::dynamic::object()), opts_(opts) {}

  template <class T>
  bool enterMixin(uint16_t id, folly::StringPiece name, const T& value) {
    if (!opts_.inlineMixins) {
      value_.insert("__" + name.str(), convertToFollyDynamic(value));
      return false;
    } else {
      return true;
    }
  }

  bool leaveMixin() {
    return true;
  }

  template <class T>
  bool visitField(uint16_t id, folly::StringPiece name, const T& value) {
    auto val = toDynamic(value);
    if (val != nullptr) {
      value_.insert(name, std::move(val));
    }
    return true;
  }

  /**
   * Obtain serialized output.
   */
  folly::dynamic moveOutput() {
    return std::move(value_);
  }

 private:
  folly::dynamic value_;
  FollyDynamicConversionOptions opts_;

  folly::dynamic toDynamic(char c) const {
    return folly::dynamic(std::string(1, c));
  }

  folly::dynamic toDynamic(const folly::IOBuf& value) const {
    if (value.isChained()) {
      auto copy = value;
      return folly::StringPiece(copy.coalesce());
    } else {
      return folly::StringPiece(folly::ByteRange(value.data(), value.length()));
    }
  }

  template <class T>
  folly::dynamic toDynamic(const folly::Optional<T>& value) const {
    if (value.hasValue()) {
      return toDynamic(*value);
    }
    return nullptr;
  }

  template <class T>
  folly::dynamic toDynamic(const Keys<T>& value) const {
    return value.fullKey();
  }

  template <class T>
  typename std::enable_if<std::is_arithmetic<T>::value, folly::dynamic>::type
  toDynamic(const T& value) const {
    return value;
  }

  template <class T>
  typename std::enable_if<!std::is_arithmetic<T>::value, folly::dynamic>::type
  toDynamic(const T& value) const {
    return toDynamic2(value);
  }

  template <class T>
  typename std::enable_if<IsCarbonStruct<T>::value, folly::dynamic>::type
  toDynamic2(const T& value) const {
    return convertToFollyDynamic(value, opts_);
  }

  template <class T>
  typename std::enable_if<!IsCarbonStruct<T>::value, folly::dynamic>::type
  toDynamic2(const T& value) const {
    return toDynamic3(value);
  }

  template <class T>
  typename std::enable_if<std::is_enum<T>::value, folly::dynamic>::type
  toDynamic3(const T& value) const {
    return static_cast<int64_t>(value);
  }

  template <class T>
  typename std::enable_if<!std::is_enum<T>::value, folly::dynamic>::type
  toDynamic3(const T& value) const {
    return toDynamic4(value);
  }

  template <class T>
  typename std::enable_if<
      std::is_convertible<T, folly::StringPiece>::value,
      folly::dynamic>::type
  toDynamic4(const T& value) const {
    return folly::StringPiece(value);
  }

  template <class T>
  typename std::enable_if<
      !std::is_convertible<T, folly::StringPiece>::value,
      folly::dynamic>::type
  toDynamic4(const T& value) const {
    return toDynamic5(value);
  }

  template <class T>
  typename std::enable_if<IsLinearContainer<T>::value, folly::dynamic>::type
  toDynamic5(const T& value) const {
    folly::dynamic array = folly::dynamic::array();
    for (auto it = SerializationTraits<T>::begin(value);
         it != SerializationTraits<T>::end(value);
         ++it) {
      array.push_back(toDynamic(*it));
    }
    return array;
  }

  template <class T>
  typename std::enable_if<!IsLinearContainer<T>::value, folly::dynamic>::type
  toDynamic5(const T& value) const {
    if (!opts_.ignoreUnserializableTypes) {
      return "(not serializable)";
    }
    return nullptr;
  }
};

} // detail

template <class Message>
folly::dynamic convertToFollyDynamic(
    const Message& m,
    FollyDynamicConversionOptions opts) {
  detail::ToDynamicVisitor visitor(opts);
  m.visitFields(visitor);
  return visitor.moveOutput();
}

} // carbon
