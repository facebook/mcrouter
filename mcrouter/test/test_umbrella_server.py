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

class TestUmbrellaServer(McrouterTestCase):
    config_mc = './mcrouter/test/test_umbrella_server_mc.json'
    config = './mcrouter/test/test_umbrella_server.json'
    extra_args = []

    def setUp(self):
        self.mc = self.add_server(Memcached())
        self.mcrouter_mc = self.add_mcrouter(
            self.config_mc,
            extra_args=self.extra_args,
            bg_mcrouter=True)
        self.mcrouter = self.add_mcrouter(
            self.config,
            extra_args=self.extra_args)

    def test_umbrella_server(self):
        key = 'foo'
        value = 'value'
        self.mcrouter.set(key, value)
        self.assertEqual(self.mcrouter.get(key), value)
        self.assertEqual(self.mc.get(key), value)
