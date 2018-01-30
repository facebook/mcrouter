/*
 *  Copyright (c) 2014-present, Facebook, Inc.
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
  EXPECT_EQ("127.0.0.1", ap->getHost());
  EXPECT_EQ(12345, ap->getPort());
  EXPECT_EQ(proto, ap->getProtocol());
  ap = AccessPoint::create("127.0.0.1:1", proto);
  EXPECT_TRUE(ap != nullptr);
  EXPECT_EQ("127.0.0.1", ap->getHost());
  EXPECT_EQ(1, ap->getPort());
  EXPECT_EQ(proto, ap->getProtocol());
  ap = AccessPoint::create("[127.0.0.1]:12345", proto);
  EXPECT_TRUE(ap != nullptr);
  EXPECT_EQ("127.0.0.1", ap->getHost());
  EXPECT_EQ(12345, ap->getPort());
  EXPECT_EQ(proto, ap->getProtocol());
  EXPECT_TRUE(AccessPoint::create("127.0.0.1", proto) == nullptr);
  EXPECT_TRUE(AccessPoint::create("127.0.0.1::", proto) == nullptr);
  ap = AccessPoint::create("[::1]:12345", proto);
  EXPECT_TRUE(ap != nullptr);
  EXPECT_EQ("0000:0000:0000:0000:0000:0000:0000:0001", ap->getHost());
  EXPECT_EQ(12345, ap->getPort());
  EXPECT_EQ(proto, ap->getProtocol());
  EXPECT_TRUE(AccessPoint::create("[::1]", proto) == nullptr);
  ap = AccessPoint::create("unix:/tmp/sock1", proto);
  EXPECT_TRUE(ap != nullptr);
  EXPECT_EQ("/tmp/sock1", ap->getHost());
  EXPECT_EQ(0, ap->getPort());
  EXPECT_EQ(proto, ap->getProtocol());
  EXPECT_TRUE(AccessPoint::create("unix:", proto) == nullptr);
}

TEST(AccessPoint, host_port_proto) {
  auto proto = mc_unknown_protocol;
  auto ap = AccessPoint::create("127.0.0.1:12345:ascii", proto);
  EXPECT_TRUE(ap != nullptr);
  EXPECT_EQ("127.0.0.1", ap->getHost());
  EXPECT_EQ(12345, ap->getPort());
  EXPECT_EQ(mc_ascii_protocol, ap->getProtocol());
  ap = AccessPoint::create("127.0.0.1:1:umbrella", proto);
  EXPECT_TRUE(ap != nullptr);
  EXPECT_EQ("127.0.0.1", ap->getHost());
  EXPECT_EQ(1, ap->getPort());
  EXPECT_EQ(mc_umbrella_protocol_DONOTUSE, ap->getProtocol());
  ap = AccessPoint::create("127.0.0.1:1:caret", proto);
  EXPECT_TRUE(ap != nullptr);
  EXPECT_EQ("127.0.0.1", ap->getHost());
  EXPECT_EQ(1, ap->getPort());
  EXPECT_EQ(mc_caret_protocol, ap->getProtocol());
  ap = AccessPoint::create("[127.0.0.1]:12345:ascii", proto);
  EXPECT_TRUE(ap != nullptr);
  EXPECT_EQ("127.0.0.1", ap->getHost());
  EXPECT_EQ(12345, ap->getPort());
  EXPECT_EQ(mc_ascii_protocol, ap->getProtocol());
  EXPECT_TRUE(AccessPoint::create("[::1]:12345:fhgsdg", proto) == nullptr);
  EXPECT_TRUE(AccessPoint::create("[::1]", proto) == nullptr);
  ap = AccessPoint::create("unix:/tmp/sock2:caret", proto);
  EXPECT_TRUE(ap != nullptr);
  EXPECT_EQ("/tmp/sock2", ap->getHost());
  EXPECT_EQ(0, ap->getPort());
  EXPECT_EQ(mc_caret_protocol, ap->getProtocol());
  EXPECT_TRUE(AccessPoint::create("unix:/tmp/sock3:fhgsdg", proto) == nullptr);
}

TEST(AccessPoint, host_port_proto_ssl) {
  auto proto = mc_unknown_protocol;
  auto ap = AccessPoint::create("127.0.0.1:12345:ascii:ssl", proto);
  EXPECT_TRUE(ap != nullptr);
  EXPECT_EQ("127.0.0.1", ap->getHost());
  EXPECT_EQ(12345, ap->getPort());
  EXPECT_EQ(mc_ascii_protocol, ap->getProtocol());
  EXPECT_TRUE(ap->useSsl());
  ap = AccessPoint::create("127.0.0.1:1:umbrella:plain", proto);
  EXPECT_TRUE(ap != nullptr);
  EXPECT_EQ("127.0.0.1", ap->getHost());
  EXPECT_EQ(1, ap->getPort());
  EXPECT_EQ(mc_umbrella_protocol_DONOTUSE, ap->getProtocol());
  EXPECT_FALSE(ap->useSsl());
  ap = AccessPoint::create("127.0.0.1:1:caret:plain", proto);
  EXPECT_TRUE(ap != nullptr);
  EXPECT_EQ("127.0.0.1", ap->getHost());
  EXPECT_EQ(1, ap->getPort());
  EXPECT_EQ(mc_caret_protocol, ap->getProtocol());
  EXPECT_FALSE(ap->useSsl());
  ap = AccessPoint::create("127.0.0.1:12345:ascii", proto, true);
  EXPECT_TRUE(ap != nullptr);
  EXPECT_EQ("127.0.0.1", ap->getHost());
  EXPECT_EQ(12345, ap->getPort());
  EXPECT_EQ(mc_ascii_protocol, ap->getProtocol());
  EXPECT_TRUE(ap->useSsl());
  ap = AccessPoint::create("[::1]:12345:ascii", proto, false);
  EXPECT_TRUE(ap != nullptr);
  EXPECT_EQ("0000:0000:0000:0000:0000:0000:0000:0001", ap->getHost());
  EXPECT_EQ(12345, ap->getPort());
  EXPECT_EQ(mc_ascii_protocol, ap->getProtocol());
  EXPECT_FALSE(ap->useSsl());
  EXPECT_TRUE(AccessPoint::create("[::1]:12345:ascii:blah", proto) == nullptr);
  EXPECT_TRUE(
      AccessPoint::create("unix:/tmp/sock4:ascii:ssl", proto) == nullptr);
  EXPECT_TRUE(
      AccessPoint::create("unix:/tmp/sock5:ascii:blah", proto) == nullptr);
  EXPECT_TRUE(
      AccessPoint::create("unix:/tmp/sock6:5000:caret", proto) == nullptr);
  EXPECT_TRUE(AccessPoint::create("unix:/tmp/sock7:5000", proto) == nullptr);
  ap = AccessPoint::create("unix:/tmp/sock8:caret:plain", proto);
  EXPECT_TRUE(ap != nullptr);
  EXPECT_EQ("/tmp/sock8", ap->getHost());
  EXPECT_EQ(0, ap->getPort());
  EXPECT_EQ(mc_caret_protocol, ap->getProtocol());
  EXPECT_TRUE(!ap->useSsl());
}

TEST(AccessPoint, port_override) {
  auto proto = mc_unknown_protocol;
  auto ap = AccessPoint::create("127.0.0.1:12345", proto, false, 44);
  EXPECT_TRUE(ap != nullptr);
  EXPECT_EQ("127.0.0.1", ap->getHost());
  EXPECT_EQ(44, ap->getPort());
  EXPECT_EQ(proto, ap->getProtocol());
  EXPECT_FALSE(ap->useSsl());
  ap = AccessPoint::create("127.0.0.1:12345:ascii", proto, true, 11);
  EXPECT_TRUE(ap != nullptr);
  EXPECT_EQ("127.0.0.1", ap->getHost());
  EXPECT_EQ(11, ap->getPort());
  EXPECT_EQ(mc_ascii_protocol, ap->getProtocol());
  EXPECT_TRUE(ap->useSsl());
  ap = AccessPoint::create("127.0.0.1", proto, false, 22);
  EXPECT_TRUE(ap != nullptr);
  EXPECT_EQ("127.0.0.1", ap->getHost());
  EXPECT_EQ(22, ap->getPort());
  EXPECT_EQ(proto, ap->getProtocol());
  EXPECT_FALSE(ap->useSsl());
  ap = AccessPoint::create("[::1]:12345", proto, true, 33);
  EXPECT_TRUE(ap != nullptr);
  EXPECT_EQ("0000:0000:0000:0000:0000:0000:0000:0001", ap->getHost());
  EXPECT_EQ(33, ap->getPort());
  EXPECT_EQ(proto, ap->getProtocol());
  EXPECT_TRUE(ap->useSsl());
  ap = AccessPoint::create("[::1]", proto, true, 55);
  EXPECT_TRUE(ap != nullptr);
  EXPECT_EQ("0000:0000:0000:0000:0000:0000:0000:0001", ap->getHost());
  EXPECT_EQ(55, ap->getPort());
  EXPECT_EQ(proto, ap->getProtocol());
  EXPECT_TRUE(ap->useSsl());
}

} // anonymous
