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

from mcrouter.test.MCProcess import Memcached
from mcrouter.test.McrouterTestCase import McrouterTestCase

class TestWarmup2(McrouterTestCase):
    config = './mcrouter/test/test_warmup2.json'

    def setUp(self):
        self.mc_warm = self.add_server(Memcached())
        self.mc_cold = self.add_server(Memcached())
        self.mcrouter = self.add_mcrouter(self.config)

    def test_warmup_get(self):
        k = 'key'
        v = 'value'
        self.assertTrue(self.mc_warm.set(k, v, exptime=1000))
        self.assertEqual(self.mcrouter.get(k), v)
        # warmup request is async
        time.sleep(1)
        self.assertTrue(self.mc_cold.get(k), v)
        cold_exptime = int(self.mc_cold.metaget(k)['exptime'])
        warm_exptime = int(self.mc_warm.metaget(k)['exptime'])
        self.assertAlmostEqual(cold_exptime, time.time() + 1000, delta=10)
        self.assertAlmostEqual(warm_exptime, time.time() + 1000, delta=10)

        self.assertTrue(self.mc_warm.delete(k))
        self.assertEqual(self.mcrouter.get(k), v)
        self.assertTrue(self.mc_cold.delete(k))
        self.assertIsNone(self.mcrouter.get(k))

        k = 'key2'
        v = 'value2'
        self.assertTrue(self.mc_warm.set(k, v))
        self.assertEqual(self.mcrouter.get(k), v)
        # warmup request is async
        time.sleep(1)
        self.assertTrue(self.mc_cold.get(k), v)
        cold_exptime = int(self.mc_cold.metaget(k)['exptime'])
        warm_exptime = int(self.mc_warm.metaget(k)['exptime'])
        self.assertEqual(cold_exptime, 0)
        self.assertEqual(warm_exptime, 0)

        # you should keep warm consistent by yourself
        self.assertTrue(self.mcrouter.delete(k))
        self.assertEqual(self.mc_warm.get(k), v)
        self.assertEqual(self.mcrouter.get(k), v)

    def test_warmup_lease_get(self):
        k = 'key'
        v = 'value'

        ret = self.mcrouter.leaseGet(k)
        self.assertEqual(ret['value'], '')
        self.assertIsNotNone(ret['token'])
        ret['value'] = v
        self.assertTrue(self.mcrouter.leaseSet(k, ret))
        ret = self.mcrouter.leaseGet(k)
        self.assertEqual(ret['value'], v)
        self.assertIsNone(ret['token'])

        k = 'key2'
        v = 'value2'
        self.assertTrue(self.mc_warm.set(k, v, exptime=1000))
        ret = self.mcrouter.leaseGet(k)
        self.assertEqual(ret['value'], v)
        self.assertIsNone(ret['token'])
        # warmup request is async
        time.sleep(1)
        ret = self.mc_cold.leaseGet(k)
        self.assertEqual(ret['value'], v)
        self.assertIsNone(ret['token'])
        cold_exptime = int(self.mc_cold.metaget(k)['exptime'])
        warm_exptime = int(self.mc_warm.metaget(k)['exptime'])
        self.assertAlmostEqual(cold_exptime, time.time() + 1000, delta=10)
        self.assertAlmostEqual(warm_exptime, time.time() + 1000, delta=10)

    def test_warmup_metaget(self):
        k = 'key'
        v = 'value'

        self.assertEqual(len(self.mcrouter.metaget(k)), 0)

        self.assertTrue(self.mc_warm.set(k, v))
        self.assertEqual(self.mcrouter.metaget(k)['exptime'], '0')
        self.assertTrue(self.mc_warm.delete(k))
        self.assertEqual(len(self.mcrouter.metaget(k)), 0)
        self.assertTrue(self.mc_cold.set(k, v))
        self.assertEqual(self.mcrouter.metaget(k)['exptime'], '0')
