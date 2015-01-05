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

from mcrouter.test.MCProcess import Memcached
from mcrouter.test.McrouterTestCase import McrouterTestCase

class TestNoreply(McrouterTestCase):
    config = './mcrouter/test/test_noreply.json'

    def setUp(self):
        # The order here must corresponds to the order of hosts in the .json
        self.mc = self.add_server(Memcached())

    def get_mcrouter(self):
        return self.add_mcrouter(self.config)

    def test_set_noreply(self):
        mcrouter = self.get_mcrouter()
        self.assertTrue(mcrouter.set("key", "value", noreply=True))
        self.assertEqual(self.mc.get("key"), "value")

    def test_add_replace_noreply(self):
        mcrouter = self.get_mcrouter()
        self.assertTrue(mcrouter.add("key", "value", noreply=True))
        self.assertEqual(self.mc.get("key"), "value")
        self.assertTrue(mcrouter.replace("key", "value1", noreply=True))
        self.assertEqual(self.mc.get("key"), "value1")

    def test_delete_noreply(self):
        mcrouter = self.get_mcrouter()
        self.assertTrue(mcrouter.set("key", "value"))
        self.assertEqual(self.mc.get("key"), "value")
        self.assertTrue(mcrouter.delete("key", noreply=True))
        self.assertFalse(self.mc.get("key"))

    def test_arith_noreply(self):
        mcrouter = self.get_mcrouter()
        self.assertTrue(mcrouter.set("arith", "1"))
        self.assertEqual(self.mc.get("arith"), "1")

        self.assertTrue(mcrouter.incr("arith", noreply=True))
        self.assertEqual(self.mc.get("arith"), "2")

        self.assertTrue(mcrouter.decr("arith", noreply=True))
        self.assertEqual(self.mc.get("arith"), "1")
