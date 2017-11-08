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

from threading import Thread
import time

from mcrouter.test.MCProcess import McrouterClient, Memcached, Mcrouter
from mcrouter.test.McrouterTestCase import McrouterTestCase


class TestMcrouterBasicBase(McrouterTestCase):
    config = './mcrouter/test/mcrouter_test_basic_1_1_1.json'
    null_route_config = './mcrouter/test/test_nullroute.json'
    extra_args = []

    def setUp(self):
        # The order here corresponds to the order of hosts in the .json
        self.mc = self.add_server(self.make_memcached())

    def get_mcrouter(self, additional_args=[]):
        return self.add_mcrouter(
            self.config, extra_args=self.extra_args + additional_args)


class TestMcrouterBasic(TestMcrouterBasicBase):
    def test_basic_lease(self):
        mcr = self.get_mcrouter()

        result = mcr.leaseGet("testkey")
        real_token = result["token"]
        self.assertNotEqual(real_token, None)
        result["value"] = "newvalue"
        result["token"] = 42000
        self.assertFalse(mcr.leaseSet("testkey", result))
        result["token"] = real_token
        self.assertTrue(mcr.leaseSet("testkey", result))
        result2 = mcr.leaseGet("testkey")
        self.assertEqual(result2["token"], None)
        self.assertEqual(result2["value"], "newvalue")

        # lease-get followed by a delete means the next lease-set will fail
        result = mcr.leaseGet("newtestkey")
        self.assertFalse(mcr.delete("newtestkey"))
        self.assertFalse(mcr.leaseSet("newtestkey", result))

    def test_invalid_key(self):
        """
        Tests behavior when mcrouter routes keys which have prefixes that are
        not in the config.
        """
        mcr = self.get_mcrouter()

        invalid_key = '/blah/bloh/key'
        self.assertFalse(mcr.set(invalid_key, 'value'))
        self.assertEqual(mcr.get(invalid_key), "SERVER_ERROR local error")

    def test_stats(self):
        mcr = self.get_mcrouter(['--proxy-threads=8'])

        # Stats without args
        res = mcr.issue_command_and_read_all('stats\r\n')
        self.assertTrue(res)
        res = mcr.issue_command_and_read_all('stats \r\n')
        self.assertTrue(res)
        res = mcr.issue_command_and_read_all('stats\n')
        self.assertTrue(res)
        res = mcr.issue_command_and_read_all('stats \n')
        self.assertTrue(res)

        # Stats with args
        args = ['detailed', 'cmd-error', 'servers', 'suspect_servers', 'count']
        for arg in args:
            res = mcr.issue_command_and_read_all('stats{0}\r\n'.format(arg))
            self.assertTrue('CLIENT_ERROR' in res)
            res = mcr.issue_command_and_read_all('stats {0}\r\n'.format(arg))
            self.assertTrue('END' in res)
            res = mcr.issue_command_and_read_all('stats {0} \r\n'.format(arg))
            self.assertTrue('END' in res)
            res = mcr.issue_command_and_read_all('stats{0}\n'.format(arg))
            self.assertTrue('CLIENT_ERROR' in res)
            res = mcr.issue_command_and_read_all('stats {0}\n'.format(arg))
            self.assertTrue('END' in res)
            res = mcr.issue_command_and_read_all('stats {0} \n'.format(arg))
            self.assertTrue('END' in res)

        # Stats with invalid arg
        res = mcr.issue_command_and_read_all('stats invalid_option\r\n')
        self.assertTrue('CLIENT_ERROR' in res)

    def test_stats_deadlock(self):
        mcr = self.get_mcrouter(['--proxy-threads=8'])

        def run_client(fail, port):
            mc = McrouterClient(port)
            mc.connect()
            for i in range(1000):
                s = mc.stats()
                if not s:
                    fail[0] = True
                    return

        f = [False]
        ts = [Thread(target=run_client, args=(f, mcr.port)) for i in range(8)]
        [t.start() for t in ts]
        [t.join() for t in ts]

        self.assertFalse(f[0])

    def test_basic_cas(self):
        mcr = self.get_mcrouter()
        self.assertIsNone(mcr.cas('key', 'value', 1))
        self.assertIsNone(mcr.gets('key'))
        self.assertTrue(mcr.add('key', 'value'))
        ret = mcr.gets('key')
        self.assertIsNotNone(ret)
        old_cas = ret['cas']
        self.assertEqual(ret['value'], 'value')
        self.assertTrue(mcr.cas('key', 'value2', ret["cas"]))
        ret = mcr.gets('key')
        self.assertEqual(ret['value'], 'value2')
        self.assertNotEqual(old_cas, ret['cas'])
        self.assertTrue(mcr.set('key', 'value2'))
        self.assertFalse(mcr.cas('key', 'value3', ret['cas']))
        self.assertEqual(mcr.gets('key')['value'], 'value2')

    def test_shutdown(self):
        mcr = self.get_mcrouter()

        mcr.shutdown()
        time.sleep(2)
        self.assertFalse(mcr.is_alive())

    def test_double_bind(self):
        mcr1 = self.get_mcrouter()
        time.sleep(1)
        mcr2 = Mcrouter(self.null_route_config, port=mcr1.port)

        time.sleep(2)
        self.assertTrue(mcr1.is_alive())
        self.assertFalse(mcr2.is_alive())

    def test_set_exptime(self):
        mcr = self.get_mcrouter()

        # positive
        self.assertTrue(mcr.set('key', 'value', exptime=10))
        self.assertEqual(mcr.get('key'), 'value')

        # negative
        self.assertTrue(mcr.set('key', 'value', exptime=-10))
        self.assertIsNone(mcr.get('key'))

        # future: year 2033
        self.assertTrue(mcr.set('key', 'value', exptime=2000000000))
        self.assertEqual(mcr.get('key'), 'value')

        # past
        self.assertTrue(mcr.set('key', 'value', exptime=1432250000))
        self.assertIsNone(mcr.get('key'))


