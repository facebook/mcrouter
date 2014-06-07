Mcrouter configuration
-----------------

### What is mcrouter config?

Mcrouter config file is used to specify how and where mcrouter routes requests.
It is a JSON file with some extensions:

* Comments are allowed (both `/* */` and `\\`)
* Macros are allowed (see [JSONM](JSON.md))

Configuration object contains two properties: `pools` (optional) and `routes`
(or `route`).
`pools` specifies memcached hosts (or other mcrouter instances) mcrouter sends
requests to, `routes` (`route`) specifies how to do it.
Mcrouter supports dynamic reconfiguration i.e. you don't need to restart
mcrouter once the config changed.

### Quick examples

Oh, it's too boring, I want to use it right now!
OK, here are some common use cases (Mcrouter is capable to do far more):

- Split load between several memcache boxes
``` JSON
  {
    "pools": {
      "A": {
        "servers": [
          // your memcache/mcrouter boxes here, e.g.:
          "127.0.0.1:12345",
          "[::1]:12346"
        ]
      }
    },
    "route": "PoolRoute|A"
  }
```
_Explanation_: depending on key hash requests will be sent to different boxes.
Mcrouter uses consistent hashing to compute key hash. More about consistent
hashing read [here](http://en.wikipedia.org/wiki/Consistent_hashing).

- Failover requests to several boxes
``` JSON
  {
    "pools": { /* same as before */ },
    "route": {
      "type": "PrefixPolicyRoute",
      "operation_policies": {
        "get": "FailoverRoute|Pool|A",
        "set": "AllSyncRoute|Pool|A",
        "delete": "AllSyncRoute|Pool|A"
      }
    }
  }
```
_Explanation_: deletes and sets will be sent to all hosts in pool; gets will
be sent to first host in pool, then if request fails, it will be automatically
retried on the second host and so on.

- Shadow production traffic to testing hosts
``` JSON
  {
    "pools": {
      "production": {
        "servers": [ /* your production memcache boxes here */ ]
      },
      "dev": {
        "servers": [ /* your memcache boxes for testing purposes */ ]
      }
    },
    "route": {
      "type": "PoolRoute",
      "pool": "production",
      "shadows": [
        {
          "target": "PoolRoute|dev",
          // shadow traffic from first and second hosts in 'production' pool
          "index_range": [0, 1],
          // shadow 10% of requests based on key hash
          "key_fraction_range": [0, 0.1]
        }
      ]
    }
  }
```
_Explanation_: all requests will go to the 'production' pool, 10% of requests
sent to first and second host will **also** be sent to the 'dev' pool.

- Sending to different hosts depending on routing prefix.
``` JSON
  {
    "pools": {
      "A": {
        "servers": [ /* hosts of pool A */ ]
      },
      "B": {
        "servers": [ /* hosts of pool B */ ]
      }
    },
    "routes": [
      {
        "aliases": [
          "/a/a/",
          "/A/A/"
        ],
        "route": "PoolRoute|A"
      },
      {
        "aliases": [
          "/b/b/"
        ],
        "route": "PoolRoute|B"
      }
    ]
  }
```
_Explanation_: Simply put, you can use routing prefixes to choose between sets
of memcached boxes. In this example, commands sent to Mcrouter "get /a/a/key"
and "get /A/A/other_key" will be served by servers in pool A (as "get key"
and "get other_key" respectively), while "get /b/b/yet_another_key" will be
served by servers in pool B (which will see "get yet_another_key").

### Defining pools

`pools` property is a dictionary with pool names as keys and pool objects as
values. Each pool object contains an ordered list of destination servers
together with some additional optional properties. Here is a list of
pool properties:

* `servers` (required)
  list of 'host:port' ips:
  ``` JSON
  "servers": [ "127.0.0.1:12345", "[::1]:5000" ]
  ```
  defines destination servers
* `protocol` : "ascii" or "umbrella" (optional, default 'ascii')
  what protocol to use (more about protocols in [Routing](Routing.md))
* `keep_routing_prefix` : bool (optional, default 'false')
  if true, routing prefix won't be stripped from key when sending request
  to this pool. Useful when making a Mcrouter talk to other Mcrouter.

### Defining `routes`

#### Route handles

Routes are composed of smaller blocks called "route handles". Each route handle
encapsulates some piece of routing logic, like sending request to single
destination host or providing failover.

Route handles form a tree with each route handle as a node. Route handle
'receives' request from parent route handle, processes it and 'sends' to
children route handles. Thus, each request is routed from root to leaf(s)
of tree. Reply goes through route handle tree in opposite direction.

#### Representing route handles in JSON

`routes` property is a list of route handle trees and `aliases`.
`aliases` is a list of routing prefixes used for given route handle
tree. Example:

``` JSON
  {
    "routes": [
      {
        "aliases": [
          "/regionA/clusterA/",
          "/regionA/clusterB/"
        ],
        "route" : {
          // route handle tree for regionA
        }
      },
      {
        // other route here
      }
    ]
  }
```

In this example all keys with `/regionA/clusterA/` and `/regionA/clusterB/`
routing prefixes will be routed to regionA route handle tree.

When routing prefixes are not used, one can use `route` instead of
`routes`. Semantically it's the same as specifying the default route (given as
a command line parameter) as the single alias:

