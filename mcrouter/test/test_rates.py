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

class TestRates(McrouterTestCase):
    config = './mcrouter/test/test_rates.json'
    extra_args = ['--destination-rate-limiting']

    def setUp(self):
        self.mc = self.add_server(Memcached())

    def get_mcrouter(self):
        return self.add_mcrouter(
            self.config,
            extra_args=self.extra_args)

    def basic_impl(self, key):
        mcrouter = self.get_mcrouter()
        time.sleep(2)

        # 10 pass for sure
        for i in range(10):
            value = str(i)
            mcrouter.set(key, value)
            self.assertEqual(self.mc.get(key), value)
        # next 20 ideally should all fail (due to difference in time
        # measurement in Python and mcrouter we make it less restrictive)
        failed = 0
        for i in range(10, 30):
            value = str(i)
            mcrouter.set(key, value)
            if self.mc.get(key) != value:
                failed = failed + 1
        self.assertGreater(failed, 10)

    def test_basic(self):
        self.basic_impl("basic")

    def test_basic_explicit(self):
        self.basic_impl("explicit")

    def test_burst(self):
        mcrouter = self.get_mcrouter()
        time.sleep(2)

        key = "burst"

        # even though rate is 50/s, around 10 pass (max burst)

        # 10 pass for sure
        for i in range(10):
            value = str(i)
            mcrouter.set(key, value)
            self.assertEqual(self.mc.get(key), value)
        failed = 0
        for i in range(10, 50):
            value = str(i)
            mcrouter.set(key, value)
            if self.mc.get(key) != value:
                failed = failed + 1
        self.assertGreater(failed, 25)