class TestMcrouterBasicTouch(TestMcrouterBasicBase):
    def __init__(self, *args, **kwargs):
        super(TestMcrouterBasicTouch, self).__init__(*args, **kwargs)
        self.use_mock_mc = True

    def test_basic_touch(self):
        mcr = self.get_mcrouter()

        # positive
        self.assertTrue(mcr.set('key', 'value', exptime=0))
        self.assertEqual(mcr.get('key'), 'value')
        self.assertEqual(mcr.touch('key', 20), "TOUCHED")
        self.assertEqual(mcr.get('key'), 'value')

        # negative
        self.assertEqual(mcr.touch('fake_key', 20), "NOT_FOUND")
        self.assertIsNone(mcr.get('fake_key'))

        # negative exptime
        self.assertTrue(mcr.set('key1', 'value', exptime=10))
        self.assertEqual(mcr.get('key1'), 'value')
        self.assertEqual(mcr.touch('key1', -20), "TOUCHED")
        self.assertIsNone(mcr.get('key1'))

        # past
        self.assertTrue(mcr.set('key2', 'value', exptime=10))
        self.assertEqual(mcr.get('key'), 'value')
        self.assertEqual(mcr.touch('key', 1432250000), "TOUCHED")
        self.assertIsNone(mcr.get('key'))


class TestMcrouterInvalidRouteBase(McrouterTestCase):
    config = './mcrouter/test/mcrouter_test_basic_1_1_1.json'
    extra_args = ['--send-invalid-route-to-default']

    def setUp(self):
        # The order here corresponds to the order of hosts in the .json
        self.mc = self.add_server(self.make_memcached())

    def get_mcrouter(self, additional_args=[]):
        return self.add_mcrouter(
            self.config, extra_args=self.extra_args + additional_args)


