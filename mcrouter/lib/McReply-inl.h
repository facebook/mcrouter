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

#include "mcrouter/lib/McOperationTraits.h"

namespace facebook { namespace memcache {

template <int op>
McReply::McReply(DefaultReplyT, McOperation<op>) noexcept
    : McReply(UpdateLike<McOperation<op>>::value ?
                mc_res_notstored :
                mc_res_notfound) {
}

template <class Request>
McReply::McReply(DefaultReplyT, const Request&) noexcept
    : McReply(UpdateLike<Request>::value ?
                mc_res_notstored :
                mc_res_notfound) {
}

template <typename InputIterator>
InputIterator McReply::reduce(InputIterator begin, InputIterator end) {
  if (begin == end) {
    return end;
  }

  InputIterator most_awful_it = begin;
  int most_awful = resultSeverity(begin->result_);

  ++begin;
  while (begin != end) {
    if (resultSeverity(begin->result_) > most_awful) {
      most_awful_it = begin;
      most_awful = resultSeverity(begin->result_);
    }
    ++begin;
  }

  return most_awful_it;
}

}}  // facebook::memcache