``` JSON
  {
    "routes": [
      {
        "aliases": [ /* default routing prefix, passed to mcrouter */ ],
        "route": /* some route handle tree */
      }
    ]
  }
```
is the same as
``` JSON
  {
    "route": /* some route handle tree */
  }
```

Route handles may be represented in JSON in either long or short form.
Long form:
``` JSON
  {
    "type" : "HashRoute",
    "children" : "Pool|MyPool",
    // some options for route here e.g. hash function, salt, etc.
  }
```
Equivalent short form:
``` JSON
  "HashRoute|Pool|MyPool"
```

Long form is used to specify additional options, while short form is
a shortcut with options set to default. One can read the previous example as
'Create HashRoute that will route to pool MyPool'.

#### List of route handles

* **AllFastestRoute**
  Immediately sends the same request to all child route handles.
  Waits until the fastest non-error reply and returns it back to client. Other
  requests complete in background and their replies are silently ignored.
  Properties:
  * `children`
    list of child route handles.


* **AllInitialRoute**
  Immediately sends the same request to all child route handles.
  Waits until the reply from first route handle in the `children` list. other
  requests are sent asynchronously.
  Properties:
  * `children`
    list of child route handles.


* **AllMajorityRoute**
  Imeediately sends the same request to all child route handles. Collects replies
  until some non-error result appears (half + 1) times (or all results if that
  never happens). Responds with one of the replies with the most common result.
  Properties:
  * `children`
    list of child route handles.


* **AllSyncRoute**
  Immediately sends the same request to all child route handles. Collects all
  the replies and responds with the "worst" reply (i.e. the error reply if one
  occured).
  Properties:
  * `children`
    list of child route handles.


* **DevNullRoute**
  Same as **NullRoute**, but with Mcrouter stats reporting.


* **ErrorRoute**
  Returns the error reply for each request right away.
  ErrorRoute may be created with value that should be set in reply e.g.
  `"ErrorRoute|MyErrorValueHere"`


* **FailoverRoute**
  Sends the request to the first child in the list and waits for the reply. If
  the reply is non-error, returns it immediately. Otherwise, sends the request
  to the second child, and so on. If all children respond with errors, returns
  the last error reply.
  Properties:
  * `children`
    list of child route handles.


* **FailoverWithExptimeRoute**
  Failover with additional settings. Sends request to `normal` route handle,
  if it responds with an error, checks `settings` and failovers to `failover`
  if needed.
  Properties:
  * `normal`
    all requests will be sent here first
  * `failover`
    list of route handles to route to in case of error reply from `normal`
  * `failover_exptime` (int, optional, default 60)
    TTL (in seconds) for update requests sent to failover. Useful when failing
    over "sets" into a special pool with a small TTL to protect against
    temporary outages.
  * `settings` (object, optional)
    Object that allows to tweak failover logic, e.g. what errors from `normal`
    route handle are OK to return right away, without sending to `failover`s.
    ``` JSON
      {
        "tko": {
          "gets": bool (optional, default true)
          "updates": bool (optional, default true)
          "deletes": bool (optional, default false)
        },
        "connectTimeout": { /* same as tko */ },
        "dataTimeout": { /* same as tko */ }
      }
    ```
    "tko", "connectTimeout" and "dataTimeout" correspond to reply errors,
    "gets", "updates", "deletes" correspond to operations. `true` means
    Mcrouter should failover the request, `false` means it should return the
    error immediately. More about errors and operations [here](Routing.md).

