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

class TestConstShardHash(McrouterTestCase):
    config = './mcrouter/test/test_const_shard_hash.json'
    extra_args = []

    def test_const_shard_hash(self):
        mc1 = self.add_server(Memcached())
        mc2 = self.add_server(Memcached())
        mcrouter = self.add_mcrouter(self.config, extra_args=self.extra_args)

        key = 'foo:0:test'
        value = 'value0'
        mcrouter.set(key, value)
        self.assertEquals(mc1.get(key), value)
        self.assertIsNone(mc2.get(key))
        key = 'foo:1:test'
        value = 'value1'
        mcrouter.set(key, value)
        self.assertIsNone(mc1.get(key))
        self.assertEquals(mc2.get(key), value)
