# Copyright (c) 2016, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

from __future__ import absolute_import
from __future__ import division
from __future__ import print_function
from __future__ import unicode_literals

class McrouterGlobals:
    @staticmethod
    def binPath(name):
        bins = {
            'mcrouter': './mcrouter/mcrouter',
            'mcpiper': './mcrouter/tools/mcpiper/mcpiper',
            'mockmc': './mcrouter/lib/network/mock_mc_server',
            'prodmc': './mcrouter/lib/network/mock_mc_server',
        }
        return bins[name]

    @staticmethod
    def preprocessArgs(args):
        return args