class TestMcrouterInvalidRoute(TestMcrouterInvalidRouteBase):
    def test_basic_invalid_route(self):
        mcr = self.get_mcrouter()

        self.assertTrue(mcr.set("key", "value"))
        self.assertEqual(mcr.get("key"), "value")

        self.assertTrue(mcr.set("/././key", "value2"))
        self.assertEqual(mcr.get("/././key"), "value2")
        self.assertEqual(mcr.get("/f/f/key"), "value2")
        self.assertEqual(mcr.get("/test/test/key"), "value2")
        self.assertEqual(mcr.get("key"), "value2")

        self.assertTrue(mcr.set("/a/a/key", "value3"))
        self.assertEqual(mcr.get("/a/a/key"), "value3")
        self.assertEqual(mcr.get("key"), "value3")

        self.assertTrue(mcr.set("/*/a/key", "value4"))
        self.assertEqual(mcr.get("/a/a/key"), "value4")
        self.assertEqual(mcr.get("key"), "value4")

        self.assertTrue(mcr.set("/*/*/key", "value4"))
        self.assertEqual(mcr.get("/a/a/key"), "value4")
        self.assertEqual(mcr.get("key"), "value4")


class TestMcrouterInvalidRouteAppendPrepend(TestMcrouterInvalidRouteBase):
    def __init__(self, *args, **kwargs):
        super(TestMcrouterInvalidRouteAppendPrepend, self).__init__(
            *args, **kwargs)
        self.use_mock_mc = True

    def test_basic_invalid_route(self):
        mcr = self.get_mcrouter()

        self.assertTrue(mcr.set("key", "value"))
        self.assertEqual(mcr.get("key"), "value")

        self.assertEqual(mcr.append("/*/*/key", "abc"), "STORED")
        self.assertEqual(mcr.get("/a/a/key"), "valueabc")
        self.assertEqual(mcr.get("key"), "valueabc")
        self.assertEqual(mcr.prepend("/*/*/key", "123"), "STORED")
        self.assertEqual(mcr.get("/a/a/key"), "123valueabc")
        self.assertEqual(mcr.get("key"), "123valueabc")


class TestMcrouterBasic2(McrouterTestCase):
    config = './mcrouter/test/mcrouter_test_basic_2_1_1.json'
    extra_args = []

    def setUp(self):
        # The order here corresponds to the order of hosts in the .json
        self.mc1 = self.add_server(Memcached())
        self.mc2 = self.add_server(Memcached())

    def get_mcrouter(self, additional_args=[]):
        return self.add_mcrouter(
            self.config, '/a/a/', extra_args=self.extra_args + additional_args)

    def test_prefix_routing(self):
        mcr = self.get_mcrouter()

        # first test default routing prefix
        self.mc1.set("cluster1_key", "cluster1")
        self.assertEqual(mcr.get("cluster1_key"), "cluster1")

        # next set to a remote cluster
        mcr.set("/b/b/cluster2_key_router", "cluster2_router")
        self.assertEqual(self.mc2.get("cluster2_key_router"), "cluster2_router")

        # try fetching a value from a remote cluster
        self.mc2.set("cluster2_key", "cluster2")
        self.assertEqual(self.mc2.get("cluster2_key"), "cluster2")
        self.assertEqual(mcr.get("/b/b/cluster2_key"), "cluster2")

    def test_delete(self):
        mcr = self.get_mcrouter()

        mcr.set('foobarbizbang', 'some_value')
        self.assertTrue(mcr.delete('foobarbizbang'))
        self.assertFalse(mcr.delete('foobarbizbang2'))
        self.assertTrue(mcr.set('hello', 'world'))
        self.assertEqual(mcr.get('hello'), 'world')

    def test_malformed_umbrella_length(self):
        mcr = self.get_mcrouter()

        # Send an umbrella request with a malformed length, and check that we
        # get something back from the server (i.e. that it doesn't crash)
        mcr.socket.settimeout(10)
        mcr.socket.send('}}\x00\x01\x00\x00\x00\x00')
        data = mcr.socket.recv(1024)
        self.assertTrue(data)

        # else hang

    def test_use_big_value(self):
        mcr = self.get_mcrouter(['--big-value-split-threshold=100'])

        reply = mcr.get('__mcrouter__.route_handles(get,test)')
        self.assertEqual(reply.count('big-value'), 1)

    def test_no_big_value(self):
        mcr = self.get_mcrouter()

        reply = mcr.get('__mcrouter__.route_handles(get,test)')
        self.assertNotIn('big-value', reply)

    def test_enable_logging_route(self):
        mcr = self.get_mcrouter(['--enable-logging-route'])

        reply = mcr.get('__mcrouter__.route_handles(get,test)')
        self.assertEqual(reply.count('logging'), 1)

    def test_no_logging_route(self):
        mcr = self.get_mcrouter()

        reply = mcr.get('__mcrouter__.route_handles(get,test)')
        self.assertNotIn('logging', reply)


