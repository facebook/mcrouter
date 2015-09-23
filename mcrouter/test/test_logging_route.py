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

from mcrouter.test.MCProcess import Memcached
from mcrouter.test.McrouterTestCase import McrouterTestCase

class TestLoggingRoute(McrouterTestCase):
    config_mc = './mcrouter/test/test_logging_route_mc.json'
    config = './mcrouter/test/test_logging_route_server.json'
    extra_args = ['--retain-source-ip', '--enable-logging-route']

    def setUp(self):
        self.mc = self.add_server(Memcached())
        self.mcrouter_mc = self.add_mcrouter(
            self.config_mc,
            extra_args=self.extra_args,
            bg_mcrouter=True)
        self.mcrouter = self.add_mcrouter(
            self.config,
            extra_args=self.extra_args)

    def test_basic(self):
        key = 'foo'
        value = 'value'
        self.mcrouter.set(key, value)
        self.assertEqual(self.mcrouter.get(key), value)
        self.assertEqual(self.mc.get(key), value)

    def test_stderr_logging(self):
        """Confirms that the LOG(INFO) logging works i.e. that the traffic is
           properly passed onto LoggingRoute"""

        magicKey = "MAGIC_KEY_TO_BE_LOGGED"
        value = "12345678"
        self.mcrouter.set(magicKey, value)
        res = self.mcrouter.get(magicKey)
        self.assertEqual(value, res)

        # Read lines from the mcrouter log and filter lines
        # that contain the name of the key
        logLines = open(self.mcrouter.log).readlines()
        linesWithKey = filter(
            lambda line: magicKey in line,
            logLines)

        # Check that we have a store log and a successful get
        self.assertEqual(len(linesWithKey), 2)
        self.assertTrue("mc_res_stored" in linesWithKey[0])
        self.assertTrue("mc_res_found" in linesWithKey[1])
        self.assertTrue("responseLength: 8" in linesWithKey[1])

        # Sanity check that there is an IP
        self.assertTrue("user ip: " in linesWithKey[0])
        self.assertFalse("user ip: N/A" in linesWithKey[0])
