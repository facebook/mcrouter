# Copyright (c) 2015-present, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

from __future__ import absolute_import
from __future__ import division
from __future__ import print_function
from __future__ import unicode_literals

from mcrouter.test.MCProcess import Mcrouter
from mcrouter.test.McrouterTestCase import McrouterTestCase


class TestLoadBalancerRoute(McrouterTestCase):
    config = './mcrouter/test/test_loadbalancer_route.json'
    null_route_config = './mcrouter/test/test_nullroute.json'
    mcrouter_server_extra_args = ['--server-load-interval-ms=50']
    extra_args = []

    def setUp(self):
        self.mc = []
        for _i in range(8):
            self.mc.append(Mcrouter(self.null_route_config,
                           extra_args=self.mcrouter_server_extra_args))
            self.add_server(self.mc[_i])

        self.mcrouter = self.add_mcrouter(
            self.config,
            extra_args=self.extra_args)

    def test_loadbalancer(self):
        n = 20000
        for i in range(0, n):
            key = 'someprefix:{}:|#|id=123'.format(i)
            self.assertTrue(not self.mcrouter.get(key))
        self.assertTrue(self.mcrouter.stats()['cmd_get_count'] > 0)
        sum = 0
        for i in range(8):
            self.assertTrue(self.mc[i].stats()['cmd_get_count'] > 0)
            sum += int(self.mc[i].stats()['cmd_get_count'])
        self.assertEqual(sum, n)
