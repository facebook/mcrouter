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

from mcrouter.test.McrouterTestCase import McrouterTestCase
from mcrouter.test.mock_servers import CustomErrorServer
from mcrouter.test.mock_servers import MockServer

class TestAsciiError(McrouterTestCase):
    config = './mcrouter/test/mcrouter_test_basic_1_1_1.json'
    extra_args = []

    def setUp(self):
        self.add_server(CustomErrorServer(len('test_error'), 'ERROR\r\nEND'))
        self.mcrouter = self.add_mcrouter(
            self.config,
            extra_args=self.extra_args)

    def test_ascii_error(self):
        resp = self.mcrouter.get('test_error')
        self.assertEqual(None, resp)

class ExtraValueServer(MockServer):
    """A server that responds normally to probes.
       But for get request it will respond with a proper reply plus some
       additional reply"""
    def __init__(self, expected_key):
        super(ExtraValueServer, self).__init__()
        self.expected_key = expected_key
        self.expected_bytes = len('get ' + expected_key + '\r\n')

    def runServer(self, client_socket, client_address):
        f = client_socket.makefile()
        # Read first char to see if it's a probe or a get request.
        char = f.read(1)
        if char == 'v':
            # Read the remaing part of version request.
            f.read(len('ERSION\r\n'))
            client_socket.send('VERSION test\r\n')
            # Read the first char of get request.
            f.read(1)
        # Read remaining part of get request.
        f.read(self.expected_bytes - 1)
        f.close()
        client_socket.send('VALUE ' + self.expected_key +
                           ' 0 9\r\ntestValue\r\nEND\r\n')
        client_socket.send('VALUE test2 0 1\r\nV\r\nEND\r\n')

class TestAsciiExtraData(McrouterTestCase):
    config = './mcrouter/test/mcrouter_test_basic_1_1_1.json'
    extra_args = ['--probe-timeout-initial', '100']

    def setUp(self):
        self.add_server(ExtraValueServer('test'))
        self.mcrouter = self.add_mcrouter(
            self.config,
            extra_args=self.extra_args)

    def test_ascii_extra_data(self):
        # Send initial get.
        resp = self.mcrouter.get('test')
        self.assertEqual('testValue', resp)
        # Send another get, that will fail because of TKO.
        resp = self.mcrouter.get('test')
        self.assertEqual(None, resp)
        # Allow mcrouter some time to recover.
        time.sleep(1)
        # Send another get request that should succeed.
        resp = self.mcrouter.get('test')
        self.assertEqual('testValue', resp)
