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
import errno
import socket
import threading
import time

class MockServer(threading.Thread):
    def __init__(self, port=0):
        """If no port provided, automatically chooses one.
        Chosen port will be set at self.port,
        after self.port_event is signalled."""

        super(MockServer, self).__init__()
        self.daemon = True
        self.port = port
        self.port_event = threading.Event()
        self.stopped_event = threading.Event()

    def getport(self):
        return self.port

    def ensure_connected(self):
        self.start()
        self.port_event.wait()

    def terminate(self):
        self.stopped_event.set()
        self.join()

    def run(self):
        if socket.has_ipv6:
            self.listen_socket = socket.socket(socket.AF_INET6,
                                               socket.SOCK_STREAM)
        else:
            self.listen_socket = socket.socket(socket.AF_INET,
                                               socket.SOCK_STREAM)
        self.listen_socket.setblocking(0)
        self.listen_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.listen_socket.bind(('', self.port))
        self.listen_socket.listen(5)
        self.port = self.listen_socket.getsockname()[1]
        self.port_event.set()

        while not self.is_stopped():
            try:
                client, address = self.listen_socket.accept()
                client.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                self.runServer(client, address)
                client.close()
            except IOError, e:
                if e.errno == errno.EWOULDBLOCK:
                    time.sleep(0.1)
                else:
                    raise

        self.listen_socket.close()

    def is_stopped(self):
        return self.stopped_event.isSet()

    def wait_until_stopped(self):
        self.stopped_event.wait()


class SleepServer(MockServer):
    """A mock server that listens on a port, but always times out on connections
    """
    def runServer(self, client_socket, client_address):
        self.wait_until_stopped()


class StoreServer(MockServer):
    """A server that responds to requests with 'STORED' after reading expected
    amount of bytes"""
    def __init__(self, expected_key, expected_value):
        super(StoreServer, self).__init__()
        self.expected_bytes = len("set  0 0 \r\n\r\n")
        self.expected_bytes += len(expected_key) + len(expected_value)

    def runServer(self, client_socket, client_address):
        f = client_socket.makefile()
        f.read(self.expected_bytes)
        f.close()
        client_socket.send('STORED\r\n')
