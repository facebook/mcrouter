Mcrouter
-----------------

Mcrouter is a memcache protocol routing layer. It abstracts request routing,
connection pooling, failover, and many other features from the client, which
can simply talk to Mcrouter over a TCP connection using the memcache protocol.
Typically, minimal or no client changes are needed to make use of Mcrouter's
features, which is set up as a drop-in proxy between the client and memcached
hosts. At Facebook, Mcrouter is a core component of a distributed cache
infrastructure that spans over a large number of individual Memcached boxes.

To install Mcrouter, see [Installation](Installation.md).

### Overview

Essentially, Mcrouter takes in a memcache request, applies rules defined by the
configs, and sends it on. The complexity resides in defining and subscribing to
the configuration rules properly. Configuration allows for flexible
specification of special handling and failover behavior.

### Command line arguments

Assuming you have a memcached instance on the local host running on port 5001,
the simplest Mcrouter setup is (::1 is IPv6 loopback address; IPv6 addresses
must be specified in square brackets. You can also use "127.0.0.1:5001" or
"localhost:5001"):
``` Shell
./mcrouter --config-str='{"pools":{"A":{"servers":["[::1]:5001"]}},"route":"PoolRoute|A"}' -p 5000
```

To test it works one can send a request to port 5000. For example, using
Netcat (http://netcat.sourceforge.net/):
``` Shell
echo -ne "get key\r\n" | nc 0 5000
```

For a complete list of command line arguments, check `./mcrouter --help`.

### Routing

Mcrouter supports typical memcache protocol commands like get, set, delete, etc.
and specific commands to access stats, version and so on.
More about routing and specific Mcrouter commands read [here](Routing.md).

### Configuration

See [Configuration](Configuration.md) for information about how Mcrouter is
configured. Mcrouter config is JSON file(s) primarily consisting of pool and
route information.
