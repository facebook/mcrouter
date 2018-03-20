# Copyright (c) 2015-present, Facebook, Inc.
#
# This source code is licensed under the MIT license found in the LICENSE
# file in the root directory of this source tree.

from __future__ import absolute_import
from __future__ import division
from __future__ import print_function
from __future__ import unicode_literals

from mcrouter.test.MCProcess import Mcrouter
from mcrouter.test.McrouterTestCase import McrouterTestCase
from mcrouter.test.mock_servers import SleepServer

import time
import os


class TestPoolStats(McrouterTestCase):
    config = './mcrouter/test/test_poolstats.json'
    null_route_config = './mcrouter/test/test_nullroute.json'
    mcrouter_server_extra_args = []
    extra_args = [
        '--pool-stats-config-file=./mcrouter/test/test_poolstats_config.json',
        '--timeouts-until-tko=50',
        '--disable-miss-on-get-errors',
        '--num-proxies=4']
    stat_prefix = 'libmcrouter.mcrouter.0.'

    def setUp(self):
        self.mc = []
        for _i in range(2):
            self.mc.append(Mcrouter(self.null_route_config,
                           extra_args=self.mcrouter_server_extra_args))
            self.add_server(self.mc[_i])

        # configure SleepServer for the east and wc pools
        for _i in range(2):
            self.mc.append(SleepServer())
            self.add_server(self.mc[_i + 2])

        self.mcrouter = self.add_mcrouter(
            self.config,
            extra_args=self.extra_args)

    def check_pool_stats(self, stats_dir):
        file_stat = os.path.join(stats_dir, self.stat_prefix + 'stats')
        sprefix = self.stat_prefix + 'twmemcache.CI.'
        verifiedEastReqs = False
        verifiedWestReqs = False
        verifiedEastErrs = False
        verifiedWestErrs = False
        with open(file_stat, 'r') as f:
            for line in f.readlines():
                # Expect all east requests to fail because it
                # is running SleepServer
                if sprefix + 'east.requests.sum' in line:
                    s = line.split(':')[1].split(',')[0]
                    self.assertEqual(int(s), 50)
                    verifiedEastReqs = True
                if sprefix + 'east.final_result_error.sum' in line:
                    s = line.split(':')[1].split(',')[0]
                    self.assertEqual(int(s), 50)
                    verifiedEastErrs = True
                if sprefix + 'west.requests.sum' in line:
                    s = line.split(':')[1].split(',')[0]
                    self.assertEqual(int(s), 100)
                    verifiedWestReqs = True
                if sprefix + 'west.final_result_error.sum' in line:
                    s = line.split(':')[1].split(',')[0]
                    self.assertEqual(int(s), 0)
                    verifiedWestErrs = True
        self.assertTrue(verifiedEastReqs)
        self.assertTrue(verifiedEastErrs)
        self.assertTrue(verifiedWestReqs)
        self.assertTrue(verifiedWestErrs)

    def test_poolstats(self):
        n = 150
        for i in range(0, n):
            m = i % 3
            if m == 0:
                key = 'twmemcache.CI.west:{}:|#|id=123'.format(i)
            elif m == 1:
                key = 'twmemcache.CI.west.1:{}:|#|id=123'.format(i)
            else:
                key = 'twmemcache.CI.east.1:{}:|#|id=123'.format(i)
            self.mcrouter.get(key)
        self.assertTrue(self.mcrouter.stats()['cmd_get_count'] > 0)
        time.sleep(11)
        self.check_pool_stats(self.mcrouter.stats_dir)
