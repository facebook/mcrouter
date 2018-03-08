# Copyright (c) 2015, Facebook, Inc.
#
# This source code is licensed under the MIT license found in the LICENSE
# file in the root directory of this source tree.

from __future__ import absolute_import
from __future__ import division
from __future__ import print_function
from __future__ import unicode_literals

from mcrouter.test.McrouterTestCase import McrouterTestCase

class TestMissOnErrorArithOps(McrouterTestCase):
    config = './mcrouter/test/test_miss_on_error_arith_ops.json'
    extra_args = []

    def setUp(self):
        self.mcrouter = self.add_mcrouter(
            self.config,
            extra_args=self.extra_args)

    def test_behaviour(self):
        key = 'foo:test:key:1234'
        self.assertIsNone(self.mcrouter.set(key, 2))
        self.assertIsNone(self.mcrouter.incr(key, 2))
        self.assertIsNone(self.mcrouter.decr(key, 2))