class TestBasicAllSyncBase(McrouterTestCase):
    config = './mcrouter/test/test_basic_all_sync.json'
    extra_args = []

    def setUp(self):
        # The order here corresponds to the order of hosts in the .json
        self.mc1 = self.add_server(self.make_memcached())
        self.mc2 = self.add_server(self.make_memcached())
        self.mc3 = self.add_server(self.make_memcached())


class TestBasicAllSync(TestBasicAllSyncBase):
    def get_mcrouter(self):
        return self.add_mcrouter(self.config, extra_args=self.extra_args)

    def test_basic_all_sync(self):
        """
        Tests that the responses are being aggregated and the most awful
        (based on the awfulness map) is begin returned
        """
        mcr = self.get_mcrouter()

        # set key in three cluster
        self.mc1.set("key", "value")
        self.mc2.set("key", "value")
        self.mc3.set("key", "value")
        self.assertEqual(self.mc1.get("key"), "value")
        self.assertEqual(self.mc2.get("key"), "value")
        self.assertEqual(self.mc3.get("key"), "value")
        self.assertEqual(mcr.get("key"), "value")

        # delete will return True on DELETED
        # will return False on NOT_FOUND

        # perform a delete and check the response
        # the aggregated response should be DELETED
        self.assertTrue(mcr.delete("key"))

        # set key in only one cluster
        self.mc1.set("key", "value")
        self.assertEqual(self.mc1.get("key"), "value")

        # the aggregated response should be NOT_FOUND
        self.assertFalse(mcr.delete("key"))


class TestBasicAllSyncAppendPrependTouch(TestBasicAllSyncBase):
    def __init__(self, *args, **kwargs):
        super(TestBasicAllSyncAppendPrependTouch, self).__init__(
            *args, **kwargs)
        self.use_mock_mc = True

    def get_mcrouter(self):
        return self.add_mcrouter(self.config, extra_args=self.extra_args)
    def test_append_prepend_all_sync(self):
        """
        Tests that append and prepend work with AllSync. We rely on these
        tests to verify correctness of append/prepend since we don't use
        these commands in production.
        """
        mcr = self.get_mcrouter()

        mcr.set("key", "value")
        self.assertEqual(self.mc1.get("key"), "value")
        self.assertEqual(self.mc2.get("key"), "value")
        self.assertEqual(self.mc3.get("key"), "value")
        self.assertEqual(mcr.get("key"), "value")

        self.assertEqual(mcr.append("key", "abc"), "STORED")
        self.assertEqual(mcr.prepend("key", "123"), "STORED")
        self.assertEqual(self.mc1.get("key"), "123valueabc")
        self.assertEqual(self.mc2.get("key"), "123valueabc")
        self.assertEqual(self.mc3.get("key"), "123valueabc")
        self.assertEqual(mcr.get("key"), "123valueabc")

        self.mc1.set("key2", "value")
        self.assertEqual(self.mc1.get("key2"), "value")
        self.assertEqual(self.mc1.append("key2", "xyz"), "STORED")
        self.assertEqual(self.mc1.get("key2"), "valuexyz")
        self.assertFalse(mcr.get("key2"))

        self.mc1.set("key3", "value")
        self.assertEqual(self.mc1.get("key3"), "value")
        self.assertEqual(self.mc1.prepend("key3", "xyz"), "STORED")
        self.assertEqual(self.mc1.get("key3"), "xyzvalue")
        self.assertFalse(mcr.get("key3"))

    def test_touch_all_sync(self):
        mcr = self.get_mcrouter()

        mcr.set("key", "value")
        self.assertEqual(self.mc1.get("key"), "value")
        self.assertEqual(self.mc2.get("key"), "value")
        self.assertEqual(self.mc3.get("key"), "value")
        self.assertEqual(mcr.get("key"), "value")

        self.assertEqual(mcr.touch("key", 3600), "TOUCHED")
        self.assertEqual(self.mc1.get("key"), "value")
        self.assertEqual(self.mc2.get("key"), "value")
        self.assertEqual(self.mc3.get("key"), "value")
        self.assertEqual(mcr.get("key"), "value")

        self.mc1.set("key2", "value")
        self.assertEqual(self.mc1.get("key2"), "value")
        self.assertEqual(self.mc1.touch("key2", 3600), "TOUCHED")
        self.assertEqual(self.mc1.get("key2"), "value")
        self.assertFalse(mcr.get("key2"))

        mcr.set("key3", "value")
        self.assertEqual(self.mc1.get("key3"), "value")
        self.assertEqual(self.mc1.touch("key3", -10), "TOUCHED")
        self.assertEqual(self.mc2.get("key3"), "value")
        self.assertEqual(self.mc3.get("key3"), "value")
        self.assertFalse(mcr.get("key3"))


