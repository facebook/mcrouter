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

class TestAsyncFiles(McrouterTestCase):
    stat_prefix = 'libmcrouter.mcrouter.0.'
    config = './mcrouter/test/mcrouter_test_basic_1_1_1.json'
    extra_args = ['--stats-logging-interval', '100', '--use-asynclog-version2']

    def check_stats(self, stats_dir, retries=5, sleep_interval=1):
        file_stat = os.path.join(stats_dir, self.stat_prefix + 'stats')
        file_startup_options = os.path.join(
            stats_dir, self.stat_prefix + 'startup_options')
        file_config_sources = os.path.join(
            stats_dir, self.stat_prefix + 'config_sources_info')

        fileStatExists = False
        fileStartupExists = False
        fileConfigExists = False
        while retries > 0:
            retries -= 1
            if not fileStatExists:
                fileStatExists = os.path.exists(file_stat)
            if not fileStartupExists:
                fileStartupExists = os.path.exists(file_startup_options)
            if not fileConfigExists:
                fileConfigExists = os.path.exists(file_config_sources)
            if fileStatExists and fileStartupExists and fileConfigExists:
                break
            # Sleep between retry intervals
            time.sleep(sleep_interval)

        self.assertTrue(fileStatExists,
                        "{} doesn't exist".format(file_stat))
        self.assertTrue(fileStartupExists,
                        "{} doesn't exist".format(file_startup_options))
        self.assertTrue(fileConfigExists,
                        "{} doesn't exist".format(file_config_sources))

        return (file_stat, file_startup_options, file_config_sources)

    def test_stats_no_requests(self):
        mcrouter = self.add_mcrouter(self.config, extra_args=self.extra_args)
        # check will wait for files
        self.check_stats(mcrouter.stats_dir)

    def test_async_files(self):
        mcrouter = self.add_mcrouter(self.config, extra_args=self.extra_args)
        self.assertIsNone(mcrouter.delete('key'))

        # wait for files
        time.sleep(2)

        # check async spool for failed delete
        asynclog_files = []
        for root, _dirs, files in os.walk(mcrouter.get_async_spool_dir()):
            for f in files:
                asynclog_files.append(os.path.join(root, f))

        self.assertEqual(len(asynclog_files), 1)
        foundPool = False

        # asynclog v2 should have pool name
        with open(asynclog_files[0], 'r') as f:
            for line in f.readlines():
                if 'foo' in line:
                    foundPool = True
                    break

        self.assertTrue(foundPool)

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
