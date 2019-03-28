# Copyright (c) Facebook, Inc. and its affiliates.
#
# This source code is licensed under the MIT license found in the LICENSE
# file in the root directory of this source tree.

from __future__ import absolute_import
from __future__ import division
from __future__ import print_function
from __future__ import unicode_literals
import unittest
import time

from mcrouter.test.MCProcess import Mcrouter, Memcached, MockMemcached


class McrouterTestCase(unittest.TestCase):
    def __init__(self, *args, **kwargs):
        super(McrouterTestCase, self).__init__(*args, **kwargs)
        self.use_mock_mc = False

    def ensureClassVariables(self):
        if 'open_servers' not in self.__dict__:
            self.open_servers = []
        if 'open_ports' not in self.__dict__:
            self.open_ports = []

    def add_server(self, server, logical_port=None):
        self.ensureClassVariables()
        server.ensure_connected()
        self.open_servers.append(server)
        self.open_ports.append(server.getport())

        if logical_port:
            if 'port_map' not in self.__dict__:
                self.port_map = {}
            if logical_port in self.port_map:
                raise Exception("logical_port %d was already used"
                                % logical_port)
            self.port_map[logical_port] = server.getport()

        return server

    def add_mcrouter(self, config, route=None, extra_args=None,
                     replace_map=None, bg_mcrouter=False):
        self.ensureClassVariables()
        substitute_ports = (self.open_ports
                            if 'port_map' not in self.__dict__
                            else self.port_map)

        mcrouter = Mcrouter(config,
                            substitute_config_ports=substitute_ports,
                            default_route=route,
                            extra_args=extra_args,
                            replace_map=replace_map)
        mcrouter.ensure_connected()

        if bg_mcrouter:
            self.open_ports.append(mcrouter.getport())

        if 'open_mcrouters' not in self.__dict__:
            self.open_mcrouters = []
        self.open_mcrouters.append(mcrouter)
        return mcrouter

    def make_memcached(self):
        return MockMemcached() if self.use_mock_mc else Memcached()

    def get_open_ports(self):
        self.ensureClassVariables()
        return self.open_ports

    def tearDown(self):
        # Stop mcrouters first to close connections to servers
        # (some mock severs might be blocked on recv() calls)
        if 'open_mcrouters' in self.__dict__:
            for mcr in self.open_mcrouters:
                mcr.terminate()

        if 'open_servers' in self.__dict__:
            for server in self.open_servers:
                server.terminate()

    def eventually_get(self, key, expVal, timeout=5):
        start_time = time.time()
        interval = 0.5
        while (True):
            if (self.mc.get(key) == expVal):
                return True
            time.sleep(interval)
            now = time.time()
            if (now - start_time > timeout):
                return False
