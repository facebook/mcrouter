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

# Test basic functionality of the dispatcher that can listen on
# multiple ports. Connect to all the ports a few times and make
# sure basic communication is ok
class TestMultiplePorts(unittest.TestCase):
    def test_multiple_ports(self):
        first_port = 11108
        num_ports = 3
        num_passes = 100
        ports = range(first_port, first_port + num_ports)
        cmd = McrouterGlobals.preprocessArgs([
            McrouterGlobals.InstallDir + '/mcrouter/mcrouter',
            '-L', '/tmp/test.log',
            '-f', 'mcrouter/test/test_ascii.json',
            '-p', ','.join(map(str, ports)),
            '--proxy-threads', '2',
            '--stats-logging-interval', '0'
        ])
        proc = Popen(cmd)
        time.sleep(1)  # magic
        for i in range(num_passes):
            for port in ports:
                sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                address = ('localhost', port)
                sock.connect(address)
                fd = sock.makefile()
                sock.send("get multiple-ports-junk-key\r\n")
                self.assertTrue(fd.readline().strip() == "END")
                sock.close()

        proc.terminate()
