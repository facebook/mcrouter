# Copyright (c) 2015, Facebook, Inc.
#
# This source code is licensed under the MIT license found in the LICENSE
# file in the root directory of this source tree.

from __future__ import absolute_import
from __future__ import division
from __future__ import print_function
from __future__ import unicode_literals

from mcrouter.test.MCProcess import Memcached
from mcrouter.test.McrouterTestCase import McrouterTestCase

class TestLargeObj(McrouterTestCase):
    config_list = './mcrouter/test/test_named_handles_list.json'
    config_obj = './mcrouter/test/test_named_handles_obj.json'

    def setUp(self):
        self.mc1 = self.add_server(Memcached())
        self.mc2 = self.add_server(Memcached())

    def test_named_handles_list(self):
        mcrouter = self.add_mcrouter(self.config_list)
        # NullRoute returns NOT_STORED
        self.assertFalse(mcrouter.set('test', 'value'))
        self.assertEquals(self.mc1.get('test'), 'value')
        self.assertEquals(self.mc2.get('test'), 'value')

    def test_named_handles_obj(self):
        mcrouter = self.add_mcrouter(self.config_obj)
        # NullRoute returns NOT_STORED
        self.assertFalse(mcrouter.set('test2', 'value'))
        self.assertEquals(self.mc1.get('test2'), 'value')
        self.assertEquals(self.mc2.get('test2'), 'value')
