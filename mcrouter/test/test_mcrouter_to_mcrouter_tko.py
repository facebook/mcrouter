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

from mcrouter.test.McrouterTestCase import McrouterTestCase

class TestMcrouterToMcrouterTko(McrouterTestCase):
    config = './mcrouter/test/test_mcrouter_to_mcrouter_tko.json'
    extra_args = ['--timeouts-until-tko', '1', '--group-remote-errors']

    def setUp(self):
        self.underlying_mcr = self.add_mcrouter(self.config,
                extra_args=self.extra_args, bg_mcrouter=True)

    def get_mcrouter(self):
        return self.add_mcrouter(self.config, extra_args=self.extra_args)

    def test_underlying_tko(self):
        mcr = self.get_mcrouter()

        self.assertFalse(mcr.delete("key"))

        stats = self.underlying_mcr.stats("suspect_servers")
        self.assertEqual(1, len(stats))
        self.assertTrue(re.match("status:(tko|down)", stats.values()[0]))

        stats = mcr.stats("suspect_servers")
        self.assertEqual(0, len(stats))
