# Copyright (c) 2015-present, Facebook, Inc.
#
# This source code is licensed under the MIT license found in the LICENSE
# file in the root directory of this source tree.

from __future__ import absolute_import
from __future__ import division
from __future__ import print_function
from __future__ import unicode_literals

from mcrouter.test.McrouterTestCase import McrouterTestCase
import tempfile
import os
import time
from string import Template


class CarbonLookasideTmpConfig():
    routeConfigFile = """
{
  "pools": {
    "A": {
      "servers": [ "127.0.0.1:12345" ]
    }
  },
  "route": {
    "type": "CarbonLookasideRoute",
    "prefix": "$TEMPLATE_PREFIX",
    "ttl": $TEMPLATE_TTL,
    "flavor": "$TEMPLATE_FILENAME",
    "child": [
      "PoolRoute|A"
    ]
  }
}
"""
    clientConfigFile = """
{
  "pools": {
    "A": {
      "servers": [ "127.0.0.1:${TEMPLATE_PORT}" ]
    }
  },
  "route": "PoolRoute|A"
}
"""
    flavorConfigFile = """
{
  "options": {
    "asynclog_disable": "true",
    "config": "${TEMPLATE_FILENAME}",
    "failures_until_tko": "10",
    "num_proxies": "4",
    "probe_delay_initial_ms": "5000",
    "scuba_sample_period": "0",
    "server_timeout_ms": "250"
  }
}
"""

    def cleanup(self):
        if not self.tmpRouteFile:
            os.remove(self.tmpRouteFile)
        if not self.tmpClientFile:
            os.remove(self.tmpClientFile)
        if not self.tmpFlavorFile:
            os.remove(self.tmpFlavorFile)

    def __init__(self, prefix, ttl, port):
        # Client file configuration
        with tempfile.NamedTemporaryFile(mode='w', delete=False) as self.tmpClientFile:
            clientDict = {'TEMPLATE_PORT': port}
            src = Template(self.clientConfigFile)
            result = src.substitute(clientDict)
            self.tmpClientFile.write(result)
        # Flavor file configuration
        with tempfile.NamedTemporaryFile(mode='w', delete=False) as self.tmpFlavorFile:
            flavorDict = {'TEMPLATE_FILENAME': 'file:' + self.tmpClientFile.name}
            src = Template(self.flavorConfigFile)
            result = src.substitute(flavorDict)
            self.tmpFlavorFile.write(result)
        # Route file configuration
        with tempfile.NamedTemporaryFile(mode='w', delete=False) as self.tmpRouteFile:
            routeDict = {'TEMPLATE_PREFIX': prefix, 'TEMPLATE_TTL': ttl,
                         'TEMPLATE_FILENAME': 'file:' + self.tmpFlavorFile.name}
            src = Template(self.routeConfigFile)
            result = src.substitute(routeDict)
            self.tmpRouteFile.write(result)

    def getFileName(self):
        return self.tmpRouteFile.name


class TestCarbonLookasideRouteBasic(McrouterTestCase):
    prefix = "CarbonLookaside"
    ttl = 10
    null_route_config = './mcrouter/test/test_nullroute.json'
    extra_args = []

    def setUp(self):
        self.mc = self.add_server(self.make_memcached())
        self.tmpConfig = CarbonLookasideTmpConfig(self.prefix, self.ttl,
                                                  self.mc.getport())
        self.config = self.tmpConfig.getFileName()
        self.mcrouter = self.add_mcrouter(
            self.config,
            extra_args=self.extra_args)

    def tearDown(self):
        self.tmpConfig.cleanup()

    def test_carbonlookaside_basic(self):
        n = 20
        # Insert 20 items into memcache
        for i in range(0, n):
            key = 'someprefix:{}:|#|id=123'.format(i)
            self.assertTrue(self.mcrouter.set(key, 'value'))
        # Get the 20 items from memcache, they will be set in
        # carbonlookaside
        for i in range(0, n):
            key = 'someprefix:{}:|#|id=123'.format(i)
            self.assertTrue(self.mcrouter.get(key, 'value'))
        # Query carbonlookaside directly with the configured prefix
        # that the items have indeed been stored.
        for i in range(0, n):
            key = '{}someprefix:{}:|#|id=123'.format(self.prefix, i)
            self.assertTrue(self.mc.get(key, 'value'))
        # Query the items through mcrouter and check that they are there
        # This query will be fed from carbonlookaside.
        for i in range(0, n):
            key = 'someprefix:{}:|#|id=123'.format(i)
            self.assertTrue(self.mcrouter.get(key, 'value'))

    def test_carbonlookaside_larger(self):
        n = 2000
        # Insert 2000 items into memcache
        for i in range(0, n):
            key = 'someprefix:{}:|#|id=123'.format(i)
            self.assertTrue(self.mcrouter.set(key, 'value'))
        # Get the 2000 items from memcache, they will be set in
        # carbonlookaside
        for i in range(0, n):
            key = 'someprefix:{}:|#|id=123'.format(i)
            self.assertTrue(self.mcrouter.get(key, 'value'))
        # Query carbonlookaside directly with the configured prefix
        # that the items have indeed been stored.
        for i in range(0, n):
            key = '{}someprefix:{}:|#|id=123'.format(self.prefix, i)
            self.assertTrue(self.mc.get(key, 'value'))
        # Query the items through mcrouter and check that they are there
        # This query will be fed from carbonlookaside.
        for i in range(0, n):
            key = 'someprefix:{}:|#|id=123'.format(i)
            self.assertTrue(self.mcrouter.get(key, 'value'))


