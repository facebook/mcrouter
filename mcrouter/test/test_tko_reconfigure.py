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

import os
import shutil
import time

from mcrouter.test.McrouterTestCase import McrouterTestCase

class TestTkoReconfigure(McrouterTestCase):
    config1 = './mcrouter/test/test_tko_reconfigure1.json'
    config2 = './mcrouter/test/test_tko_reconfigure2.json'
    extra_args = ['--timeouts-until-tko', '1']

    def tearDown(self):
        McrouterTestCase.tearDown(self)
        os.remove(self.config)

    def setUp(self):
        self.config = os.tmpnam()
        shutil.copyfile(self.config1, self.config)
        self.mcrouter = self.add_mcrouter(
            self.config,
            extra_args=self.extra_args)

    def test_tko_reconfigure(self):
        # server a is in 'fast' state
        self.assertIsNone(self.mcrouter.get('hit'))
        self.assertEqual(self.mcrouter.stats('suspect_servers'), {
            '127.0.0.1:12345': 'status:tko num_failures:2'
        })

        self.mcrouter.change_config(self.config2)
        # wait for mcrouter to reconfigure
        time.sleep(4)
        # no servers should be marked as TKO
        self.assertEqual(self.mcrouter.stats('suspect_servers'), {})
        # one was removed from config
        self.assertTrue(self.mcrouter.check_in_log(
            '127.0.0.1:12345 (A) was TKO, removed from config'))