* **HashRoute**
  Routes to the destination based on key hash.
  Properties:
  * `children`
    list of children route handles.
  * `salt` (string, optional, default empty)
    Salt to add to key before hashing
  * `hash_func` (string, optional, default "Ch3")
    hashing function to use, "Ch3", "Crc32" or "WeightedCh3"
    **TODO!!!!** explain what each function does.
  * `weights` (list of doubles, valid only when hash_func is 'WeightedCh3')
    Weight for each destination. Should have not less elements than number of
    children. Only first (number of children) weights are used, though.


* **HostIdRoute**
  Routes to the destination based on client host id.
  Properties:
  * `children`
    list of children route handles.


* **LatestRoute**
  Route that attempts to "behave well" in how many new targets it connects to.
  Creates a FailoverRoute with at most `failover_count` child handles chosen
  pseudo-randomly based on client host id.
  Properties:
  * `children`
    list of children route handles.
  * `failover_count` (int, optional, default 5)
    number of route handles to route to


* **MigrateRoute**
  This route handle changes behavior based on Migration mode.
  1. Before the migration starts, sends all requests to `from` route handle.
  2. Between start_time and (start_time + interval), sends all requests except
  for deletes to `from` route handle and sends all delete requests to both
  `from` and `to` route handle. For delete requests, returns reply from
  worst among two replies.
  3. Between (start_time + interval) and (start_time + 2*interval), sends all
  requests except for deletes to `to` route handle and sends all delete requests
  to both `from` and `to` route handle. For delete requests, returns reply from
  worst among two replies.
  4. After (start_time + 2*interval), sends all requests to `to` route handle.
  Properties:
  * `from`
    Route handle that routes to destination we are migrating from
  * `to`
    Route handle that routes to destination we are migrating to
  * `start_time` (int)
    Time in seconds from epoch when the migration starts.
  * `interval` (int, options, default 3600)
    Duration of migration (in seconds)


* **MissFailoverRoute**
  For get-like requests, sends the same request sequentially to each route
  handle in the list in order until the first hit reply.
  If all replies result in errors/misses, returns the reply from the
  last destination in the list.
  **TODO!!!!** What should it do for delete/set? Currently the behavior is strange.
  Properties:
  * `children`
    list of children route handles.
* **NullRoute**
  Returns the default reply for each request right away. Default replies are:
  * delete - not found
  * get - not found
  * set - not stored
  No properties.


