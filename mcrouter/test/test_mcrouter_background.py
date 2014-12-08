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

from mcrouter.test.MCProcess import Memcached
from mcrouter.test.McrouterTestCase import McrouterTestCase

class TestMcrouterBackground(McrouterTestCase):
    config = './mcrouter/test/mcrouter_test_basic_1_1_1.json'
    extra_args = []

    def setUp(self):
        # The order here corresponds to the order of hosts in the .json
        self.mc = self.add_server(Memcached())
        self.mcrouter = self.get_mcrouter(['-b'])

    def get_mcrouter(self, additional_args=[]):
        return self.add_mcrouter(
            self.config, extra_args=self.extra_args + additional_args)

    def test_version(self):
        v = self.mcrouter.version()
        self.assertTrue(v.startswith('VERSION mcrouter'))

    def test_basic_cmds(self):
        kv = {"hello": "world", "mcrouter": "memcache"}
        for k in kv:
            self.assertTrue(self.mcrouter.set(k, kv[k]))
            self.assertEqual(kv[k], self.mcrouter.get(k))
            self.assertTrue(self.mcrouter.delete(k))
            self.assertFalse(self.mcrouter.get(k))
