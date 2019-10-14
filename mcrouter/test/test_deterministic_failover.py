# Copyright (c) Facebook, Inc. and its affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from __future__ import absolute_import
from __future__ import division
from __future__ import print_function
from __future__ import unicode_literals

from mcrouter.test.MCProcess import Mcrouter
from mcrouter.test.McrouterTestCase import McrouterTestCase
from mcrouter.test.mock_servers import SleepServer

import time


class TestDeterministicFailoverNoFailure(McrouterTestCase):
    config = './mcrouter/test/test_deterministic_failover.json'
    null_route_config = './mcrouter/test/test_nullroute.json'
    mcrouter_server_extra_args = []
    extra_args = [
        '--timeouts-until-tko=1',
        '--num-proxies=4']

    def setUp(self):
        self.mc = []
        # configure SleepServer for the east and wc pools
        for _i in range(5):
            self.mc.append(Mcrouter(self.null_route_config,
                           extra_args=self.mcrouter_server_extra_args))
            self.add_server(self.mc[_i])

        for _i in range(12):
            self.mc.append(Mcrouter(self.null_route_config,
                           extra_args=self.mcrouter_server_extra_args))
            self.add_server(self.mc[5 + _i])

        self.mcrouter = self.add_mcrouter(
            self.config,
            extra_args=self.extra_args)

    def test_deterministic_failover(self):
        for i in range(0, 0):
            key = 'key_{}'.format(i)
            self.mcrouter.get(key)
        time.sleep(5)
        stats = self.mcrouter.stats('all')
        self.assertEqual(int(stats['result_error_count']), 0)


class TestDeterministicFailoverAllSleepServers(McrouterTestCase):
    config = './mcrouter/test/test_deterministic_failover.json'
    null_route_config = './mcrouter/test/test_nullroute.json'
    mcrouter_server_extra_args = []
    extra_args = [
        '--timeouts-until-tko=1',
        '--disable-miss-on-get-errors',
        '--num-proxies=1']

    def setUp(self):
        self.mc = []
        # configure SleepServer for all servers
        for _i in range(17):
            self.mc.append(SleepServer())
            self.add_server(self.mc[_i])

        self.mcrouter = self.add_mcrouter(
            self.config,
            extra_args=self.extra_args)

    def test_deterministic_failover(self):
        for i in range(0, 10):
            key = 'key_{}_abc_{}'.format(i, 17 * i)
            self.mcrouter.get(key)
            time.sleep(1)
            stats = self.mcrouter.stats('all')
            # The following stats include both the normal route and failover
            # route responses when all servers are not present (ie expected to
            # timeout and be declared TKO after the first failure)
            # The progression of result errors and tko errors show how well the
            # hash function is working
            if i == 0:
                self.assertEqual(int(stats["failover_policy_result_error"]), 4)
                self.assertEqual(int(stats["failover_policy_tko_error"]), 0)
            elif i == 1:
                self.assertEqual(int(stats["failover_policy_result_error"]), 8)
                self.assertEqual(int(stats["failover_policy_tko_error"]), 1)
            elif i == 2:
                self.assertEqual(int(stats["failover_policy_result_error"]), 11)
                self.assertEqual(int(stats["failover_policy_tko_error"]), 5)
            elif i == 3:
                self.assertEqual(int(stats["failover_policy_result_error"]), 13)
                self.assertEqual(int(stats["failover_policy_tko_error"]), 13)
            elif i == 4:
                self.assertEqual(int(stats["failover_policy_result_error"]), 14)
                self.assertEqual(int(stats["failover_policy_tko_error"]), 22)
            elif i == 5:
                self.assertEqual(int(stats["failover_policy_result_error"]), 15)
                self.assertEqual(int(stats["failover_policy_tko_error"]), 31)
            elif i == 6:
                self.assertEqual(int(stats["failover_policy_result_error"]), 15)
                self.assertEqual(int(stats["failover_policy_tko_error"]), 41)
            elif i == 7:
                self.assertEqual(int(stats["failover_policy_result_error"]), 16)
                self.assertEqual(int(stats["failover_policy_tko_error"]), 50)
            elif i == 8:
                self.assertEqual(int(stats["failover_policy_result_error"]), 16)
                self.assertEqual(int(stats["failover_policy_tko_error"]), 60)
            elif i == 9:
                self.assertEqual(int(stats["failover_policy_result_error"]), 17)
                self.assertEqual(int(stats["failover_policy_tko_error"]), 69)


