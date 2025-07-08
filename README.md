# Mcrouter [![Build Status](https://github.com/facebook/mcrouter/actions/workflows/getdeps_linux.yml/badge.svg)](https://github.com/facebook/mcrouter/actions/workflows/getdeps_linux.yml)
[![Support Ukraine](https://img.shields.io/badge/Support-Ukraine-FFD500?style=flat&labelColor=005BBB)](https://opensource.fb.com/support-ukraine) [![License](https://img.shields.io/badge/license-MIT-blue)](https://github.com/facebook/mcrouter/blob/master/LICENSE)

Mcrouter (pronounced mc router) is a memcached protocol router for scaling [memcached](https://memcached.org/)
deployments. It's a core component of cache
infrastructure at Facebook and Instagram where mcrouter handles almost
5 billion requests per second at peak.

Mcrouter is developed and maintained by Facebook.

See https://github.com/facebook/mcrouter/wiki to get started.

## Quick start guide

### Building

Mcrouter depends on [folly](https://github.com/facebook/folly), [wangle](https://github.com/facebook/wangle), [fizz](https://github.com/facebookincubator/fizz), [fbthrift](https://github.com/facebook/fbthrift)
and [mvfst](https://github.com/facebook/mvfst).

The `getdeps.py` tool, shared between many open source C++ projects at Meta, may be used to build mcrouter and its dependencies:

    # Clone the repo
    git clone https://github.com/facebook/mcrouter.git
    cd mcrouter

    # Build, using system dependencies if available, including tests
    ./build/fbcode_builder/getdeps.py --allow-system-packages build mcrouter

    # Build, using system dependencies if available, without tests
    ./build/fbcode_builder/getdeps.py --allow-system-packages --no-tests build mcrouter

Once built, you may use `getdeps.py` to run the tests as well:

    # Run the tests
    ./build/fbcode_builder/getdeps.py --allow-system-packages test

### Packaging
mcrouter ships with a basic `CPack` configuration that may be used to package it for your Linux distribution.
After building the project, change to its `getdeps.py` scratch dir, and run `cpack` with the appropriate generator.

### Running mcrouter

Assuming you have a memcached instance on the local host running on port 5001,
the simplest mcrouter setup is:

    $ mcrouter \
        --config-str='{"pools":{"A":{"servers":["127.0.0.1:5001"]}},
                      "route":"PoolRoute|A"}' \
        -p 5000
    $ echo -ne "get key\r\n" | nc 0 5000

(nc is the GNU Netcat, http://netcat.sourceforge.net/)

## Features

+ Memcached ASCII protocol
+ Connection pooling
+ Multiple hashing schemes
+ Prefix routing
+ Replicated pools
+ Production traffic shadowing
+ Online reconfiguration
+ Flexible routing
+ Destination health monitoring/automatic failover
+ Cold cache warm up
+ Broadcast operations
+ Reliable delete stream
+ Multi-cluster support
+ Rich stats and debug commands
+ Quality of service
+ Large values
+ Multi-level caches
+ IPv6 support
+ SSL support

## Links

Documentation: https://github.com/facebook/mcrouter/wiki
Engineering discussions and support: https://www.facebook.com/groups/mcrouter

## License

Copyright (c) Facebook, Inc. and its affiliates.

Licensed under the MIT license:
https://github.com/facebook/mcrouter/blob/master/LICENSE
