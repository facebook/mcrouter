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

from mcrouter.test.MCProcess import Mcrouter
from mcrouter.test.MCProcess import Memcached
from mcrouter.test.McrouterTestCase import McrouterTestCase

class TestShadowWithFile(McrouterTestCase):
    config = './mcrouter/test/test_shadow_with_file.json'
    extra_args = []

    def setUp(self):
        self.mc1 = self.add_server(Memcached(port=11710))
        self.mc2 = self.add_server(Memcached(port=11711))
        self.mc_shadow = self.add_server(Memcached(port=11712))

    def get_mcrouter(self):
        return self.add_server(Mcrouter(self.config,
                                        extra_args=self.extra_args))

    def test_shadow_with_file(self):
        mcr = self.get_mcrouter()
        # SpookyHashV2 will choose these values for 0.0 .. 0.1:
        shadow_list = [5, 7, 13, 33, 43, 46, 58, 71, 83, 85, 89, 91, 93]
        for i in range(100):
            key = 'f' + str(i)
            value = 'value' + str(i)
            mcr.set(key, value)
            self.assertTrue(self.mc1.get(key) == value or
                            self.mc2.get(key) == value)
            if i in shadow_list:
                self.assertEqual(self.mc_shadow.get(key), value)
            else:
                self.assertEqual(self.mc_shadow.get(key), None)