class TestDeterministicFailoverAllSleepServersSamePool(McrouterTestCase):
    config = './mcrouter/test/test_deterministic_failover2.json'
    null_route_config = './mcrouter/test/test_nullroute.json'
    mcrouter_server_extra_args = []
    extra_args = [
        '--timeouts-until-tko=1',
        '--disable-miss-on-get-errors',
        '--num-proxies=1']

    def setUp(self):
        self.mc = []
        # configure SleepServer for all servers
        for _i in range(23):
            self.mc.append(SleepServer())
            self.add_server(self.mc[_i])

        self.mcrouter = self.add_mcrouter(
            self.config,
            extra_args=self.extra_args)

    def test_deterministic_failover(self):
        for i in range(0, 10):
            key = 'key_{}_hYgGEs_{}_kBVq9Z_{}'.format(13 * i, i, 71 * i, i)
            self.mcrouter.get(key)
            time.sleep(1)
            stats = self.mcrouter.stats('all')
            # The following stats include both the normal route and failover
            # route responses when all servers are not present (ie expected to
            # timeout and be declared TKO after the first failure)
            # The progression of result errors and tko errors show how well the
            if i == 0:
                self.assertEqual(int(stats["failover_policy_result_error"]), 4)
                self.assertEqual(int(stats["failover_policy_tko_error"]), 1)
                self.assertEqual(int(stats["failover_num_collisions"]), 2)
            elif i == 1:
                self.assertEqual(int(stats["failover_policy_result_error"]), 7)
                self.assertEqual(int(stats["failover_policy_tko_error"]), 3)
                self.assertEqual(int(stats["failover_num_collisions"]), 3)
            elif i == 2:
                self.assertEqual(int(stats["failover_policy_result_error"]), 10)
                self.assertEqual(int(stats["failover_policy_tko_error"]), 4)
                self.assertEqual(int(stats["failover_num_collisions"]), 3)
            elif i == 3:
                self.assertEqual(int(stats["failover_policy_result_error"]), 14)
                self.assertEqual(int(stats["failover_policy_tko_error"]), 7)
                self.assertEqual(int(stats["failover_num_collisions"]), 4)
            elif i == 4:
                self.assertEqual(int(stats["failover_policy_result_error"]), 17)
                self.assertEqual(int(stats["failover_policy_tko_error"]), 13)
                self.assertEqual(int(stats["failover_num_collisions"]), 8)
            elif i == 5:
                self.assertEqual(int(stats["failover_policy_result_error"]), 19)
                self.assertEqual(int(stats["failover_policy_tko_error"]), 23)
                self.assertEqual(int(stats["failover_num_collisions"]), 14)
            elif i == 6:
                self.assertEqual(int(stats["failover_policy_result_error"]), 21)
                self.assertEqual(int(stats["failover_policy_tko_error"]), 33)
                self.assertEqual(int(stats["failover_num_collisions"]), 16)
            elif i == 7:
                self.assertEqual(int(stats["failover_policy_result_error"]), 21)
                self.assertEqual(int(stats["failover_policy_tko_error"]), 45)
                self.assertEqual(int(stats["failover_num_collisions"]), 21)
            elif i == 8:
                self.assertEqual(int(stats["failover_policy_result_error"]), 23)
                self.assertEqual(int(stats["failover_policy_tko_error"]), 55)
                self.assertEqual(int(stats["failover_num_collisions"]), 27)
            elif i == 9:
                self.assertEqual(int(stats["failover_policy_result_error"]), 23)
                self.assertEqual(int(stats["failover_policy_tko_error"]), 67)
                self.assertEqual(int(stats["failover_num_collisions"]), 29)


class TestDeterministicFailoverAllSleepServersSharedConfig(McrouterTestCase):
    config = './mcrouter/test/test_deterministic_failover3.json'
    null_route_config = './mcrouter/test/test_nullroute.json'
    mcrouter_server_extra_args = []
    extra_args = [
        '--timeouts-until-tko=1',
        '--disable-miss-on-get-errors',
        '--route-prefix=/Route/A/',
        '--num-proxies=1']

    def setUp(self):
        self.mc = []
        # configure SleepServer for all servers
        for _i in range(23):
            self.mc.append(SleepServer())
            self.add_server(self.mc[_i])

        self.mcrouter = self.add_mcrouter(
            self.config,
            extra_args=self.extra_args)

    def test_deterministic_failover(self):
        for i in range(0, 8):
            key = 'key_{}_hYgGEs_{}_kBVq9Z_{}'.format(13 * i, i, 71 * i, i)
            self.mcrouter.get(key)
            time.sleep(1)
            stats = self.mcrouter.stats('all')
            # The following stats include both the normal route and failover
            # route responses when all servers are not present (ie expected to
            # timeout and be declared TKO after the first failure)
            # The progression of result errors and tko errors show how well the
            if i == 0:
                self.assertEqual(int(stats["failover_policy_result_error"]), 4)
                self.assertEqual(int(stats["failover_policy_tko_error"]), 0)
                self.assertEqual(int(stats["failover_num_collisions"]), 1)
            elif i == 1:
                self.assertEqual(int(stats["failover_policy_result_error"]), 8)
                self.assertEqual(int(stats["failover_policy_tko_error"]), 0)
                self.assertEqual(int(stats["failover_num_collisions"]), 2)
            elif i == 2:
                self.assertEqual(int(stats["failover_policy_result_error"]), 12)
                self.assertEqual(int(stats["failover_policy_tko_error"]), 1)
                self.assertEqual(int(stats["failover_num_collisions"]), 2)
            elif i == 3:
                self.assertEqual(int(stats["failover_policy_result_error"]), 16)
                self.assertEqual(int(stats["failover_policy_tko_error"]), 4)
                self.assertEqual(int(stats["failover_num_collisions"]), 3)
            elif i == 4:
                self.assertEqual(int(stats["failover_policy_result_error"]), 19)
                self.assertEqual(int(stats["failover_policy_tko_error"]), 12)
                self.assertEqual(int(stats["failover_num_collisions"]), 8)
            elif i == 5:
                self.assertEqual(int(stats["failover_policy_result_error"]), 21)
                self.assertEqual(int(stats["failover_policy_tko_error"]), 22)
                self.assertEqual(int(stats["failover_num_collisions"]), 14)
            elif i == 6:
                self.assertEqual(int(stats["failover_policy_result_error"]), 23)
                self.assertEqual(int(stats["failover_policy_tko_error"]), 32)
                self.assertEqual(int(stats["failover_num_collisions"]), 16)
            elif i == 7:
                self.assertEqual(int(stats["failover_policy_result_error"]), 23)
                self.assertEqual(int(stats["failover_policy_tko_error"]), 44)
                self.assertEqual(int(stats["failover_num_collisions"]), 21)


