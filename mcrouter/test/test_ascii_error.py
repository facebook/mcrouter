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
from mcrouter.test.mock_servers import MockServer

class ErrorServer(MockServer):
    """A server that responds with ERROR after reading expected amount of
    bytes"""
    def __init__(self, expected_key):
        super(ErrorServer, self).__init__()
        self.expected_bytes = len("get \r\n") + len(expected_key)

    def runServer(self, client_socket, client_address):
        f = client_socket.makefile()
        f.read(self.expected_bytes)
        f.close()
        client_socket.send('ERROR\r\nEND')


class TestAsciiError(McrouterTestCase):
    config = './mcrouter/test/mcrouter_test_basic_1_1_1.json'
    extra_args = []

    def setUp(self):
        self.add_server(ErrorServer('test_error'))
        self.mcrouter = self.add_mcrouter(
            self.config,
            extra_args=self.extra_args)

    def test_ascii_error(self):
        resp = self.mcrouter.get('test_error')
        self.assertEqual(None, resp)
