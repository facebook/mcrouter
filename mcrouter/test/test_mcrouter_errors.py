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

import re

from mcrouter.test.MCProcess import Memcached
from mcrouter.test.McrouterTestCase import McrouterTestCase
from mcrouter.test.mock_servers import ConnectionErrorServer
from mcrouter.test.mock_servers import CustomErrorServer
from mcrouter.test.mock_servers import SleepServer

class TestMcrouterForwardedErrors(McrouterTestCase):
    config = './mcrouter/test/mcrouter_test_basic_1_1_1.json'
    get_cmd = 'get test_key\r\n'
    set_cmd = 'set test_key 0 0 3\r\nabc\r\n'
    delete_cmd = 'delete test_key\r\n'
    server_errors = [
            'SERVER_ERROR out of order',
            'SERVER_ERROR timeout',
            'SERVER_ERROR connection timeout',
            'SERVER_ERROR connection error',
            'SERVER_ERROR 307 busy',
            'SERVER_ERROR 302 try again',
            'SERVER_ERROR unavailable',
            'SERVER_ERROR bad value',
            'SERVER_ERROR aborted',
            'SERVER_ERROR local error',
            'SERVER_ERROR remote error',
            'SERVER_ERROR waiting'
            ]
    client_errors = [
            'CLIENT_ERROR bad command',
            'CLIENT_ERROR bad key',
            'CLIENT_ERROR bad flags',
            'CLIENT_ERROR bad exptime',
            'CLIENT_ERROR bad lease_id',
            'CLIENT_ERROR bad cas_id',
            'CLIENT_ERROR malformed request',
            'CLIENT_ERROR out of memory'
            ]

    def setUp(self):
        self.server = self.add_server(CustomErrorServer())

    # server returned: SERVER_ERROR
    def test_server_replied_server_error_for_set(self):
        cmd = self.set_cmd
        self.server.setExpectedBytes(len(cmd))
        for error in self.server_errors:
            self.server.setError(error)
            mcrouter = self.add_mcrouter(self.config)
            res = mcrouter.issue_command(cmd)
            self.assertEqual(error + '\r\n', res)

    def test_server_replied_server_error_for_get(self):
        cmd = self.get_cmd
        self.server.setExpectedBytes(len(cmd))
        for error in self.server_errors:
            self.server.setError(error)
            mcrouter = self.add_mcrouter(self.config)
            res = mcrouter.issue_command(cmd)
            self.assertEqual('END\r\n', res)

    def test_server_replied_server_error_for_get_with_no_miss_on_error(self):
        # With --disable-miss-on-get-errors, errors should be forwarded
        # to client
        cmd = self.get_cmd
        self.server.setExpectedBytes(len(cmd))
        for error in self.server_errors:
            self.server.setError(error)
            mcrouter = self.add_mcrouter(self.config,
                    extra_args=['--disable-miss-on-get-errors'])
            res = mcrouter.issue_command(cmd)
            self.assertEqual(error + '\r\n', res)

    def test_server_replied_server_error_for_delete(self):
        cmd = self.delete_cmd
        self.server.setExpectedBytes(len(cmd))
        for error in self.server_errors:
            self.server.setError(error)
            mcrouter = self.add_mcrouter(self.config)
            res = mcrouter.issue_command(cmd)
            self.assertEqual('NOT_FOUND\r\n', res)

    def test_server_replied_server_error_for_delete_with_no_asynclog(self):
        # With --asynclog-disable, errors should be forwarded to client
        cmd = self.delete_cmd
        self.server.setExpectedBytes(len(cmd))
        for error in self.server_errors:
            self.server.setError(error)
            mcrouter = self.add_mcrouter(self.config,
                    extra_args=['--asynclog-disable'])
            res = mcrouter.issue_command(cmd)
            self.assertEqual(error + '\r\n', res)

    # server returned: CLIENT_ERROR
    def test_server_replied_client_error_for_set(self):
        cmd = self.set_cmd
        self.server.setExpectedBytes(len(cmd))
        for error in self.client_errors:
            self.server.setError(error)
            mcrouter = self.add_mcrouter(self.config)
            res = mcrouter.issue_command(cmd)
            self.assertEqual(error + '\r\n', res)

    def test_server_replied_client_error_for_get(self):
        cmd = self.get_cmd
        self.server.setExpectedBytes(len(cmd))
        for error in self.client_errors:
            self.server.setError(error)
            mcrouter = self.add_mcrouter(self.config)
            res = mcrouter.issue_command(cmd)
            self.assertEqual('END\r\n', res)

    def test_server_replied_client_error_for_get_with_no_miss_on_error(self):
        # With --disable-miss-on-get-errors, errors should be forwarded
        # to client
        cmd = self.get_cmd
        self.server.setExpectedBytes(len(cmd))
        for error in self.client_errors:
            self.server.setError(error)
            mcrouter = self.add_mcrouter(self.config,
                    extra_args=['--disable-miss-on-get-errors'])
            res = mcrouter.issue_command(cmd)
            self.assertEqual(error + '\r\n', res)

    def test_server_replied_client_error_for_delete(self):
        cmd = self.delete_cmd
        self.server.setExpectedBytes(len(cmd))
        for error in self.client_errors:
            self.server.setError(error)
            mcrouter = self.add_mcrouter(self.config)
            res = mcrouter.issue_command(cmd)
            self.assertEqual(error + '\r\n', res)

    def test_early_server_reply(self):
        value_len = 1024 * 1024 * 4
        value = 'a' * value_len
        cmd_header = 'set test:key 0 0 {}\r\n'.format(value_len)
        cmd = cmd_header + value + '\r\n'
        error = 'SERVER_ERROR out of memory'
        self.server.setError(error)
        # reply before reading the value part of request
        self.server.setExpectedBytes(len(cmd), len(cmd_header))
        # sleep for one second to ensure that there's a gap between
        # request write and reply read events in mcrouter
        self.server.setSleepAfterReply(1)
        mcrouter = self.add_mcrouter(self.config,
                                     extra_args=['--server-timeout', '2000'])
        res = mcrouter.issue_command(cmd)
        self.assertEqual(error + '\r\n', res)