class TestBasicAllFirst(McrouterTestCase):
    config = './mcrouter/test/test_basic_all_first.json'
    extra_args = []

    def setUp(self):
        # The order here corresponds to the order of hosts in the .json
        self.mc1 = self.add_server(Memcached())
        self.mc2 = self.add_server(Memcached())
        self.mc3 = self.add_server(Memcached())

    def get_mcrouter(self):
        return self.add_mcrouter(self.config, extra_args=self.extra_args)

    def test_basic_all_first(self):
        """
        Tests that the first non-tko response is returned
        """
        mcr = self.get_mcrouter()

        self.mc1.terminate()
        self.assertTrue(mcr.set("key", "value"))
        self.assertEqual(mcr.get("key"), "value")

class TestBasicAllMajority(McrouterTestCase):
    config = './mcrouter/test/test_basic_all_majority.json'
    extra_args = []

    def setUp(self):
        # The order here corresponds to the order of hosts in the .json
        self.mc1 = self.add_server(Memcached())
        self.mc2 = self.add_server(Memcached())
        self.mc3 = self.add_server(Memcached())
        self.mc4 = self.add_server(Memcached())

    def get_mcrouter(self):
        return self.add_mcrouter(self.config, extra_args=self.extra_args)

    def test_basic_all_majority(self):
        """
        Tests that the majority response (ties broken by awfulness) is being
        returned
        """
        mcr = self.get_mcrouter()

        # set key in four cluster
        self.mc1.set("key", "value")
        self.mc2.set("key", "value")
        self.mc3.set("key", "value")
        self.mc4.set("key", "value")
        self.assertEqual(self.mc1.get("key"), "value")
        self.assertEqual(self.mc2.get("key"), "value")
        self.assertEqual(self.mc3.get("key"), "value")
        self.assertEqual(self.mc4.get("key"), "value")
        self.assertEqual(mcr.get("key"), "value")

        # perform a delete and check the response
        # the majority response should be DELETED
        self.assertTrue(mcr.delete("key"))

        # make sure all deletes complete (otherwise they can race
        # with the sets below)
        time.sleep(1)

        # set key in three clusters
        self.assertTrue(self.mc1.set("key", "value"))
        self.assertTrue(self.mc2.set("key", "value"))
        self.assertTrue(self.mc3.set("key", "value"))
        self.assertEqual(self.mc1.get("key"), "value")
        self.assertEqual(self.mc2.get("key"), "value")
        self.assertEqual(self.mc3.get("key"), "value")

        # the majority response should be DELETED
        self.assertTrue(mcr.delete("key"))

        # make sure all deletes complete (otherwise they can race
        # with the sets below)
        time.sleep(1)

        # set key in only one clusters
        self.mc1.set("key", "value")
        self.assertEqual(self.mc1.get("key"), "value")

        # the majority response should be NOT_FOUND
        self.assertFalse(mcr.delete("key"))

        # make sure all deletes complete (otherwise they can race
        # with the sets below)
        time.sleep(1)

        # set key in two out of four clusters
        self.mc1.set("key", "value")
        self.mc2.set("key", "value")
        self.assertEqual(self.mc1.get("key"), "value")
        self.assertEqual(self.mc2.get("key"), "value")

        # the majority response should be NOT_FOUND
        # since it is sorted by awfulness map
        self.assertFalse(mcr.delete("key"))

