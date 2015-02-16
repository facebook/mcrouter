# Mcrouter [![Build Status](https://travis-ci.org/facebook/mcrouter.svg?branch=master)](https://travis-ci.org/facebook/mcrouter)

Mcrouter is a memcached protocol router for scaling memcached
(http://memcached.org/) deployments. It's a core component of cache
infrastructure at Facebook and Instagram where mcrouter handles almost
5 billion requests per second at peak.

Mcrouter is developed and maintained by Facebook.

See https://github.com/facebook/mcrouter/wiki to get started.

## Quick start guide

See https://github.com/facebook/mcrouter/wiki/mcrouter-installation for more
detailed installation instructions.

Mcrouter depends on folly (https://github.com/facebook/folly).

The installation is a standard autotools flow:

    $ autoreconf --install
    $ ./configure
    $ make
    $ sudo make install
    $ mcrouter --help

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

Copyright (c) 2015, Facebook, Inc.
All rights reserved.

Licensed under a BSD license:
https://github.com/facebook/mcrouter/blob/master/LICENSE
