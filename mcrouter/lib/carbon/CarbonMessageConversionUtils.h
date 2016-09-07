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

#include <folly/dynamic.h>

namespace carbon {

struct FollyDynamicConversionOptions {
  /**
   * If false, each mixin will be put in the object as a member with name
   * "__MixinAliasOrName".
   *
   * If true, members of mixins would be put directly into the object as if
   * they were direct members.
   */
  bool inlineMixins{false};

  /**
   * If false, all fields that are not serializable would contain
   * "(not serializable)" as a value.
   *
   * If true, such fields would be omitted from the output completely.
   */
  bool ignoreUnserializableTypes{false};
};

/**
 * Convenience method for converting Carbon generated messages/structures into
 * folly::dynamic.
 *
 * Note: this method is limited in what it can convert, all unknown types will
 *       be not properly converted and will be replaced with
 *       "(not serializable)".
 */
template <class Message>
folly::dynamic convertToFollyDynamic(
    const Message& m,
    FollyDynamicConversionOptions opts = FollyDynamicConversionOptions());

} // carbon

#include "CarbonMessageConversionUtils-inl.h"
