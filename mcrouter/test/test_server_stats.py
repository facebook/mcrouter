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

from mcrouter.test.McrouterTestCase import McrouterTestCase
from mcrouter.test.mock_servers import SleepServer

class TestServerStatsOutstandingRequests(McrouterTestCase):
    config = './mcrouter/test/test_server_stats_pending.json'
    extra_args = ['--server-timeout', '5000']

    def setUp(self):
        # The order here must corresponds to the order of hosts in the .json
        self.add_server(SleepServer())
        self.mcrouter = self.add_mcrouter(
            self.config, extra_args=self.extra_args
        )

    def test_server_stats(self):
        self.mcrouter.set('test:foo', 'bar')
        stats = self.mcrouter.stats('servers')
        num_stats = 0
        for stat_key, stat_value in stats.iteritems():
            num_stats += 1
            num_outstanding_reqs = 0
            value_parts = stat_value.split(' ')
            pending_reqs = value_parts[1].split(':')
            inflight_reqs = value_parts[2].split(':')
            num_outstanding_reqs += int(pending_reqs[1])
            num_outstanding_reqs += int(inflight_reqs[1])
            self.assertEqual('avg_latency_us:0.000', value_parts[0])
            self.assertEqual('pending_reqs', pending_reqs[0])
            self.assertEqual('inflight_reqs', inflight_reqs[0])
            self.assertEqual(1, num_outstanding_reqs)
        self.assertEqual(1, num_stats)
