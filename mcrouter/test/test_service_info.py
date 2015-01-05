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

class TestServiceInfo(McrouterTestCase):
    config = './mcrouter/test/test_service_info.json'
    extra_args = []

    def get_mcrouter(self):
        return self.add_mcrouter(self.config, extra_args=self.extra_args)

    def test_route_format(self):
        mc1 = self.add_server(Memcached())
        mc2 = self.add_server(Memcached())
        ports = [mc1.port, mc2.port]
        mcrouter = self.get_mcrouter()
        route = mcrouter.get("__mcrouter__.route(set,a)")
        parts = route.split("\r\n")
        self.assertEqual(len(parts), 2)
        for i, part in enumerate(parts):
            host, port = part.split(":")
            self.assertEqual(host, "127.0.0.1")
            self.assertEqual(port, str(ports[i]))

    def test_hostid(self):
        mcrouter = self.get_mcrouter()
        hostid = mcrouter.get("__mcrouter__.hostid")
        self.assertEqual(str(int(hostid)), hostid)
        self.assertEqual(hostid, mcrouter.get("__mcrouter__.hostid"))
