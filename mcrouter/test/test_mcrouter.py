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

class TestDevNull(McrouterTestCase):
    config = './mcrouter/test/test_dev_null.json'
    extra_args = []

    def setUp(self):
        # The order here must corresponds to the order of hosts in the .json
        self.mc_good = self.add_server(Memcached())
        self.mc_wild = self.add_server(Memcached())

    def get_mcrouter(self):
        return self.add_mcrouter(self.config, extra_args=self.extra_args)

    def test_dev_null(self):
        mcr = self.get_mcrouter()

        # finally setup is done
        mcr.set("good:key", "should_be_set")
        mcr.set("key", "should_be_set_wild")
        mcr.set("null:key", "should_not_be_set")
        mcgood_val = self.mc_good.get("good:key")
        mcnull_val = self.mc_wild.get("null:key")
        mcwild_val = self.mc_wild.get("key")

        self.assertEqual(mcgood_val, "should_be_set")
        self.assertEqual(mcnull_val, None)
        self.assertEqual(mcwild_val, "should_be_set_wild")

        self.assertEqual(mcr.delete("null:key2"), None)
        self.assertEqual(int(mcr.stats('ods')['dev_null_requests']), 2)

class TestMigratedPools(McrouterTestCase):
    config = './mcrouter/test/test_migrated_pools.json'
    extra_args = []

    def setUp(self):
        self.wild_new = self.add_server(Memcached())
        self.wild_old = self.add_server(Memcached())

    def get_mcrouter(self):
        return self.add_mcrouter(
            self.config, extra_args=self.extra_args,
            replace_map={"START_TIME": (int(time.time()) + 2)})

    def test_migrated_pools(self):
        mcr = self.get_mcrouter()

        #set keys that should be deleted in later phases
        for phase in range(1, 5):
            self.wild_old.set("get-key-" + str(phase), str(phase))
            self.wild_new.set("get-key-" + str(phase), str(phase * 100))

        # first we are in the old domain make sure all ops go to
        # the old host only
        self.assertEqual(mcr.get("get-key-1"), str(1))
        mcr.set("set-key-1", str(42))
        self.assertEqual(self.wild_old.get("set-key-1"), str(42))
        self.assertEqual(self.wild_new.get("set-key-1"), None)
        mcr.delete("get-key-1")
        #make sure the delete went to old but not new
        self.assertEqual(self.wild_old.get("get-key-1"), None)
        self.assertEqual(self.wild_new.get("get-key-1"), str(100))

        #next phase
        time.sleep(2)
        # gets/sets go to the old place
        self.assertEqual(mcr.get("get-key-2"), str(2))
        mcr.set("set-key-2", str(4242))
        self.assertEqual(self.wild_old.get("set-key-2"), str(4242))
        self.assertEqual(self.wild_new.get("set-key-2"), None)

        mcr.delete("get-key-2")
        #make sure the delete went to both places
        self.assertEqual(self.wild_old.get("get-key-2"), None)
        self.assertEqual(self.wild_new.get("get-key-2"), None)

        #next phase
        time.sleep(2)
        # gets/sets go to the new place
        self.assertEqual(mcr.get("get-key-3"), str(300))
        mcr.set("set-key-3", str(424242))
        self.assertEqual(self.wild_old.get("set-key-3"), None)
        self.assertEqual(self.wild_new.get("set-key-3"), str(424242))

        mcr.delete("get-key-3")
        #make sure the delete went to both places
        self.assertEqual(self.wild_old.get("get-key-3"), None)
        self.assertEqual(self.wild_new.get("get-key-3"), None)

        #next phase
        time.sleep(2)
        # gets/sets go to the new place
        self.assertEqual(mcr.get("get-key-4"), str(400))
        mcr.set("set-key-4", str(42424242))
        self.assertEqual(self.wild_old.get("set-key-4"), None)
        self.assertEqual(self.wild_new.get("set-key-4"), str(42424242))

        mcr.delete("get-key-4")
        #make sure the delete went to the new place only
        self.assertEqual(self.wild_old.get("get-key-4"), str(4))
        self.assertEqual(self.wild_new.get("get-key-4"), None)

