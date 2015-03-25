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

import socket
from subprocess import Popen
import time
import unittest

from mcrouter.test.config import McrouterGlobals

# Test validate-config parameter
class TestValidateConfig(unittest.TestCase):
    valid_config = 'mcrouter/test/test_ascii.json'
    invalid_config = 'mcrouter/test/invalid_config.json'
    extra_args = []

    def try_mcrouter(self, config):
        listen_sock = socket.socket()
        listen_sock.listen(100)
        cmd = McrouterGlobals.preprocessArgs([
            McrouterGlobals.InstallDir + '/mcrouter/mcrouter',
            '-L', '/tmp/test.log',
            '-f', config,
            '--listen-sock-fd', str(listen_sock.fileno()),
            '--stats-logging-interval', '0',
            '--validate-config'
        ] + self.extra_args)
        proc = Popen(cmd)
        for i in range(50):
            if proc.poll() is not None:
                break
            time.sleep(0.1)

        ret = proc.returncode

        if proc is not None and proc.poll() is None:
            proc.terminate()
        listen_sock.close()

        return ret

    def test_valid_config(self):
        ret = self.try_mcrouter(self.valid_config)
        self.assertEqual(ret, 0)

    def test_invalid_config(self):
        ret = self.try_mcrouter(self.invalid_config)
        self.assertNotEqual(ret, 0)
        self.assertNotEqual(ret, None)
