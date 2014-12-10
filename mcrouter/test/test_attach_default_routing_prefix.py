# Copyright (c) 2014, Facebook, Inc.
#  All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

from __future__ import absolute_import
from __future__ import division
from __future__ import print_function
from __future__ import unicode_literals

import time

from mcrouter.test.MCProcess import Memcached
from mcrouter.test.McrouterTestCase import McrouterTestCase

class TestAttachDefaultRoutingPrefixSimple(McrouterTestCase):
    config = './mcrouter/test/test_attach_default_routing_prefix_simple.json'
    extra_args = []

    def setUp(self):
        # The order here must corresponds to the order of hosts in the .json
        self.mc = self.add_server(Memcached())
        self.mcr = self.add_mcrouter(
            self.config, '/a/a/', extra_args=self.extra_args)

    def test_attach_default_routing_prefix_simple(self):
        self.mcr.set("key", "value")
        self.assertIsNone(self.mc.get("key"))
        self.assertEquals(self.mc.get("/a/a/key"), "value")

class TestAttachDefaultRoutingPrefix(McrouterTestCase):
    config = './mcrouter/test/test_attach_default_routing_prefix.json'
    extra_args = []

    def setUp(self):
        # The order here must corresponds to the order of hosts in the .json
        self.mc = []
        for i in range(0, 3):
            self.mc.append(self.add_server(Memcached()))
        self.mcr = self.add_mcrouter(
            self.config, '/a/a/', extra_args=self.extra_args)

    def test_attach_default_routing_prefix(self):
        self.mcr.set("key", "value")
        self.assertIsNone(self.mc[0].get("key"))
        self.assertEquals(self.mc[0].get("/a/a/key"), "value")

        self.mcr.set("/a/a/key", "value2")
        self.assertIsNone(self.mc[0].get("key"))
        self.assertEquals(self.mc[0].get("/a/a/key"), "value2")

        self.mcr.set("/*/*/key", "value3")
        # /*/*/ is asynchronous
        time.sleep(1)

        self.assertIsNone(self.mc[0].get("key"))
        self.assertEquals(self.mc[0].get("/*/*/key"), "value3")

        self.assertIsNone(self.mc[1].get("key"))
        self.assertEquals(self.mc[1].get("/*/*/key"), "value3")

        self.assertIsNone(self.mc[2].get("/*/*/key"))
        self.assertEquals(self.mc[2].get("key"), "value3")