class TestMigratedPoolsFailover(McrouterTestCase):
    config = './mcrouter/test/test_migrated_pools_failover.json'
    extra_args = []

    def setUp(self):
        self.a_new = self.add_server(Memcached())
        self.a_old = self.add_server(Memcached())
        self.b_new = self.add_server(Memcached())
        self.b_old = self.add_server(Memcached())

    def get_mcrouter(self):
        return self.add_mcrouter(
            self.config, extra_args=self.extra_args,
            replace_map={"START_TIME": (int(time.time()) + 2)})

    def test_migrated_pools_failover(self):
        mcr = self.get_mcrouter()

        #set keys that should be deleted in later phases
        for phase in range(1, 5):
            self.a_old.set("get-key-" + str(phase), str(phase))
            self.a_new.set("get-key-" + str(phase), str(phase * 10))
            self.b_old.set("get-key-" + str(phase), str(phase * 100))
            self.b_new.set("get-key-" + str(phase), str(phase * 1000))

        # first we are in the old domain make sure all ops go to
        # the old host only
        self.assertEqual(mcr.get("get-key-1"), str(1))
        mcr.set("set-key-1", str(42))
        self.assertEqual(self.a_old.get("set-key-1"), str(42))

        self.a_old.terminate()
        self.assertEqual(mcr.get("get-key-1"), str(100))
        mcr.set("set-key-1", str(42))
        self.assertEqual(self.b_old.get("set-key-1"), str(42))

        #next phase
        time.sleep(2.5)
        self.assertEqual(mcr.get("get-key-2"), str(200))
        mcr.set("set-key-2", str(42))
        self.assertEqual(self.b_old.get("set-key-2"), str(42))

        #next phase
        time.sleep(2.5)
        # gets/sets go to the new place
        self.assertEqual(mcr.get("get-key-3"), str(30))
        mcr.set("set-key-3", str(424242))
        self.assertEqual(self.a_new.get("set-key-3"), str(424242))

        self.a_new.terminate()
        self.assertEqual(mcr.get("get-key-3"), str(3000))

class TestDuplicateServers(McrouterTestCase):
    config = './mcrouter/test/test_duplicate_servers.json'
    extra_args = []

    def setUp(self):
        self.wildcard = self.add_server(Memcached(), 12345)

    def get_mcrouter(self):
        return self.add_mcrouter(
            self.config, '/a/a/', extra_args=self.extra_args)

    def test_duplicate_servers(self):
        mcr = self.get_mcrouter()

        stats = mcr.stats('servers')
        # Check that only one proxy destination connection is made
        # for all the duplicate servers
        self.assertEqual(1, len(stats))
        # Hardcoding default server timeout
        key = 'localhost:' + str(self.port_map[12345]) + ':TCP:ascii-1000'
        self.assertTrue(key in stats)

class TestDuplicateServersDiffTimeouts(McrouterTestCase):
    config = './mcrouter/test/test_duplicate_servers_difftimeouts.json'
    extra_args = []

    def setUp(self):
        self.wildcard = self.add_server(Memcached(), 12345)

    def get_mcrouter(self):
        return self.add_mcrouter(
            self.config, '/a/a/', extra_args=self.extra_args)

    def test_duplicate_servers_difftimeouts(self):
        mcr = self.get_mcrouter()

        stats = mcr.stats('servers')
        # Check that only two proxy destination connections are made
        # for all the duplicate servers in pools with diff timeout
        self.assertEqual(2, len(stats))
        # Hardcoding default server timeout
        key = 'localhost:' + str(self.port_map[12345]) + ':TCP:ascii-1000'
        self.assertTrue(key in stats)

        key = 'localhost:' + str(self.port_map[12345]) + ':TCP:ascii-2000'
        self.assertTrue(key in stats)

class TestSamePoolFailover(McrouterTestCase):
    config = './mcrouter/test/test_same_pool_failover.json'
    extra_args = []

    def setUp(self):
        self.add_server(Memcached(), 12345)

    def get_mcrouter(self):
        return self.add_mcrouter(self.config, extra_args=self.extra_args)

    def test_same_pool_failover(self):
        mcr = self.get_mcrouter()

        self.assertEqual(mcr.get('foobar'), None)
        self.assertTrue(mcr.set('foobar', 'bizbang'))
        self.assertEqual(mcr.get('foobar'), 'bizbang')
        mcr.delete('foobar')
        self.assertEqual(mcr.get('foobar'), None)

class TestGetFailover(McrouterTestCase):
    config = './mcrouter/test/test_get_failover.json'
    extra_args = []

    def setUp(self):
        self.gut = self.add_server(Memcached())
        self.wildcard = self.add_server(Memcached())

    def get_mcrouter(self):
        return self.add_mcrouter(self.config, extra_args=self.extra_args)

    def failover_common(self, key):
        self.mcr = self.get_mcrouter()

        self.assertEqual(self.mcr.get(key), None)
        self.assertTrue(self.mcr.set(key, 'bizbang'))
        self.assertEqual(self.mcr.get(key), 'bizbang')

        # kill the main host so everything failsover to gut
        self.wildcard.terminate()

        self.assertEqual(self.mcr.get(key), None)
        self.assertTrue(self.mcr.set(key, 'bizbang-fail'))
        self.assertEqual(self.mcr.get(key), 'bizbang-fail')

    def test_get_failover(self):
        self.failover_common('testkey')
        # the failover should have set it with a much shorter TTL
        # so make sure that we can't get the value after the TTL
        # has expired
        time.sleep(4)
        self.assertEqual(self.mcr.get('testkey'), None)

