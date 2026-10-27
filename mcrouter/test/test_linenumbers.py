#!/usr/bin/env python3
# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

# pyre-unsafe

import select
import signal
import subprocess
import sys
import tempfile
import unittest

from mcrouter.test.config import McrouterGlobals


class OutputCheckerTestCase(unittest.TestCase):
    cleanup_timeout = 5
    config_str = """{
          "route": {
            "type": "XYZRoute",
            "pool": {
              "name" : "A",
              "servers": [
                "localhost:5001",
                "localhost:5002",
                "localhost:5003"
              ],
            },
          }
        }
    """

    def setUp(self):
        self.proc = None
        (_, config) = tempfile.mkstemp("")
        with open(config, "w") as config_file:
            config_file.write(self.config_str)
        self.config = config

    def tearDown(self):
        try:
            if self.proc:
                try:
                    self.proc.terminate()
                except OSError:
                    pass
                # SIGTERM can produce a stack dump larger than the stderr pipe.
                try:
                    self.proc.communicate(timeout=self.cleanup_timeout)
                except subprocess.TimeoutExpired:
                    self.proc.kill()
                    self.proc.communicate(timeout=self.cleanup_timeout)
        finally:
            signal.alarm(0)

    def spawn(self, cmd):
        self.proc = subprocess.Popen(cmd, stderr=subprocess.PIPE)

    def check_for_error_line_number(self):
        for line in self.proc.stderr:
            line = line.decode()
            if "Unknown RouteHandle: XYZRoute line: 3" in line:
                return True
        return False


class OutputCheckerCleanupTest(unittest.TestCase):
    def check_cleanup(self, script, expected_returncode, cleanup_timeout=5):
        checker = OutputCheckerTestCase()
        checker.cleanup_timeout = cleanup_timeout
        checker.spawn([sys.executable, "-c", script])
        proc = checker.proc
        previous_handler = signal.getsignal(signal.SIGALRM)

        def timeout_handler(signum, frame):
            raise TimeoutError("Subprocess cleanup exceeded the test deadline")

        try:
            signal.signal(signal.SIGALRM, timeout_handler)
            signal.alarm(10)
            ready, _, _ = select.select([proc.stderr], [], [], 5)
            self.assertTrue(ready, "Child did not register its signal handler")
            self.assertEqual(proc.stderr.readline(), b"ready\n")
            checker.tearDown()
            self.assertEqual(proc.returncode, expected_returncode)
        finally:
            signal.alarm(0)
            signal.signal(signal.SIGALRM, previous_handler)
            if proc.poll() is None:
                proc.kill()
            proc.communicate(timeout=5)

    def test_cleanup_drains_sigterm_output(self):
        self.check_cleanup(
            """
import os
import signal
import sys


def terminate(signum, frame):
    remaining = 1024 * 1024
    while remaining:
        remaining -= os.write(2, b"x" * min(remaining, 65536))
    sys.exit(0)


signal.signal(signal.SIGTERM, terminate)
os.write(2, b"ready\\n")
while True:
    signal.pause()
""",
            0,
        )

    def test_cleanup_kills_child_ignoring_sigterm(self):
        self.check_cleanup(
            """
import os
import signal

signal.signal(signal.SIGTERM, signal.SIG_IGN)
os.write(2, b"ready\\n")
while True:
    signal.pause()
""",
            -signal.SIGKILL,
            cleanup_timeout=0.1,
        )

    def test_cleanup_reaps_already_exited_child(self):
        checker = OutputCheckerTestCase()
        checker.spawn([sys.executable, "-c", "pass"])
        checker.proc.wait(timeout=5)
        checker.tearDown()
        self.assertEqual(checker.proc.returncode, 0)


class TestLineNumbers(OutputCheckerTestCase):
    def test_linenumbers(self):
        args = McrouterGlobals.preprocessArgs(
            [
                McrouterGlobals.binPath("mcrouter"),
                "--config",
                "file:" + self.config,
                "-p",
                str(5721),
            ]
        )

        self.spawn(args)
        self.assertTrue(self.check_for_error_line_number())
