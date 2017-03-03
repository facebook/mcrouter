# Copyright (c) 2017-present, Facebook, Inc.
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