class TestBasicFailover(McrouterTestCase):
    config = './mcrouter/test/test_basic_failover.json'
    extra_args = []

    def setUp(self):
        # The order here corresponds to the order of hosts in the .json
        self.mc1 = self.add_server(Memcached())
        self.mc2 = self.add_server(Memcached())

    def get_mcrouter(self):
        return self.add_mcrouter(self.config, extra_args=self.extra_args)

    def test_failover(self):
        """
        Tests that the failover path works.
        """

        # default path is mctestc01
        mcr = self.get_mcrouter()

        # Go through the default route and verify a get.
        self.assertTrue(self.mc1.set("key", "value"))
        self.assertEqual(mcr.get("key"), "value")

        self.mc1.terminate()

        # Go through the failover now.
        # We assert twice since in the first call mcrouter will discover
        # a tko host and it short circuits the second time.
        self.assertEqual(mcr.get("key"), None)
        self.assertEqual(mcr.get("key"), None)

        # Set in the failover and check.
        self.assertTrue(self.mc2.set("key", "value"))
        self.assertEqual(mcr.get("key"), "value")
        self.assertEqual(mcr.get("key"), "value")

    def test_failover_negative_exptime(self):
        mcr = self.get_mcrouter()

        # Go through the default route and verify a get.
        self.assertTrue(mcr.set("key", "value", exptime=0))
        self.assertEqual(mcr.get("key"), "value")

        # Exptime using negative value: past
        self.assertTrue(mcr.set("key", "value", exptime=-10))
        self.assertIsNone(mcr.get("key"))

        self.mc1.terminate()

        # Go through the failover now.
        # We assert twice since in the first call mcrouter will discover
        # a tko host and it short circuits the second time.
        self.assertEqual(mcr.get("key"), None)
        self.assertEqual(mcr.get("key"), None)

        # Check get failover still works
        self.assertTrue(self.mc2.set("key", "value"))
        self.assertEqual(mcr.get("key"), "value")
        # Exptime using negative value: past
        self.assertTrue(mcr.set("key", "value", exptime=-10))
        self.assertIsNone(mcr.get("key"))

class TestBasicFailoverOverride(McrouterTestCase):
    config = './mcrouter/test/test_basic_failover_override.json'
    extra_args = []

    def setUp(self):
        # The order here corresponds to the order of hosts in the .json
        self.mc1 = self.add_server(Memcached())
        self.mc2 = self.add_server(Memcached())

    def get_mcrouter(self):
        return self.add_mcrouter(self.config, extra_args=self.extra_args)

    def test_failover_override(self):
        """
        Tests that the failover overrides work.
        """
        mcr = self.get_mcrouter()

        # See that failovers are disabled for cluster1
        self.mc1.terminate()
        self.assertEqual(mcr.set("key1", "value1"), None)
        self.assertEqual(mcr.get("key1"), None)
        self.assertEqual(mcr.get("key1"), None)

        # Check get failover still works
        self.assertTrue(self.mc2.set("key2", "value2"))
        self.assertEqual(mcr.get("key2"), "value2")
        self.assertEqual(mcr.get("key2"), "value2")

