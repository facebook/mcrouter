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

class TestAllowGetsOnly(McrouterTestCase):
    config = './mcrouter/test/mcrouter_test_basic_1_1_1.json'

    def setUp(self):
        self.mc = self.add_server(Memcached())
        extra_args = ['--allow-only-gets']
        self.mcr = self.add_mcrouter(self.config, extra_args=extra_args)

    def test_allow_gets_only(self):
        self.assertTrue(self.mc.set('key', '1'))
        self.assertEqual(self.mc.get('key'), '1')

        self.assertFalse(self.mcr.set('key', '2'))
        self.assertFalse(self.mcr.delete('key'))
        self.assertFalse(self.mcr.incr('key'))
        self.assertFalse(self.mcr.decr('key'))
        self.assertFalse(self.mcr.add('key', '2'))
        self.assertFalse(self.mcr.add('key2', '1'))

        # both get and metaget should work
        self.assertEqual(self.mcr.get('key'), '1')
        self.assertEqual(self.mcr.metaget('key')['exptime'], '0')
        self.assertIsNone(self.mcr.get('key2'))
