/*
 *  Copyright (c) 2015, Facebook, Inc.
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

namespace {
inline int awfulness(mc_res_t result) {
  switch (result) {
    case mc_res_ok:
    case mc_res_stored:
    case mc_res_stalestored:
    case mc_res_exists:
    case mc_res_deleted:
    case mc_res_found:
      return 1;

    case mc_res_waiting:
      return 2;

    case mc_res_notfound:
    case mc_res_notstored:
      return 4;

    case mc_res_ooo:
    case mc_res_timeout:
    case mc_res_connect_timeout:
    case mc_res_connect_error:
    case mc_res_busy:
    case mc_res_shutdown:
    case mc_res_try_again:
    case mc_res_tko:
      return 5;

    case mc_res_bad_key:
    case mc_res_bad_value:
    case mc_res_aborted:
      return 6;

    case mc_res_remote_error:
    case mc_res_unknown:
      return 7;

    case mc_res_local_error:
      return 8;

    case mc_res_client_error:
      return 9;

    default:
      return 10;
  }
}
}

template <typename Operation>
McReply::McReply(DefaultReplyT, Operation)
    : McReply(UpdateLike<Operation>::value ?
                mc_res_notstored :
                mc_res_notfound) {
}

template <typename InputIterator>
InputIterator McReply::reduce(InputIterator begin, InputIterator end) {
  if (begin == end) {
    return end;
  }

  InputIterator most_awful_it = begin;
  int most_awful = awfulness(begin->result_);

  ++begin;
  while (begin != end) {
    if (awfulness(begin->result_) > most_awful) {
      most_awful_it = begin;
      most_awful = awfulness(begin->result_);
    }
    ++begin;
  }

  return most_awful_it;
}

inline bool McReply::isError() const {
  return mc_res_is_err(result_);
}

inline bool McReply::isFailoverError() const {
  switch (result_) {
    case mc_res_busy:
    case mc_res_shutdown:
    case mc_res_tko:
    case mc_res_try_again:
    case mc_res_local_error:
    case mc_res_connect_error:
    case mc_res_connect_timeout:
    case mc_res_timeout:
    case mc_res_remote_error:
      return true;
    default:
      return false;
  }
}

}}  // facebook::memcache