class TestBasicFailoverLeastFailures(McrouterTestCase):
    """
    The main purpose of this test is to make sure LeastFailures policy
    is parsed correctly from json config. We rely on cpp tests to stress
    correctness of LeastFailures failover policy.
    """
    config = './mcrouter/test/test_basic_failover_least_failures.json'
    extra_args = []

    def setUp(self):
        # The order here corresponds to the order of hosts in the .json
        self.mc1 = self.add_server(Memcached())
        self.mc2 = self.add_server(Memcached())
        self.mc3 = self.add_server(Memcached())
        self.mc4 = self.add_server(Memcached())

    def get_mcrouter(self):
        return self.add_mcrouter(self.config, extra_args=self.extra_args)

    def test_failover_least_failures(self):
        mcr = self.get_mcrouter()

        self.assertTrue(self.mc4.set("key", "value"))

        self.mc1.terminate()
        self.mc2.terminate()
        self.mc3.terminate()

        # Main child #1 fails, as do 2 and 3. No request to 4 since
        # max_tries = 3
        self.assertEqual(mcr.get("key"), None)

        # Now 4 has least errors.
        self.assertEqual(mcr.get("key"), "value")

class TestMcrouterBasicL1L2(McrouterTestCase):
    config = './mcrouter/test/test_basic_l1_l2.json'
    config_ncache = './mcrouter/test/test_basic_l1_l2_ncache.json'
    extra_args = []

    def setUp(self):
        # The order here corresponds to the order of hosts in the .json
        self.l1 = self.add_server(Memcached())
        self.l2 = self.add_server(Memcached())

    def get_mcrouter(self, config):
        return self.add_mcrouter(config, extra_args=self.extra_args)

    def test_l1_l2_get(self):
        """
        Tests that gets using l1/l2 caching and result upgrading is working
        """
        mcr = self.get_mcrouter(self.config)

        # get a non-existent key
        self.assertFalse(mcr.get("key1"))

        # set keys in only l1 pool
        self.l1.set("key1", "value1")
        self.assertEqual(self.l1.get("key1"), "value1")

        # perform a get and check the response
        self.assertTrue(mcr.get("key1"), "value1")

        # set key only in l2 pool
        self.l2.set("key2", "value2")
        self.assertEqual(self.l2.get("key2"), "value2")
        self.assertEqual(self.l1.get("key2"), None)

        # perform a get and check the response
        self.assertEqual(mcr.get("key2"), "value2")

        # perform the same get until it gets upgraded to l1
        # if the test gets stuck in an infinite loop here upgrading results is
        # not working
        while self.l1.get("key2") != "value2":
            self.assertEqual(mcr.get("key2"), "value2")

    def test_l1_l2_get_l1_down(self):
        """
        Tests that gets using l1/l2 caching is working when l1 is down
        """
        mcr = self.get_mcrouter(self.config)

        # set key in l1 and l2 pools
        self.l1.set("key1", "value1")
        self.l2.set("key1", "value1")
        self.assertEqual(self.l1.get("key1"), "value1")
        self.assertEqual(self.l2.get("key1"), "value1")

        # terminate the l1 pool
        self.l1.terminate()
        # we should still be able to get from l2
        self.assertEqual(mcr.get("key1"), "value1")

    def test_l1_l2_get_l2_down(self):
        """
        Tests that gets using l1/l2 caching is working when l2 is down
        """
        mcr = self.get_mcrouter(self.config)

        # set key in l1 and l2 pools
        self.l1.set("key1", "value1")
        self.l2.set("key1", "value1")
        self.assertEqual(self.l1.get("key1"), "value1")
        self.assertEqual(self.l2.get("key1"), "value1")

        # terminate the l2 regional pool
        self.l2.terminate()
        # we should still be able to get from l1
        self.assertTrue(mcr.get("key1"), "value1")

        # terminate l1 pool as well
        self.l1.terminate()
        # we should get nothing back
        self.assertFalse(mcr.get("key1"))

    def test_l1_l2_get_ncache(self):
        mcr = self.get_mcrouter(self.config_ncache)

        # get a non-existent key
        self.assertFalse(mcr.get("key1"))

        time.sleep(1)

        self.assertEqual(self.l1.get("key1"), "ncache")
        self.assertTrue(self.l2.set("key1", "value1"))

        self.assertFalse(mcr.get("key1"))
        self.assertFalse(mcr.get("key1"))
        self.assertFalse(mcr.get("key1"))
        self.assertFalse(mcr.get("key1"))
        self.assertFalse(mcr.get("key1"))
        time.sleep(1)

        self.assertEqual(mcr.get("key1"), "value1")
        self.assertEqual(self.l1.get("key1"), "value1")