class TestGetFailoverWithFailoverTag(TestGetFailover):
    config = './mcrouter/test/test_get_failover_with_failover_tag.json'

    def test_get_failover(self):
        key = 'testkey|#|extra=1'
        self.failover_common(key)

        # Verify the failover tag was appended
        fail_key = key + ":failover=1"
        self.assertEqual(self.mcr.get(key), 'bizbang-fail')
        self.assertEqual(self.gut.get(fail_key), 'bizbang-fail')

class TestLeaseGetFailover(McrouterTestCase):
    config = './mcrouter/test/test_get_failover.json'
    extra_args = []

    def setUp(self):
        self.gut = self.add_server(Memcached())
        self.wildcard = self.add_server(Memcached())

    def get_mcrouter(self):
        return self.add_mcrouter(self.config, extra_args=self.extra_args)

    def test_lease_get_failover(self):
        mcr = self.get_mcrouter()

        get_res = {}
        get_res['testkey'] = mcr.leaseGet('testkey')
        get_res['testkey']['value'] = 'bizbang-lease'
        self.assertGreater(get_res['testkey']['token'], 0)
        self.assertTrue(mcr.leaseSet('testkey', get_res['testkey']))
        get_res['testkey'] = mcr.leaseGet('testkey')
        self.assertFalse(get_res['testkey']['token'])
        self.assertEqual(get_res['testkey']['value'], 'bizbang-lease')

        # kill the main host so everything failsover to mctestc00.gut
        self.wildcard.terminate()

        get_res['testkey'] = mcr.leaseGet('testkey')
        get_res['testkey']['value'] = 'bizbang-lease-fail'
        self.assertGreater(get_res['testkey']['token'], 0)
        self.assertTrue(mcr.leaseSet('testkey', get_res['testkey']))

        get_res['testkey'] = mcr.leaseGet('testkey')
        self.assertFalse(get_res['testkey']['token'])
        self.assertEqual(get_res['testkey']['value'], 'bizbang-lease-fail')

        # the failover should have set it with a much shorter TTL
        # so make sure that we can't get the value after the TTL
        # has expired
        time.sleep(4)
        get_res['testkey'] = mcr.leaseGet('testkey')
        self.assertGreater(get_res['testkey']['token'], 0)
        self.assertFalse(get_res['testkey']['value'])

class TestMetaGetFailover(McrouterTestCase):
    config = './mcrouter/test/test_get_failover.json'
    extra_args = []

    def setUp(self):
        self.gut = self.add_server(Memcached())
        self.wildcard = self.add_server(Memcached())

    def get_mcrouter(self):
        return self.add_mcrouter(self.config, extra_args=self.extra_args)

    def test_metaget_failover(self):
        mcr = self.get_mcrouter()

        get_res = {}
        self.assertTrue(mcr.set('testkey', 'bizbang'))
        get_res = mcr.metaget('testkey')
        self.assertEqual(0, int(get_res['exptime']))

        self.wildcard.terminate()

        self.assertTrue(mcr.set('testkey', 'bizbang-fail'))
        self.assertEqual(mcr.get('testkey'), 'bizbang-fail')
        get_res = mcr.metaget('testkey')
        self.assertAlmostEqual(int(get_res['exptime']),
                               int(time.time()) + 3,
                               delta=1)

        # the failover should have set it with a much shorter TTL
        # so make sure that we can't get the value after the TTL
        # has expired
        time.sleep(4)
        self.assertEqual(mcr.metaget('testkey'), {})
        self.assertEqual(mcr.get('testkey'), None)

class TestFailoverWithLimit(McrouterTestCase):
    config = './mcrouter/test/test_failover_limit.json'

    def setUp(self):
        self.gut = self.add_server(Memcached())
        self.wildcard = self.add_server(Memcached())

    def get_mcrouter(self):
        return self.add_mcrouter(self.config)

    def test_failover_limit(self):
        mcr = self.get_mcrouter()

        self.assertTrue(mcr.set('key', 'value.wildcard'))
        self.assertEqual(mcr.get('key'), 'value.wildcard')
        self.wildcard.terminate()

        # first 12 requests should succeed (10 burst + 2 rate)
        self.assertTrue(mcr.set('key', 'value.gut'))
        for i in range(11):
            self.assertEqual(mcr.get('key'), 'value.gut')
        # now every 5th request should succeed
        for i in range(10):
            for j in range(4):
                self.assertIsNone(mcr.get('key'))
            self.assertEqual(mcr.get('key'), 'value.gut')
