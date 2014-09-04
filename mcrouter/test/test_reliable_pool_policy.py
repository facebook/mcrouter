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

class TestReliablePoolPolicy(McrouterTestCase):
    config = './mcrouter/test/test_reliable_pool_policy.json'
    extra_args = []

    def setUp(self):
        self.mc1 = self.add_server(Memcached())
        self.mc2 = self.add_server(Memcached())
        self.mcrouter = self.add_mcrouter(
            self.config,
            extra_args=self.extra_args)

    def test_reliable_pool_no_failover(self):
        key = 'footemp'
        value = 'value'
        self.mcrouter.set(key, value)
        self.assertEqual(self.mc1.get(key), value)
        self.assertIsNone(self.mc2.get(key))

    def test_reliable_pool_failover(self):
        self.mc1.terminate()
        key = 'footemp'
        value = 'value'
        self.mcrouter.set(key, value)
        self.assertEqual(self.mc2.get(key), value)
