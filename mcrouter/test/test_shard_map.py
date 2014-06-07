# Copyright (c) 2014, Facebook, Inc.
#  All rights reserved.
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

class EchoServer(MockServer):
    """A server that responds to get requests with its port number.
    """

    def runServer(self, client_socket, client_address):
        while not self.is_stopped():
            cmd = client_socket.recv(1000)
            if not cmd:
                return
            if cmd.startswith('get'):
                client_socket.send('VALUE hit 0 %d\r\n%s\r\nEND\r\n' %
                                   (len(str(self.port)), str(self.port)))

class TestShardMap(McrouterTestCase):
    config = './mcrouter/test/test_shard_map.json'
    extra_args = []

    def setUp(self):
        for i in xrange(7):
            self.add_server(EchoServer())

        self.mcrouter = self.add_mcrouter(
            self.config,
            '/test/A/',
            extra_args=self.extra_args)

    def test_shard_map(self):
        # Test map entries
        resp = self.mcrouter.get('hit:8000:blah')
        self.assertEquals(resp, str(self.get_open_ports()[0]))
        resp = self.mcrouter.get('hit:8001:blah')
        self.assertEquals(resp, str(self.get_open_ports()[1]))
        resp = self.mcrouter.get('hit:8002:blah')
        self.assertEquals(resp, str(self.get_open_ports()[2]))
        resp = self.mcrouter.get('hit:8003:blah')
        self.assertEquals(resp, str(self.get_open_ports()[3]))
        resp = self.mcrouter.get('hit:8004:blah')
        self.assertEquals(resp, str(self.get_open_ports()[4]))

        # Test stuff not in the map is going somewhere (using ch3)
        resp = self.mcrouter.get('hit:8005:blah')
        self.assertTrue(int(resp) in self.get_open_ports())
        resp = self.mcrouter.get('hit:8006:blah')
        self.assertTrue(int(resp) in self.get_open_ports())
        resp = self.mcrouter.get('hit:8007:blah')
        self.assertTrue(int(resp) in self.get_open_ports())
