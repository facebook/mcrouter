# Copyright (c) Facebook, Inc. and its affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

from __future__ import absolute_import
from __future__ import division
from __future__ import print_function
from __future__ import unicode_literals

import json
import os
import time

from mcrouter.test.McrouterTestCase import McrouterTestCase


class TestAsyncFilesAttr(McrouterTestCase):
    stat_prefix = 'libmcrouter.mcrouter.0.'
    config = './mcrouter/test/mcrouter_test_basic_1_1_1.json'
    extra_args = ['--stats-logging-interval', '100', '--use-asynclog-version2']

    def check_stats(self, stats_dir):
        file_stat = os.path.join(stats_dir, self.stat_prefix + 'stats')
        file_startup_options = os.path.join(
            stats_dir, self.stat_prefix + 'startup_options')
        file_config_sources = os.path.join(
            stats_dir, self.stat_prefix + 'config_sources_info')

        self.assertTrue(os.path.exists(file_stat),
                        "{} doesn't exist".format(file_stat))
        self.assertTrue(os.path.exists(file_startup_options),
                        "{} doesn't exist".format(file_startup_options))
        self.assertTrue(os.path.exists(file_config_sources),
                        "{} doesn't exist".format(file_config_sources))

        return (file_stat, file_startup_options, file_config_sources)

    def test_stats_no_requests(self):
        mcrouter = self.add_mcrouter(self.config, extra_args=self.extra_args)
        # wait for files
        time.sleep(3)
        self.check_stats(mcrouter.stats_dir)

    def test_async_files_attr(self):
        mcrouter = self.add_mcrouter(self.config, extra_args=self.extra_args)
        binary = "buck-out/gen/mcrouter/lib/network/gen/carbon-Memcache-client "
        port = str(mcrouter.getport())
        args = "'{\"key\":\"abcd\", \"attributes\":{\"a1\":1000, \"a2\":2000}}'"
        command = binary + "-p " + port + " delete " + args

        os.system(command)

        # wait for files
        time.sleep(2)

        # check async spool for failed delete
        asynclog_files = []
        for root, _dirs, files in os.walk(mcrouter.get_async_spool_dir()):
            for f in files:
                asynclog_files.append(os.path.join(root, f))

        self.assertEqual(len(asynclog_files), 1)

        # asynclog v2 should have attributes
        with open(asynclog_files[0], 'r') as f:
            file_json = json.load(f)
            self.assertEqual(file_json[3]["a"]["a1"], 1000)
            self.assertEqual(file_json[3]["a"]["a2"], 2000)

        # check stats
        (file_stat, file_startup_options, file_config_sources) = \
            self.check_stats(mcrouter.stats_dir)

        with open(file_stat) as f:
            stat_json = json.load(f)
            self.assertGreaterEqual(stat_json[self.stat_prefix + 'uptime'], 0)

        with open(file_startup_options) as f:
            startup_json = json.load(f)
            self.assertEqual(startup_json['default_route'], '/././')

        with open(file_config_sources) as f:
            sources_json = json.load(f)
            self.assertEqual(sources_json['mcrouter_config'],
                             '6bbe796e576bf700f5e3ba8a66d409d5')

        # check stats are up-to-date
        now = time.time()
        time.sleep(2)

        self.assertGreater(os.path.getmtime(file_stat), now)
        self.assertGreater(os.path.getmtime(file_startup_options), now)
        self.assertGreater(os.path.getmtime(file_config_sources), now)
