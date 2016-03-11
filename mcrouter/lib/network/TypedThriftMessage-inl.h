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

namespace facebook { namespace memcache {

template <class M>
template <class InputIterator>
InputIterator TypedThriftReply<M>::reduce(InputIterator begin,
                                          InputIterator end) {
  if (begin == end) {
    return end;
  }

  InputIterator most_awful_it = begin;
  int most_awful = resultSeverity(begin->result());

  ++begin;
  while (begin != end) {
    if (resultSeverity(begin->result()) > most_awful) {
      most_awful_it = begin;
      most_awful = resultSeverity(begin->result());
    }
    ++begin;
  }

  return most_awful_it;
}

}} // facebook::memcache
