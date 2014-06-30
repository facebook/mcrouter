/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include <gtest/gtest.h>

#include "mcrouter/AccessPoint.h"

using namespace facebook::memcache::mcrouter;

using std::string;

namespace {

TEST(AccessPoint, host_port) {
  AccessPoint ap;
  auto proto = mc_unknown_protocol;
  auto transp = mc_unknown_transport;
  EXPECT_TRUE(AccessPoint::create("127.0.0.1:12345", proto, transp, ap));
  EXPECT_EQ(ap.getHost(), "127.0.0.1");
  EXPECT_EQ(ap.getPort(), "12345");
  EXPECT_EQ(ap.getProtocol(), proto);
  EXPECT_EQ(ap.getTransport(), transp);
  EXPECT_TRUE(AccessPoint::create("127.0.0.1:1", proto, transp, ap));
  EXPECT_EQ(ap.getHost(), "127.0.0.1");
  EXPECT_EQ(ap.getPort(), "1");
  EXPECT_EQ(ap.getProtocol(), proto);
  EXPECT_EQ(ap.getTransport(), transp);
  EXPECT_TRUE(AccessPoint::create("[127.0.0.1]:12345", proto, transp, ap));
  EXPECT_EQ(ap.getHost(), "127.0.0.1");
  EXPECT_EQ(ap.getPort(), "12345");
  EXPECT_EQ(ap.getProtocol(), proto);
  EXPECT_EQ(ap.getTransport(), transp);
  EXPECT_FALSE(AccessPoint::create("127.0.0.1", proto, transp, ap));
  EXPECT_FALSE(AccessPoint::create("127.0.0.1::", proto, transp, ap));
  EXPECT_TRUE(AccessPoint::create("[::1]:12345", proto, transp, ap));
  EXPECT_EQ(ap.getHost(), "::1");
  EXPECT_EQ(ap.getPort(), "12345");
  EXPECT_EQ(ap.getProtocol(), proto);
  EXPECT_EQ(ap.getTransport(), transp);
  EXPECT_FALSE(AccessPoint::create("[::1]", proto, transp, ap));
}

TEST(AccessPoint, host_port_proto) {
  AccessPoint ap;
  auto proto = mc_unknown_protocol;
  auto transp = mc_unknown_transport;
  EXPECT_TRUE(AccessPoint::create("127.0.0.1:12345:ascii", proto, transp, ap));
  EXPECT_EQ(ap.getHost(), "127.0.0.1");
  EXPECT_EQ(ap.getPort(), "12345");
  EXPECT_EQ(ap.getProtocol(), mc_ascii_protocol);
  EXPECT_EQ(ap.getTransport(), transp);
  EXPECT_TRUE(AccessPoint::create("127.0.0.1:1:umbrella", proto, transp, ap));
  EXPECT_EQ(ap.getHost(), "127.0.0.1");
  EXPECT_EQ(ap.getPort(), "1");
  EXPECT_EQ(ap.getProtocol(), mc_umbrella_protocol);
  EXPECT_EQ(ap.getTransport(), transp);
  EXPECT_TRUE(AccessPoint::create("[127.0.0.1]:12345:binary",
              proto, transp, ap));
  EXPECT_EQ(ap.getHost(), "127.0.0.1");
  EXPECT_EQ(ap.getPort(), "12345");
  EXPECT_EQ(ap.getProtocol(), mc_binary_protocol);
  EXPECT_EQ(ap.getTransport(), transp);
  EXPECT_TRUE(AccessPoint::create("[::1]:12345:fhgsdg", proto, transp, ap));
  EXPECT_EQ(ap.getHost(), "::1");
  EXPECT_EQ(ap.getPort(), "12345");
  EXPECT_EQ(ap.getProtocol(), proto);
  EXPECT_EQ(ap.getTransport(), transp);
  EXPECT_FALSE(AccessPoint::create("[::1]", proto, transp, ap));
}

} // namespace
