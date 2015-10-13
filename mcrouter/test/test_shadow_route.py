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

class TestShadowRoute(McrouterTestCase):
    config = './mcrouter/test/test_shadow_route.json'

    def setUp(self):
        # The order here must corresponds to the order of hosts in the .json
        self.mc_foo_0 = self.add_server(Memcached())
        self.mc_foo_1 = self.add_server(Memcached())
        self.mc_foo_shadow = self.add_server(Memcached())
        self.mc_bar_shadow = self.add_server(Memcached())
        self.mcrouter = self.add_mcrouter(self.config)

    def test_shadow_route(self):
        shadow_list = [5, 7, 13, 33, 43, 46, 58, 71, 83, 85, 89, 91, 93]
        kv = [('f' + str(i), 'value' + str(i)) for i in range(100)]
        shadow_keys = [kv[i][0] for i in shadow_list]

        for key, value in kv:
            self.mcrouter.set(key, value)

        time.sleep(1)

        for key, value in kv:
            self.assertEqual(self.mc_foo_0.get(key), value)
            self.assertEqual(self.mc_foo_1.get(key), value)
            if key in shadow_keys:
                self.assertEqual(self.mc_foo_shadow.get(key), value)
                self.assertEqual(self.mc_bar_shadow.get(key), value)
            else:
                self.assertIsNone(self.mc_foo_shadow.get(key))
                self.assertIsNone(self.mc_bar_shadow.get(key))