* **PoolRoute**
  Route handle that routes to a pool. With different settings it gives same
  functionality as HashRoute, also allows rate limiting, shadowing, etc.
  Properties:
  * `pool` (string or object)
    if it is a string, routes to the pool with the given name. If it is an
    object, creates a pool 'on the fly'. This object has the same format as in
    the `pools` property described earlier, with an additional `name` property.
    Example:
    ``` JSON
      {
        "type": "PoolRoute",
        "pool": {
          "name": "MyPool",
          "servers": [ /* pool hosts here */ ]
        }
      }
    ```
  * `shadows` (optional, default empty)
    list of objects that define additional route handles to route to (shadows)
    each object has following properties:
    * `target`
      Route handle for shadow requests
    * `index_range` (array of two integers, both in [0, number of servers in pool - 1])
      Only requests sent to servers from `index_range` will be sent to `target`
    * `key_fraction_range` (array of two doubles, both in [0, 1])
      Only requests with key hash from `key_fraction_range` will be sent to
      `target`
  * `hash` (optional, default "Ch3")
    string or object that defines hash function, same as in **HashRoute**.
  * `rates` (optional, default no rate limiting)
    If set, enables rate limiting of requests to prevent server overload.
    Object that defines rate and burst are parameters to a token bucket
    algorithm (http://en.wikipedia.org/wiki/Token_bucket):
    ``` JSON
      {
        "type": "PoolRoute",
        "pool": "MyPool",
        "rates": {
          "gets_rate": double (optional)
          "gets_burst": double (optional)
          "sets_rate": double (optional)
          "sets_burst": double (optional)
          "deletes_rate": double (optional)
          "deletes_burst": double (optional)
        }
      }
    ```


* **PrefixPolicyRoute**
  RouteHandle that can send to a different target based on operation.
  Properties:
  * `default_policy`
    Route handle to use by default
  * `operation_policies`
    Object with operation name as key and route handle for given operation as
    value. Example:
    ``` JSON
      {
        "default_policy": "PoolRoute|A",
        "operation_policies": {
          "delete": {
            "type": "AllSyncRoute",
            "children": [ "PoolRoute|A", "PoolRoute|B" ]
          }
        }
      }
    ```
    Sends gets and sets to pool A, deletes to pool A and pool B.
    Valid operations are 'get', 'set', 'delete'.


* **WarmUpRoute**
  This route handle is intended to be used as the 'to' route in a Migrate
  route. It allows for substantial changes in the number of boxes in a pool
  without increasing the miss rate and, subsequently, the load on the
  underlying storage or service.
  All sets and deletes go to the target ("cold") route handle.
  Gets are attempted on the "cold" route handle and in case of a miss, data is
  fetched from the "warm" route handle (where the request is likely to result
  in a cache hit). If "warm" returns an hit, the response is then forwarded to
  the client and an asynchronous request, with the configured expiration time,
  updates the value in the "cold" route handle.
  Properties:
  * `cold`
    Route handle that routes to "cold" destination (with empty cache)
  * `warm`
    Route handle that routes to "warm" destination (with filled cache)
  * `exptime`
    Expiration time for warm up requests

#### Prefix route selector

In addition to choosing by routing prefix, Mcrouter also allows selection by
the key prefix directly. For example, you might want to route all keys that
start with foo_ to pool A, while all keys that start with bar_ should go to
pool B. Note that these key prefixes are not stripped, unlike routing prefixes.

To enable this logic, the root of the route handle tree should be a special
route handle with the type set to "PrefixSelectorRoute" and the properties as
follows:

* `policies`
  object with prefix as key and route handle as value. If some key matches
  several prefixes, the longest one is selected.
* `wildcard`
  If key prefix doesn't match any of prefixes from `policies`, request will
  go here.

Example:
``` JSON
  {
    "pools": { /* define pool A, B and C */ }
    "routes": [
      {
        "aliases": [ "/a/a/" ],
        "route": {
          "type": "PrefixSelectorRoute",
          "policies": {
            "a": "PoolRoute|A",
            "ab": "PoolRoute|B"
          },
          "wildcard": "PoolRoute|C"
        }
      }
    ]
  }
```
_Explanation_: requests with routing prefix "/a/a/" and key prefix "a"
(but not "ab"!) will be sent to pool A, requests with routing "/a/a/" and key
prefix "ab" will be sent to pool B. Other requests with routing prefix "/a/a/"
will be sent to pool C. So key "/a/a/abcd" will be sent to pool B (as "abcd");
"/a/a/acdc" to pool A (as "acdc"), "/a/a/b" to pool C (as "b").

#### Named handles

Sometimes the same route handle is used in different parts of the config.
To avoid duplication one can add `name` to route handle object and refer to this
route handle by its name. If two route handles have the same name, only the
first is parsed, he second one is treated as a duplicate and is not parsed
(even if it has different properties); instead the first route handle is
substituted in its place.
Additionally route handles may be defined in the `named_handles` property of
the config. Example:
``` JSON
  {
    "pools": { /* define pool A and pool B */ },
    "named_handles": [
      {
        "type": "PoolRoute",
        "name": "ratedA",
        "pool": "A",
        "rates": {
          "sets_rate" : 10
        }
      }
    ],
    "routes": [
      {
        "aliases": [ "/a/a/" ],
        "route": {
          "type": "FailoverRoute",
          "children": [
            "ratedA",
            {
              "type": "PoolRoute",
              "name": "ratedB",
              "pool": "B",
              "rates": {
                "sets_rate": 100
              }
            }
          ]
        }
      },
      {
        "aliases": [ "/b/b/" ],
        "route": {
          "type": "FailoverRoute",
          "children": [
            "ratedB",
            "ratedA"
          ]
        }
      }
    ]
  }
```
In this example we have two pools with different rate limits. Requests with
"/a/a/" routing prefix will be routed to pool A with failover to pool B,
with "/b/b/" routing prefix to pool B, with failover to pool A.

### JSONM
Furthermore, one can use macros in JSON. More about JSONM [here](JSONM.md).
