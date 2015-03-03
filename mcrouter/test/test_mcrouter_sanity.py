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

import random
import time

from mcrouter.test.MCProcess import Memcached
from mcrouter.test.McrouterTestCase import McrouterTestCase
from mcrouter.test.mock_servers import DeadServer
from mcrouter.test.mock_servers import SleepServer

def randstring(n):
    s = "0123456789abcdef"
    ans = ""
    for i in range(n):
        ans += random.choice(s)
    return ans

class TestMcrouterSanity(McrouterTestCase):
    config = './mcrouter/test/test_ascii.json'

    def setUp(self):
        mc_ports = [
            11510, 11511, 11512, 11513,
            11520, 11521, 11522, 11523,
            11530, 11531, 11532, 11533,
            11541]
        mc_gut_port = 11540
        tmo_port = 11555

        # have to do these before starting mcrouter
        self.mcs = [self.add_server(Memcached(), logical_port=port)
                    for port in mc_ports]
        self.mc_gut = self.add_server(Memcached(), logical_port=mc_gut_port)
        self.mcs.append(self.mc_gut)

        self.add_server(SleepServer(), logical_port=tmo_port)

        for port in [65532, 65522]:
            self.add_server(DeadServer(), logical_port=port)

        self.mcrouter = self.add_mcrouter(self.config)

    def data(self, n):
        """ generate n random (key, value) pairs """
        prefixes = ['foo:', 'bar:', 'baz:', 'wc:', 'lat:']
        keys = [random.choice(prefixes) + randstring(random.randint(3, 10))
                for i in range(n)]
        keys = list(set(keys))
        vals = [randstring(random.randint(3, 10)) for i in range(len(keys))]
        return zip(keys, vals)

    def test_basic(self):
        """ basic test that we get back what we put in """
        for k, v in self.data(100):
            self.mcrouter.set(k, v)
            got = self.mcrouter.get(k)
            self.assertEqual(v, got)

    def test_getset(self):
        """
        This test extends on the idea of test_basic() by doing a bunch more
        in  mcrouter
        """
        data = self.data(5000)
        for k, v in data:
            self.mcrouter.set(k, v)
            got = self.mcrouter.get(k)
            self.assertEqual(v, got)

    def test_ops(self):
        mcr = self.mcrouter
        n = 100
        data = self.data(n)
        for k, v in data:
            # set
            self.assertTrue(mcr.set(k, v))
            self.assertEqual(mcr.get(k), v)

        # multiget
        for i in range(2, n, 5):
            res = mcr.get([k for k, v in data[0:i]])
            self.assertEqual(len(res), i)
            for k, v in data[0:i]:
                self.assertEqual(mcr.get(k), v)

        for k, v in data:
            # add, replace
            v2 = randstring(random.randint(3, 10))
            self.assertFalse(mcr.add(k, v2))
            self.assertTrue(mcr.replace(k, v2))
            self.assertEqual(mcr.get(k), v2)

            # delete
            mcr.delete(k)
            self.assertIsNone(mcr.get(k))

            # add, replace
            self.assertFalse(mcr.replace(k, v))
            self.assertTrue(mcr.add(k, v))
            self.assertEqual(mcr.get(k), v)

            # arith
            i = 42
            mcr.set(k, i)
            self.assertEqual(mcr.incr(k), i + 1)
            self.assertEqual(mcr.get(k), str(i + 1))
            self.assertEqual(mcr.decr(k), i)
            self.assertEqual(mcr.get(k), str(i))

            self.assertEqual(mcr.incr(k, 4), i + 4)
            self.assertEqual(mcr.get(k), str(i + 4))
            self.assertEqual(mcr.decr(k, 4), i)
            self.assertEqual(mcr.get(k), str(i))

    def test_metaroute(self):
        """ get the route and verify if we actually use it """
        mcr = self.mcrouter
        mcds = dict([(mcd.addr[1], mcd) for mcd in self.mcs])
        for k, v in self.data(100):
            d2 = mcr.get("__mcrouter__.route(set,%s)" % k).split("\r\n")[0]
            p2 = int(d2.split(":")[1])  # host:port:protocol

            # verify that we actually use that route
            mcr.set(k, v)
            got = mcds[p2].get(k)
            self.assertEqual(got, v)

    def test_metaconfig(self):
        """ test __mcrouter__.config_file """
        mcr = self.mcrouter

        self.assertTrue(mcr.get("__mcrouter__.config_file"))
        self.assertTrue(mcr.get("__mcrouter__.config_md5_digest"))

    def test_down(self):
        """
        Test responses for down server. Ideally we'd verify that we're retrying
        when we should be, but no way to do that with black box, really. Maybe
        when some kind of stats are implemented it might expose that.
        """
        mcr = self.mcrouter
        k = 'down:foo'

        # get => None (NOT_FOUND)
        self.assertIsNone(mcr.get(k))

        # set => SERVER_ERRROR
        self.assertIsNone(mcr.set(k, 'abc'))

        # (delete,incr,decr) => NOT_FOUND
        self.assertIsNone(mcr.delete(k))

        self.assertIsNone(mcr.incr(k))

        self.assertIsNone(mcr.decr(k))

    def test_failover(self):
        """
        The server in failover pool is not up, so it should failover
        to wildcard.

        The server in the tmo pool does not respond to any pings, so check
        to make sure it fails over to wildcard.
        """

        mcr = self.mcrouter
        mcd_gut = self.mc_gut

        # request should time out
        t1 = time.time()
        self.assertTrue(mcr.set("tmo:tko", "should time out"))
        self.assertGreater(time.time() - 0.5, t1)

        s = {}
        s['failover:'] = 100
        s['tmo:'] = 5
        for key, mx in s.iteritems():
            for i in range(1, mx):
                k = key + str(i)
                v = randstring(random.randint(3, 10))

                self.assertTrue(mcr.set(k, v))
                self.assertEqual(mcr.get(k), v)
                self.assertEqual(mcd_gut.get(k), v)

            for i in range(1, mx):
                k = key + str(i)
                # delete failover is not enabled by default
                self.assertFalse(mcr.delete(k))

        # The sets being failed over should have a max expiration time
        # set of a few seconds for failover.  The tmo pool should not
        # have an expiration time.
        k = "failover:expires"
        v = "failover:expires_value"
        self.assertTrue(mcr.set(k, v))
        time.sleep(4)
        self.assertIsNone(mcd_gut.get(k))

        k = "tmo:does_not_expire"
        v = "tmo:does_not_expire_value"
        self.assertTrue(mcr.set(k, v))
        time.sleep(4)
        self.assertEqual(mcd_gut.get(k), v)

        # Test the data miss path by setting the key in
        # the gutter box and reading through mcrouter
        # Then delete through mcrouter and check
        # gutter to ensure it has been deleted
        key = 'datamiss:'
        for i in range(1, 100):
            k = key + str(i)
            v = randstring(random.randint(3, 10))

            self.assertIsNone(mcr.get(k))

            self.assertTrue(mcd_gut.set(k, v))
            self.assertEqual(mcr.get(k), v)

            self.assertTrue(mcd_gut.delete(k))
            self.assertIsNone(mcr.get(k))

            self.assertTrue(mcd_gut.set(k, v))
            self.assertEqual(mcr.get(k), v)
            self.assertTrue(mcr.delete(k))
            self.assertIsNone(mcd_gut.get(k))

    def test_version(self):
        v = self.mcrouter.version()
        self.assertTrue(v.startswith('VERSION mcrouter'))

    def test_server_stats(self):
        stats = self.mcrouter.stats('servers')
        num_stats = 0
        for stat_key, stat_value in stats.iteritems():
            key_parts = stat_key.split(':')
            self.assertEqual(4, len(key_parts))  # IP:port:transport:protocol
            num_stats += 1
            value_parts = stat_value.split(' ')
            self.assertEqual(value_parts[0], 'avg_latency_us:0.000')
            self.assertEqual(value_parts[1], 'pending_reqs:0')
            self.assertEqual(value_parts[2], 'inflight_reqs:0')

        # Not sure if there is an easy way to automate this
        # Now that we have proxy destination - no of distinct servers
        self.assertEqual(17, num_stats)

    def test_bad_commands(self):
        m = self.mcrouter
        exp = "SERVER_ERROR Command not supported\r\n"
        bad_commands = [
            'flush_regex .*\r\n',
            'prepend a 0 0 3\r\nabc\r\n',
            'append a 0 0 3\r\nabc\r\n',
            ]
        for bc in bad_commands:
            self.assertEqual(m.issue_command(bc), exp)

    def test_bad_key(self):
        m = self.mcrouter
        bad_key = 'foo:' + ('a' * 260)
        try:
            m.set(bad_key, bad_key)
            assert False, "Expected exception"
        except:
            pass
        try:
            m.get(bad_key)
            assert False, "Expected exception"
        except:
            pass

    def test_bad_stats(self):
        m = self.mcrouter
        bad_stat_cmd = 'stats abcde\r\n'
        expected_resp = 'CLIENT_ERROR bad stats command\r\n'
        self.assertEqual(m.issue_command(bad_stat_cmd), expected_resp)

    def test_server_error_message(self):
        # Test involes trying to get a key that triggers a server error
        m = self.mcrouter
        exp = b"SERVER_ERROR returned error msg with binary data \xdd\xab\r\n"
        bad_command = 'set __mockmc__.trigger_server_error 0 0 1\r\n0\r\n'

        self.assertEqual(m.issue_command(bad_command), exp)

    def test_reject_policy(self):
        # Test the reject policy
        m = self.mcrouter
        exp = "SERVER_ERROR reject\r\n"
        bad_command = 'set rej:foo 0 0 3\r\nrej\r\n'

        self.assertEqual(m.issue_command(bad_command), exp)


class TestMcrouterSanityOverUmbrella(TestMcrouterSanity):
    config = './mcrouter/test/test_umbrella.json'
