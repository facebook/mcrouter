# Copyright (c) 2017-present, Facebook, Inc.
#
# This source code is licensed under the MIT license found in the LICENSE
# file in the root directory of this source tree.

from __future__ import absolute_import
from __future__ import division
from __future__ import print_function
from __future__ import unicode_literals

from mcrouter.test.MCProcess import Memcached
from mcrouter.test.McrouterTestCase import McrouterTestCase


class TestMemcacheRouter(McrouterTestCase):
    config = './mcrouter/test/test_basic_caret.json'
    extra_args = ['--carbon-router-name', 'Memcache']

    def setUp(self):
        self.mc = self.add_server(Memcached())
        self.mcrouter = self.add_mcrouter(self.config,
                                          extra_args=self.extra_args)

    def test_basic(self):
        self.assertTrue(self.mcrouter.set("abc", "def"))
        self.assertEquals(self.mcrouter.get("abc"), "def")
