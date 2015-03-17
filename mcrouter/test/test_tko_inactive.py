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

from mcrouter.test.mock_servers import SleepServer
from mcrouter.test.McrouterTestCase import McrouterTestCase

class TestTkoInactive(McrouterTestCase):
    config = './mcrouter/test/mcrouter_test_basic_1_1_1.json'
    extra_args = ['--timeouts-until-tko', '1',
                  '--reset-inactive-connection-interval', '750',
                  '--server-timeout', '500',
                  '--probe-timeout-initial', '500']

    def setUp(self):
        self.add_server(SleepServer())
        self.mcrouter = self.add_mcrouter(
            self.config,
            extra_args=self.extra_args)

    def test_tko_inactive(self):
        self.assertIsNone(self.mcrouter.get('hit'))
        time.sleep(3)
        self.assertTrue(self.mcrouter.is_alive())