class TestMcrouterGeneratedErrors(McrouterTestCase):
    config = './mcrouter/test/mcrouter_test_basic_1_1_1.json'
    get_cmd = 'get test_key\r\n'
    set_cmd = 'set test_key 0 0 3\r\nabc\r\n'
    delete_cmd = 'delete test_key\r\n'

    def getMcrouter(self, server, args=[]):
        self.add_server(server)
        return self.add_mcrouter(self.config, extra_args=args)

    # mcrouter generated: timeout
    def test_timeout_set(self):
        mcrouter = self.getMcrouter(SleepServer())
        res = mcrouter.issue_command(self.set_cmd)
        self.assertEqual('SERVER_ERROR timeout\r\n', res)

    def test_timeout_get(self):
        mcrouter = self.getMcrouter(SleepServer())
        res = mcrouter.issue_command(self.get_cmd)
        self.assertEqual('END\r\n', res)

    def test_timeout_delete(self):
        mcrouter = self.getMcrouter(SleepServer())
        res = mcrouter.issue_command(self.delete_cmd)
        self.assertEqual('NOT_FOUND\r\n', res)

    # mcrouter generated: connection error
    def test_connection_error_set(self):
        mcrouter = self.getMcrouter(ConnectionErrorServer())
        res = mcrouter.issue_command(self.set_cmd)
        self.assertTrue(re.match('SERVER_ERROR (connection|remote) error', res))

    def test_connection_error_get(self):
        mcrouter = self.getMcrouter(ConnectionErrorServer())
        res = mcrouter.issue_command(self.get_cmd)
        self.assertEqual('END\r\n', res)

    def test_connection_error_delete(self):
        mcrouter = self.getMcrouter(ConnectionErrorServer())
        res = mcrouter.issue_command(self.delete_cmd)
        self.assertEqual('NOT_FOUND\r\n', res)

    # mcrouter generated: TKO
    def test_tko_set(self):
        mcrouter = self.getMcrouter(SleepServer(),
                args=['--timeouts-until-tko', '1'])
        res = mcrouter.issue_command(self.set_cmd)
        res = mcrouter.issue_command(self.set_cmd)
        self.assertEqual('SERVER_ERROR unavailable\r\n', res)

    def test_tko_get(self):
        mcrouter = self.getMcrouter(SleepServer(),
                args=['--timeouts-until-tko', '1'])
        res = mcrouter.issue_command(self.set_cmd)
        res = mcrouter.issue_command(self.get_cmd)
        self.assertEqual('END\r\n', res)

    def test_tko_delete(self):
        mcrouter = self.getMcrouter(SleepServer(),
                args=['--timeouts-until-tko', '1'])
        res = mcrouter.issue_command(self.set_cmd)
        res = mcrouter.issue_command(self.delete_cmd)
        self.assertEqual('NOT_FOUND\r\n', res)

    # mcrouter generated: bad key
    def test_bad_key_set(self):
        mcrouter = self.getMcrouter(Memcached())
        cmd = 'set test.key' + ('x' * 10000) + ' 0 0 3\r\nabc\r\n'
        res = mcrouter.issue_command(cmd)
        self.assertEqual('CLIENT_ERROR bad key\r\n', res)

    def test_bad_key_get(self):
        mcrouter = self.getMcrouter(Memcached())
        cmd = 'get test.key' + ('x' * 10000) + '\r\n'
        res = mcrouter.issue_command(cmd)
        self.assertEqual('CLIENT_ERROR bad key\r\n', res)

    def test_bad_key_delete(self):
        mcrouter = self.getMcrouter(Memcached())
        cmd = 'delete test.key' + ('x' * 10000) + '\r\n'
        res = mcrouter.issue_command(cmd)
        self.assertEqual('CLIENT_ERROR bad key\r\n', res)

    # mcrouter generated: remote error
    def test_remote_error_command_not_supported(self):
        mcrouter = self.getMcrouter(Memcached())
        cmd = 'flush_all\r\n'
        res = mcrouter.issue_command(cmd)
        self.assertEqual('SERVER_ERROR Command disabled\r\n', res)
