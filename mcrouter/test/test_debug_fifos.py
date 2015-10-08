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

import os
import time

class TestDebugFifos(McrouterTestCase):
    config = './mcrouter/test/mcrouter_test_basic_1_1_1.json'
    extra_args = ['--proxy-threads=1']

    def setUp(self):
        self.add_server(Memcached())
        self.mcrouter = self.add_mcrouter(self.config,
                                          extra_args=self.extra_args)

    def get_fifo(self, substr):
        fifos = os.listdir(self.mcrouter.fifos_dir)
        self.assertEqual(2, len(fifos))

        fifos = [f for f in fifos if substr in f]
        self.assertEqual(1, len(fifos))
        return os.path.join(self.mcrouter.fifos_dir, fifos[0])

    def test_mcgrep_fifo(self):
        key = 'test.abc'
        value = 'abc123'
        self.assertTrue(self.mcrouter.set(key, value))
        self.assertEqual('abc123', self.mcrouter.get(key))

        # Wait mcrouter create the fifos.
        time.sleep(2)

        # Connects to the client and server fifos
        cfd = os.open(self.get_fifo('client'), os.O_RDONLY | os.O_NONBLOCK)
        sfd = os.open(self.get_fifo('server'), os.O_RDONLY | os.O_NONBLOCK)

        # Wait mcrouter detects new fifo connection
        time.sleep(2)

        # Send requests
        self.mcrouter.get(key)

        # Reads client fifo
        buf = os.read(cfd, 4096)
        self.assertTrue(len(buf) > 0)
        self.assertTrue(value in buf.decode('ascii', errors='ignore'))

        # Read server fifo
        buf = os.read(sfd, 4096)
        self.assertTrue(len(buf) > 0)
        self.assertTrue(value in buf.decode('ascii', errors='ignore'))

        os.close(cfd)
        os.close(sfd)
