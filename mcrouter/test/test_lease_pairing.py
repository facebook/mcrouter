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

from mcrouter.test.MCProcess import Memcached, McrouterClients
from mcrouter.test.McrouterTestCase import McrouterTestCase

class TestLeasePairing(McrouterTestCase):
    config = './mcrouter/test/test_lease_pairing.json'

    def create_mcrouter(self, extra_args=[]):
        extra_args += ['--proxy-threads', '2']

        self.memcached1 = self.add_server(Memcached())
        self.memcached2 = self.add_server(Memcached())
        self.mcrouter = self.add_mcrouter(self.config,
                                          extra_args=extra_args)
        self.clients = McrouterClients(self.mcrouter.port, 2)

    def test_lease_pairing_enabled(self):
        # The lease-get and it's corresponding lease-set
        # should go to the same server.

        self.create_mcrouter(extra_args=['--enable-lease-pairing'])

        # kill memcached1
        self.memcached1.pause()

        # lease get - should go to memcache2
        get_reply = self.clients[0].leaseGet("key")
        self.assertTrue(get_reply is not None)

        # bring memcached1 up
        self.memcached1.resume()

        # lease set should go to the same server as lease get - memcache2
        set_reply = self.clients[1].leaseSet("key",
                {"value": "abc", "token": get_reply['token']})
        self.assertTrue(set_reply is not None)
        self.assertTrue(self.memcached1.get("key") is None)
        self.assertTrue(self.memcached2.get("key") is not None)

    def test_lease_pairing_disabled(self):
        # The lease-get and it's corresponding lease-set
        # should go to the same server.

        self.create_mcrouter()

        # kill memcached1
        self.memcached1.pause()

        # lease get - should go to memcache2
        get_reply = self.clients[0].leaseGet("key")
        self.assertTrue(get_reply is not None)

        # bring memcached1 up
        self.memcached1.resume()

        # lease set should go to memcache1
        set_reply = self.clients[1].leaseSet("key",
                {"value": "abc", "token": get_reply['token']})
        self.assertTrue(set_reply is not None)
        self.assertTrue(self.memcached1.get("key") is not None)
        self.assertTrue(self.memcached2.get("key") is None)
