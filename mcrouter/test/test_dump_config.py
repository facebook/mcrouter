# Copyright (c) 2017, Facebook, Inc.
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

import os


class TestDumpConfig(McrouterTestCase):
    config = './mcrouter/test/mcrouter_test_basic_1_1_1.json'
    extra_args = ['--proxy-threads=1']

    def setUp(self):
        self.add_server(Memcached())
        self.mcrouter = self.add_mcrouter(self.config,
                                          extra_args=self.extra_args)

    def test_save_config(self):
        # first send a command to make sure mcrouter is working.
        self.assertTrue(self.mcrouter.set('abc', '123'))

        saved_config_root = '{}/mcrouter/0'.format(
            self.mcrouter.config_dump_root)

        # check if dir exists
        self.assertTrue(os.path.isdir(saved_config_root))
        self.assertTrue(os.path.exists(saved_config_root))

        # check config files
        saved_config_files = os.listdir(saved_config_root)
        self.assertEqual(1, len(saved_config_files))

        saved_config_file = '{}/{}'.format(
            saved_config_root, saved_config_files[0])
        with open(self.mcrouter.config) as original_file:
            with open(saved_config_file) as saved_file:
                self.assertEqual(original_file.read(), saved_file.read())
