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

class TestConstShardHash(McrouterTestCase):
    config = './mcrouter/test/test_config_params.json'

    def test_config_params(self):
        mc = self.add_server(Memcached())
        self.port_map = {}
        extra_args = ['--config-params', 'PORT:{},POOL:A'.format(mc.getport())]
        mcrouter = self.add_mcrouter(self.config, extra_args=extra_args)

        self.assertTrue(mcrouter.set('key', 'value'))
        self.assertEqual(mcrouter.get('key'), 'value')
