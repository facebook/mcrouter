# Mcrouter [![Build Status](https://travis-ci.org/facebook/mcrouter.svg?branch=master)](https://travis-ci.org/facebook/mcrouter)

This section shows canned mcrouter configurations that can help users ramp up quickly.

## sharded_repl_async_missfailover.json

    "pools": {
       "A": {
          "servers": [
              "192.168.45.101:11211",
              "192.168.45.102:11211"
          ]
      },
      "B": {
         "servers": [
             "192.168.45.103:11211",
             "192.168.45.104:11211"
         ]
      }
    },
    "route": {
      "type": "OperationSelectorRoute",
      "default_policy": {
          "type": "AllAsyncRoute",
          "children": [ "PoolRoute|A", "PoolRoute|B" ]
      },
      "operation_policies": {
         "get": {
             "type": "MissFailoverRoute",
             "children": [ "PoolRoute|A", "PoolRoute|B" ]
         }
      }
    }
    }

## Overview
The above configuration allows users to set up 2x2 memcache pools where keys will be sharded based on the default hashing algorithm.

## Route Explanation
* The OperationSelectorRoute with the default AllAsyncRoute configuration will send all mutations to both pools asynchronously
* If any of memcached node(s) in Pool A go down, all set ops will bypass the node(s) and continue on in Pool B
* If any of memcached node(s) in Pool A go down, some get ops will result in a cache miss and get re-directed seamlessly to Pool B
* When the memcached node comes back up, it will be empty. Mcrouter will continue to fulfill get requests from Pool B

## Links

Documentation: https://github.com/facebook/mcrouter/wiki
Engineering discussions and support: https://www.facebook.com/groups/mcrouter

## License

Copyright (c) 2016, Facebook, Inc.
All rights reserved.

Licensed under a BSD license:
https://github.com/facebook/mcrouter/blob/master/LICENSE
