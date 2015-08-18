/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <gtest/gtest.h>

#include "mcrouter/lib/network/AccessPoint.h"

using namespace facebook::memcache;

namespace {

TEST(AccessPoint, host_port) {
  auto proto = mc_unknown_protocol;
  auto ap = AccessPoint::create("127.0.0.1:12345", proto);
  EXPECT_TRUE(ap != nullptr);
  EXPECT_EQ(ap->getHost(), "127.0.0.1");
  EXPECT_EQ(ap->getPort(), 12345);
  EXPECT_EQ(ap->getProtocol(), proto);
  ap = AccessPoint::create("127.0.0.1:1", proto);
  EXPECT_TRUE(ap != nullptr);
  EXPECT_EQ(ap->getHost(), "127.0.0.1");
  EXPECT_EQ(ap->getPort(), 1);
  EXPECT_EQ(ap->getProtocol(), proto);
  ap = AccessPoint::create("[127.0.0.1]:12345", proto);
  EXPECT_TRUE(ap != nullptr);
  EXPECT_EQ(ap->getHost(), "127.0.0.1");
  EXPECT_EQ(ap->getPort(), 12345);
  EXPECT_EQ(ap->getProtocol(), proto);
  EXPECT_TRUE(AccessPoint::create("127.0.0.1", proto) == nullptr);
  EXPECT_TRUE(AccessPoint::create("127.0.0.1::", proto) == nullptr);
  ap = AccessPoint::create("[::1]:12345", proto);
  EXPECT_TRUE(ap != nullptr);
  EXPECT_EQ(ap->getHost(), "0000:0000:0000:0000:0000:0000:0000:0001");
  EXPECT_EQ(ap->getPort(), 12345);
  EXPECT_EQ(ap->getProtocol(), proto);
  EXPECT_TRUE(AccessPoint::create("[::1]", proto) == nullptr);
}

TEST(AccessPoint, host_port_proto) {
  auto proto = mc_unknown_protocol;
  auto ap = AccessPoint::create("127.0.0.1:12345:ascii", proto);
  EXPECT_TRUE(ap != nullptr);
  EXPECT_EQ(ap->getHost(), "127.0.0.1");
  EXPECT_EQ(ap->getPort(), 12345);
  EXPECT_EQ(ap->getProtocol(), mc_ascii_protocol);
  ap = AccessPoint::create("127.0.0.1:1:umbrella", proto);
  EXPECT_TRUE(ap != nullptr);
  EXPECT_EQ(ap->getHost(), "127.0.0.1");
  EXPECT_EQ(ap->getPort(), 1);
  EXPECT_EQ(ap->getProtocol(), mc_umbrella_protocol);
  ap = AccessPoint::create("[127.0.0.1]:12345:binary", proto);
  EXPECT_TRUE(ap != nullptr);
  EXPECT_EQ(ap->getHost(), "127.0.0.1");
  EXPECT_EQ(ap->getPort(), 12345);
  EXPECT_EQ(ap->getProtocol(), mc_binary_protocol);
  ap = AccessPoint::create("[::1]:12345:fhgsdg", proto);
  EXPECT_TRUE(ap != nullptr);
  EXPECT_EQ(ap->getHost(), "0000:0000:0000:0000:0000:0000:0000:0001");
  EXPECT_EQ(ap->getPort(), 12345);
  EXPECT_EQ(ap->getProtocol(), proto);
  EXPECT_TRUE(AccessPoint::create("[::1]", proto) == nullptr);
}

} // namespace