class TestDeterministicFailoverSMCConfig(McrouterTestCase):
    config = './mcrouter/test/test_deterministic_failover4.json'
    null_route_config = './mcrouter/test/test_nullroute.json'
    mcrouter_server_extra_args = []
    extra_args = [
        '--timeouts-until-tko=1',
        '--disable-miss-on-get-errors',
        '--route-prefix=/Route/A/',
        '--num-proxies=1']

    def setUp(self):
        self.mc = []
        # configure SleepServer for all servers
        self.mcrouter = self.add_mcrouter(
            self.config,
            extra_args=self.extra_args)

    def test_deterministic_failover(self):
        for i in range(0, 10):
            key = 'key_{}_hYgGEs_{}_kBVq9Z_{}'.format(13 * i, i, 71 * i, i)
            self.mcrouter.get(key)
            time.sleep(1)
            stats = self.mcrouter.stats('all')
            # The following stats include both the normal route and failover
            # route responses when all servers are not present (ie expected to
            # timeout and be declared TKO after the first failure)
            # The progression of result errors and tko errors show how well the
            if i == 0:
                self.assertEqual(int(stats["failover_policy_result_error"]), 4)
                self.assertEqual(int(stats["failover_policy_tko_error"]), 0)
                self.assertEqual(int(stats["failover_num_collisions"]), 0)
            elif i == 1:
                self.assertEqual(int(stats["failover_policy_result_error"]), 8)
                self.assertEqual(int(stats["failover_policy_tko_error"]), 0)
                self.assertEqual(int(stats["failover_num_collisions"]), 0)
            elif i == 2:
                self.assertEqual(int(stats["failover_policy_result_error"]), 12)
                self.assertEqual(int(stats["failover_policy_tko_error"]), 0)
                self.assertEqual(int(stats["failover_num_collisions"]), 0)
            elif i == 3:
                self.assertEqual(int(stats["failover_policy_result_error"]), 16)
                self.assertEqual(int(stats["failover_policy_tko_error"]), 0)
                self.assertEqual(int(stats["failover_num_collisions"]), 0)
            elif i == 4:
                self.assertEqual(int(stats["failover_policy_result_error"]), 20)
                self.assertEqual(int(stats["failover_policy_tko_error"]), 0)
                self.assertEqual(int(stats["failover_num_collisions"]), 0)
            elif i == 5:
                self.assertEqual(int(stats["failover_policy_result_error"]), 24)
                self.assertEqual(int(stats["failover_policy_tko_error"]), 0)
                self.assertEqual(int(stats["failover_num_collisions"]), 0)
            elif i == 6:
                self.assertEqual(int(stats["failover_policy_result_error"]), 28)
                self.assertEqual(int(stats["failover_policy_tko_error"]), 0)
                self.assertEqual(int(stats["failover_num_collisions"]), 0)
            elif i == 7:
                self.assertEqual(int(stats["failover_policy_result_error"]), 32)
                self.assertEqual(int(stats["failover_policy_tko_error"]), 1)
                self.assertEqual(int(stats["failover_num_collisions"]), 0)
            elif i == 8:
                self.assertEqual(int(stats["failover_policy_result_error"]), 36)
                self.assertEqual(int(stats["failover_policy_tko_error"]), 1)
                self.assertEqual(int(stats["failover_num_collisions"]), 0)
            elif i == 9:
                self.assertEqual(int(stats["failover_policy_result_error"]), 40)
                self.assertEqual(int(stats["failover_policy_tko_error"]), 1)
                self.assertEqual(int(stats["failover_num_collisions"]), 0)
