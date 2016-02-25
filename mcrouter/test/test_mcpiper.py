# Copyright (c) 2016, Facebook, Inc.
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

from mcrouter.test.MCProcess import BaseDirectory, Memcached, Mcpiper
from mcrouter.test.McrouterTestCase import McrouterTestCase

class TestMcpiper(McrouterTestCase):
    mcrouter_ascii_config = './mcrouter/test/mcrouter_test_basic_1_1_1.json'
    mcrouter_ascii_extra_args = ['--debug-fifo-root',
                                 BaseDirectory('mcrouter_ascii').path]

    mcrouter_umbrella_config = './mcrouter/test/test_umbrella_server.json'
    mcrouter_umbrella_extra_args = ['--debug-fifo-root',
                                    BaseDirectory('mcrouter_umbrella').path]

    def setUp(self):
        self.memcached = self.add_server(Memcached())
        self.mcrouter_ascii = self.add_mcrouter(
            self.mcrouter_ascii_config,
            extra_args=self.mcrouter_ascii_extra_args,
            bg_mcrouter=True)
        self.mcrouter_umbrella = self.add_mcrouter(
            self.mcrouter_umbrella_config,
            extra_args=self.mcrouter_umbrella_extra_args)

    def get_mcpiper(self, mcrouter, args=None):
        mcpiper = Mcpiper(mcrouter.debug_fifo_root, args)

        # Make sure mcrouter creates fifos and start replicating data to them.
        mcrouter.set('abc', '123')
        mcrouter.delete('abc')
        time.sleep(3)

        return mcpiper

    def do_get_test(self, mcrouter):
        # Prepare data
        self.assertTrue(self.memcached.set('key_hit', 'value_hit'))

        mcpiper = self.get_mcpiper(mcrouter)

        self.assertEquals('value_hit', mcrouter.get('key_hit'))
        self.assertFalse(mcrouter.get('key_miss'))

        # wait for data to arrive in mcpiper
        time.sleep(2)

        self.assertTrue(mcpiper.contains('get key_hit'))
        self.assertTrue(mcpiper.contains('mc_res_found'))
        self.assertTrue(mcpiper.contains('value_hit'))
        self.assertTrue(mcpiper.contains('get key_miss'))
        self.assertTrue(mcpiper.contains('mc_res_notfound'))

    def do_set_test(self, mcrouter):
        mcpiper = self.get_mcpiper(mcrouter)

        self.assertTrue(mcrouter.set('key', 'value2'))

        # wait for data to arrive in mcpiper
        time.sleep(2)

        self.assertTrue(mcpiper.contains('set key'))
        self.assertTrue(mcpiper.contains('value2'))

    def do_delete_test(self, mcrouter):
        # Prepare data
        self.assertTrue(self.memcached.set('key_del', 'value_to_del'))

        mcpiper = self.get_mcpiper(mcrouter)

        self.assertTrue(mcrouter.delete('key_del'))
        self.assertFalse(mcrouter.delete('key_not_found'))

        # wait for data to arrive in mcpiper
        time.sleep(2)

        self.assertTrue(mcpiper.contains('delete key_del'))
        self.assertTrue(mcpiper.contains('deleted'))
        self.assertTrue(mcpiper.contains('delete key_not_found'))
        self.assertTrue(mcpiper.contains('mc_res_notfound'))

    def test_get_ascii(self):
        self.do_get_test(self.mcrouter_ascii)

    def test_set_ascii(self):
        self.do_set_test(self.mcrouter_ascii)

    def test_delete_ascii(self):
        self.do_delete_test(self.mcrouter_ascii)

    def test_get_umbrella(self):
        self.do_get_test(self.mcrouter_umbrella)

    def test_set_umbrella(self):
        self.do_set_test(self.mcrouter_umbrella)

    def test_delete_umbrella(self):
        self.do_delete_test(self.mcrouter_umbrella)