class TestCarbonLookasideRouteExpiry(McrouterTestCase):
    prefix = "CarbonLookaside"
    ttl = 2
    null_route_config = './mcrouter/test/test_nullroute.json'
    extra_args = []

    def setUp(self):
        self.mc = self.add_server(self.make_memcached())
        self.tmpConfig = CarbonLookasideTmpConfig(self.prefix, self.ttl,
                                                  self.mc.getport())
        self.config = self.tmpConfig.getFileName()
        self.mcrouter = self.add_mcrouter(
            self.config,
            extra_args=self.extra_args)

    def tearDown(self):
        self.tmpConfig.cleanup()

    def test_carbonlookaside_ttl_expiry(self):
        n = 20
        # Insert 20 items into memcache
        for i in range(0, n):
            key = 'someprefix:{}:|#|id=123'.format(i)
            self.assertTrue(self.mcrouter.set(key, 'value'))
        # Get the 20 items from memcache, they will be set in
        # carbonlookaside
        for i in range(0, n):
            key = 'someprefix:{}:|#|id=123'.format(i)
            self.assertTrue(self.mcrouter.get(key, 'value'))
        # Query carbonlookaside directly with the configured prefix
        # that the items have indeed been stored.
        for i in range(0, n):
            key = '{}someprefix:{}:|#|id=123'.format(self.prefix, i)
            self.assertTrue(self.mc.get(key, 'value'))
        time.sleep(2)
        # Query carbonlookaside directly and check they have expired
        for i in range(0, n):
            key = '{}someprefix:{}:|#|id=123'.format(self.prefix, i)
            self.assertFalse(self.mc.get(key, 'value'))


class TestCarbonLookasideRouteNoExpiry(McrouterTestCase):
    prefix = "CarbonLookaside"
    ttl = 0
    null_route_config = './mcrouter/test/test_nullroute.json'
    extra_args = []

    def setUp(self):
        self.mc = self.add_server(self.make_memcached())
        self.tmpConfig = CarbonLookasideTmpConfig(self.prefix, self.ttl,
                                                  self.mc.getport())
        self.config = self.tmpConfig.getFileName()
        self.mcrouter = self.add_mcrouter(
            self.config,
            extra_args=self.extra_args)

    def tearDown(self):
        self.tmpConfig.cleanup()

    def test_carbonlookaside_ttl_no_expiry(self):
        n = 20
        # Insert 20 items into memcache
        for i in range(0, n):
            key = 'someprefix:{}:|#|id=123'.format(i)
            self.assertTrue(self.mcrouter.set(key, 'value', exptime=2))
        # Get the 20 items from memcache, they will be set in
        # carbonlookaside
        for i in range(0, n):
            key = 'someprefix:{}:|#|id=123'.format(i)
            self.assertTrue(self.mcrouter.get(key, 'value'))
        time.sleep(3)
        # Items should have expired in memcache
        for i in range(0, n):
            key = 'someprefix:{}:|#|id=123'.format(i)
            self.assertFalse(self.mc.get(key, 'value'))
        # Items still available in carbonlookaside through mcrouter
        for i in range(0, n):
            key = 'someprefix:{}:|#|id=123'.format(i)
            self.assertTrue(self.mcrouter.get(key, 'value'))