class TestMcrouterBasicL1L2SizeSplit(McrouterTestCase):
    config = './mcrouter/test/test_basic_l1_l2_sizesplit.json'
    config_bothset = './mcrouter/test/test_basic_l1_l2_sizesplit_bothset.json'
    extra_args = []
    MC_MSG_FLAG_SIZE_SPLIT = 0x20

    def setUp(self):
        # The order here corresponds to the order of hosts in the .json
        self.l1 = self.add_server(Memcached())
        self.l2 = self.add_server(Memcached())

    def get_mcrouter(self, config):
        return self.add_mcrouter(config, extra_args=self.extra_args)

    def test_l1_l2_sizesplit_get(self):
        """
        Basic functionality tests. Sets go to the right place, gets route properly
        """
        mcr = self.get_mcrouter(self.config)

        # get a non-existent key
        self.assertFalse(mcr.get("key1"))

        # set small key
        mcr.set("key1", "value1")
        # small key should be normal value in L1
        self.assertEqual(self.l1.get("key1"), "value1")
        # small key shouldn't be in L2
        self.assertFalse(self.l2.get("key1"))

        # perform a get and check the response
        self.assertEqual(mcr.get("key1"), "value1")

        # key should end up split
        value2 = "foo" * 200
        mcr.set("key2", value2)

        # response should be zero bytes and have the flag
        l1res = self.l1.get("key2", return_all_info=True)
        self.assertEqual(l1res["value"], "")
        self.assertTrue(l1res["flags"] & self.MC_MSG_FLAG_SIZE_SPLIT)
        self.assertNotEqual(self.l1.get("key2"), "value1")
        # full value on L2
        self.assertEqual(self.l2.get("key2"), value2)

        # get should run the internal redirect, give us L2 value
        self.assertEqual(mcr.get("key2"), value2)
        self.assertNotEqual(mcr.get("key2"), "")

    def test_l1_l2_sizesplit_bothget(self):
        """
        Basic functionality. Allow full setst to both pools.
        """
        mcr = self.get_mcrouter(self.config_bothset)

        self.assertFalse(mcr.get("key1"))

        # small key should only exist in L1
        mcr.set("key1", "value1")

        # small key should be normal value in L1
        self.assertEqual(self.l1.get("key1"), "value1")
        # small key shouldn't be in L2
        self.assertFalse(self.l2.get("key1"), "value1")

        # perform a get and check the response
        self.assertEqual(mcr.get("key1"), "value1")

        # key should end up split. end up in both pools.
        value2 = "foo" * 200
        mcr.set("key2", value2)
        # The write to L2 is async and we're checking it right away.
        time.sleep(1)

        self.assertEqual(self.l1.get("key2"), value2)
        self.assertEqual(self.l2.get("key2"), value2)
        self.assertEqual(mcr.get("key2"), value2)

    def test_l1_l2_get_l2_down(self):
        """
        If L2 is down, do we get expected errors.
        """
        mcr = self.get_mcrouter(self.config)

        value = "foob" * 200
        mcr.set("key", value)

        self.l2.terminate()
        self.assertEqual(self.l1.get("key"), "")
        self.assertFalse(mcr.get("key"))


class TestMcrouterPortOverride(McrouterTestCase):
    config = './mcrouter/test/mcrouter_test_portoverride.json'

    def test_portoverride(self):
        mc = self.add_server(Memcached())
        self.port_map = {}
        extra_args = ['--config-params', 'PORT:{}'.format(mc.getport())]
        mcr = self.add_mcrouter(self.config, extra_args=extra_args)
        self.assertTrue(mcr.set('key', 'value'))
        self.assertEqual(mcr.get('key'), 'value')
