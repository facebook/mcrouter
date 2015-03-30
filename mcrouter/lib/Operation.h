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

#include <type_traits>

namespace facebook { namespace memcache {

/**
 * Operation + Request type uniquely define the Reply type.
 *
 * We leave the door open for multiple Request types for the same operation,
 * but given any two of (Operation, Request, Reply) we should know
 * the third one.
 */
template <typename Operation, typename Request>
struct ReplyType;

template <typename Operation, typename Request>
using ReplyT = typename ReplyType<Operation, Request>::type;
}}
