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
            except IOError as e:
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

class TkoServer(MockServer):
    def __init__(self, period, phase=0, tmo=0.5, hitcmd='hit'):
        """Simple server stub that alternatively responds to requests
        with or withoud a delay.

        On startup, 'period' - 'phase' requests will be fast initially,
        then next 'period' requests will be slow; and so on.
        Always responds to 'version' requests without changing state.
        """
        super(TkoServer, self).__init__()
        self.period = period
        self.step = phase
        self.tmo = tmo
        self.hitcmd = hitcmd

    def runServer(self, client_socket, client_address):
        while not self.is_stopped():
            f = client_socket.makefile()
            cmd = f.readline()
            f.close()
            if not cmd:
                continue
            if cmd == 'version\r\n':
                client_socket.send('VERSION TKO_SERVER\r\n')
                continue
            # fast 'period' times in a row, then slow 'period' times in a row
            if self.step % (2 * self.period) >= self.period:
                time.sleep(self.tmo)
            self.step += 1
            if cmd.startswith('get {}\r\n'.format(self.hitcmd)):
                client_socket.send(
                    'VALUE hit 0 %d\r\n%s\r\nEND\r\n' % (
                        len(str(self.port)), str(self.port)))
            elif cmd.startswith('get'):
                client_socket.send('END\r\n')
