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

#include "mcrouter/lib/carbon/CarbonProtocolReader.h"
#include "mcrouter/lib/carbon/CarbonProtocolWriter.h"
#include "mcrouter/lib/carbon/Keys.h"
#include "mcrouter/lib/carbon/SerializationTraits.h"

namespace carbon {

// SerializationTraits specialization for Keys<>
template <class Storage>
struct SerializationTraits<Keys<Storage>> {
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

} // carbon
