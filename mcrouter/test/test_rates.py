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

class TestRates(McrouterTestCase):
    config = './mcrouter/test/test_rates.json'
    extra_args = ['--destination-rate-limiting']

    def setUp(self):
        # The order here must corresponds to the order of hosts in the .json
        self.mc = self.add_server(Memcached())

    def get_mcrouter(self):
        return self.add_mcrouter(
            self.config,
            extra_args=self.extra_args)

    def test_basic(self):
        mcrouter = self.get_mcrouter()
        time.sleep(1.1)

        key = "basic"

        # 2 pass, 3rd rate limited
        mcrouter.set(key, "1")
        self.assertEqual(self.mc.get(key), "1")
        mcrouter.set(key, "2")
        self.assertEqual(self.mc.get(key), "2")
        mcrouter.set(key, "3")
        self.assertEqual(self.mc.get(key), "2")

    def test_burst(self):
        mcrouter = self.get_mcrouter()
        time.sleep(1.1)

        key = "burst"

        # even though rate is 4/s, only 3 pass (max burst)
        mcrouter.set(key, "1")
        self.assertEqual(self.mc.get(key), "1")
        mcrouter.set(key, "2")
        self.assertEqual(self.mc.get(key), "2")
        mcrouter.set(key, "3")
        self.assertEqual(self.mc.get(key), "3")
        mcrouter.set(key, "4")
        self.assertEqual(self.mc.get(key), "3")
