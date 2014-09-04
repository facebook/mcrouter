from __future__ import absolute_import
from __future__ import division
from __future__ import print_function
from __future__ import unicode_literals

import time

from mcrouter.test.mock_servers import MockServer
from mcrouter.test.McrouterTestCase import McrouterTestCase

class TkoServer(MockServer):
    def __init__(self, period, phase=0, tmo=0.2, hitcmd='hit'):
        """Simple server stub that alternatively responds to requests
        with or withoud a delay.

        On startup, 'period' - 'phase' requests will be fast initially,
        then next 'period' requests will be slow; and so on.
        """
        super(TkoServer, self).__init__()
        self.period = period
        self.step = phase
        self.tmo = tmo
        self.hitcmd = hitcmd

    def runServer(self, client_socket, client_address):
        while not self.is_stopped():
            cmd = client_socket.recv(1000)
            if not cmd:
                return
            # slow 'period' times in a row, then respond 'period' times in a row
            if self.step % (2 * self.period) >= self.period:
                time.sleep(self.tmo)
            self.step += 1
            if cmd == 'version\r\n':
                client_socket.send('VERSION TKO_SERVER\r\n')
            elif cmd.startswith('get {}\r\n'.format(self.hitcmd)):
                client_socket.send(
                    'VALUE hit 0 %d\r\n%s\r\nEND\r\n' % (
                        len(str(self.port)), str(self.port)))
            elif cmd.startswith('get'):
                client_socket.send('END\r\n')

class FailoverServer(MockServer):
    def runServer(self, client_socket, client_address):
        while not self.is_stopped():
            cmd = client_socket.recv(1000)
            if not cmd:
                return
            if cmd == 'version\r\n':
                client_socket.send('VERSION FAILOVER_SERVER\r\n')
            elif cmd.startswith('get hit'):
                client_socket.send(
                    'VALUE hit 0 %d\r\n%s\r\nEND\r\n' % (
                        len(str(self.port)), str(self.port)))
            elif cmd.startswith('get'):
                client_socket.send('END\r\n')

class TestSlowBoxTko(McrouterTestCase):
    config = './mcrouter/test/test_slow_box_tko.json'
    extra_args = ['-t', '2500',
                  '--timeouts-until-tko', '2',
                  '--latency-window-size', '4',
                  '--latency-threshold-us', '60000',
                  '-r', '140']
    additional_args = ['--global-tko-tracking']

    def setUp(self):
        # Keys that correspond to the various indeces in the A pool.
        # i.e. doing a get for hit_keys[0] will simulate a hit
        # for server 0 (if available)
        self.slow_server = self.add_server(TkoServer(tmo=.12,
                                                     period=4,
                                                     phase=0))
        self.failover_server = self.add_server(FailoverServer())
        self.mcrouter = self.add_mcrouter(
            self.config,
            '/test/A/',
            extra_args=self.extra_args + self.additional_args)

    def test_tko(self):
        # The first request in test seems to be really slow. We push the box
        # through a few reqs just to clean him out, and bring him to the slow
        # period
        for i in range(4):
            self.mcrouter.get('hit')

        # We need 3 requests to bring the box above the threshold. Because of
        # the one request delay 2 more requests are required to TKO
        for i in range(5):
            resp = self.mcrouter.get('hit')
            self.assertEqual(resp, str(self.slow_server.getport()))

        resp = self.mcrouter.get('hit')
        self.assertEqual(resp, str(self.failover_server.getport()))
        time.sleep(0.2)

        # As his latency improves, we should not tko him again in the next 3
        # requests, since the current reqs are all fast
        for i in range(3):
            resp = self.mcrouter.get('hit')
            self.assertEqual(resp, str(self.slow_server.getport()))
