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

class TestBigvalue(McrouterTestCase):
    config = './mcrouter/test/test_bigvalue.json'
    extra_args = ['--big-value-split-threshold', '5000',
                  '--big-value-batch-size', '2',
                  '-t', '30']

    def setUp(self):
        self.add_server(Memcached())

    def get_mcrouter(self):
        return self.add_mcrouter(
            self.config,
            extra_args=self.extra_args)

    def test_bigvalue(self):
        mcrouter = self.get_mcrouter()
        value = "x" * 20 * 1000 * 1000
        self.assertTrue(mcrouter.set('key', value))
        # Not assertEqual, which would output 20Mb of 'x'
        # if the test fails
        self.assertTrue(mcrouter.get('key') == value)
