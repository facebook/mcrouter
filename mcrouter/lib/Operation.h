/*
 *  Copyright (c) 2014-present, Facebook, Inc.
 *
 *  This source code is licensed under the MIT license found in the LICENSE
 *  file in the root directory of this source tree.
 *
 */
#pragma once

namespace facebook {
namespace memcache {

template <typename Request>
using ReplyT = typename Request::reply_type;
}
} // facebook::memcache
