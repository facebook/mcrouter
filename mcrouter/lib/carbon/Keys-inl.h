/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <iostream>

namespace carbon {

template <class Storage>
void Keys<Storage>::update() {
  const folly::StringPiece key = fullKey();
  keyWithoutRoute_ = key;
  if (!key.empty() && *key.begin() == '/') {
    size_t pos = 1;
    for (int i = 0; i < 2; ++i) {
      pos = key.find('/', pos);
      if (pos == std::string::npos) {
        break;
      }
      ++pos;
    }
    if (pos != std::string::npos) {
      keyWithoutRoute_.advance(pos);
      routingPrefix_.reset(key.begin(), pos);
    }
  }
  routingKey_ = keyWithoutRoute_;
  size_t pos = keyWithoutRoute_.find("|#|");
  if (pos != std::string::npos) {
    routingKey_.reset(keyWithoutRoute_.begin(), pos);
  }
  routingKeyHash_ = 0;
}

} // carbon
