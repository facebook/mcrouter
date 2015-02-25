# Copyright (c) 2015, Facebook, Inc.
# All rights reserved.
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

class TestModifyKey(McrouterTestCase):
    config = './mcrouter/test/test_modify_key.json'
    extra_args = []

    def setUp(self):
        self.mc = self.add_server(Memcached())
        self.mcr = self.add_mcrouter(
            self.config, '/a/a/', extra_args=self.extra_args)

    def test_modify_key(self):
        self.assertTrue(self.mcr.set("key", "value"))
        self.assertIsNone(self.mc.get("key"))
        self.assertEqual(self.mc.get("/a/b/foo:key"), "value")

        self.assertTrue(self.mcr.set("foo:bar", "value2"))
        self.assertEqual(self.mc.get("/a/b/foo:bar"), "value2")

        self.assertTrue(self.mcr.set("/*/*/foo:bar", "value3"))
        time.sleep(1)
        self.assertEqual(self.mc.get("/a/b/foo:bar"), "value3")
        self.assertEqual(self.mc.get("/*/*/foo:bar"), "value3")
        self.assertEqual(self.mc.get("foo:bar"), "value3")

        self.assertTrue(self.mcr.set("/a/a/foo:bar", "value4"))
        self.assertEqual(self.mc.get("/a/b/foo:bar"), "value4")

        self.assertTrue(self.mcr.set("/a/a/o:", "value5"))
        self.assertEqual(self.mc.get("/a/b/foo:o:"), "value5")

        self.assertTrue(self.mcr.set("/b/c/key", "value6"))
        self.assertEqual(self.mc.get("/b/c/foo:key"), "value6")

        self.assertTrue(self.mcr.set("/c/d/123", "value7"))
        self.assertEqual(self.mc.get("123"), "value7")

        self.assertFalse(self.mcr.set("/c/d/", "value"))
        self.assertFalse(self.mcr.set("/d/e/", "value"))

        self.assertTrue(self.mcr.set("/d/e/123", "value8"))
        self.assertEqual(self.mc.get("123"), "value8")
