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

import os
import signal
import time
import socket

from mcrouter.test.McrouterTestCase import McrouterTestCase

class TestMcrouterManagedMode(McrouterTestCase):
    config = './mcrouter/test/mcrouter_test_basic_1_1_1.json'
    extra_args = ['-m']

    def setUp(self):
        self.mcrouter = self.add_mcrouter(self.config,
            extra_args=self.extra_args)

    def killMainProcess(self, sig=signal.SIGTERM):
        proc = self.mcrouter.getprocess()
        proc.send_signal(sig)
        proc.wait()

    def get_child_pid(self):
        detailedStats = {}
        try:
            detailedStats = self.mcrouter.stats('detailed')
        except socket.error:
            return -1

        if 'child_pid' in detailedStats:
            return int(detailedStats['child_pid'])
        return -1

    def killChildProcess(self, sig=signal.SIGTERM):
        pid = self.get_child_pid()
        if pid > 0:
            try:
                os.kill(pid, sig)
                return True
            except OSError:
                return False
        return False

    def test_sigkill_parent(self):
        self.killMainProcess(signal.SIGKILL)
        self.assertFalse(self.mcrouter.is_alive())

    def test_sigterm_parent(self):
        self.killMainProcess(signal.SIGTERM)
        self.assertFalse(self.mcrouter.is_alive())

    def test_sigint_parent(self):
        self.killMainProcess(signal.SIGINT)
        self.assertFalse(self.mcrouter.is_alive())

    def test_sigquit_parent(self):
        self.killMainProcess(signal.SIGQUIT)
        self.assertFalse(self.mcrouter.is_alive())

    def test_sigkill_child(self):
        self.killChildProcess(signal.SIGKILL)
        self.assertTrue(self.mcrouter.is_alive())
        time.sleep(1)
        self.assertTrue(self.mcrouter.is_alive())

    def test_sigterm_child(self):
        self.killChildProcess(signal.SIGTERM)
        self.assertTrue(self.mcrouter.is_alive())
        time.sleep(1)
        self.assertTrue(self.mcrouter.is_alive())

    def test_sigint_child(self):
        self.killChildProcess(signal.SIGINT)
        self.assertTrue(self.mcrouter.is_alive())
        time.sleep(1)
        self.assertTrue(self.mcrouter.is_alive())

    def test_sigquit_child(self):
        self.killChildProcess(signal.SIGQUIT)
        self.assertTrue(self.mcrouter.is_alive())
        time.sleep(1)
        self.assertTrue(self.mcrouter.is_alive())

    def test_shutdown(self):
        self.mcrouter.shutdown()
        time.sleep(2)
        self.assertFalse(self.mcrouter.is_alive())
