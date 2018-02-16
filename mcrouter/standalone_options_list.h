/*
 *  Copyright (c) 2014-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
// @nolint
#ifndef MCROUTER_OPTION_GROUP
#define MCROUTER_OPTION_GROUP(_sep)
#endif

#define no_long ""
#define no_short '\0'

/**
 * Format same as in mcrouter_options_list.h
 */

MCROUTER_OPTION_GROUP("Standalone mcrouter options")

MCROUTER_OPTION_STRING(log_file, "", "log-path", 'L', "Log file path")

MCROUTER_OPTION_STRING(
    carbon_router_name,
    "Memcache",
    "carbon-router-name",
    no_short,
    "Name of the carbon router to use")

MCROUTER_OPTION_OTHER(
    std::vector<uint16_t>,
    ports,
    ,
    "port",
    'p',
    "Port(s) to listen on (comma separated)")

MCROUTER_OPTION_OTHER(
    std::vector<uint16_t>,
    ssl_ports,
    ,
    "ssl-port",
    no_short,
    "SSL Port(s) to listen on (comma separated)")

MCROUTER_OPTION_STRING(
  tls_ticket_key_seed_path, "",
  "tls-ticket-key-seed-path", no_short,
  "Path to file containing JSON object for old, current, and new seeds"
  " used to generate TLS ticket keys")

MCROUTER_OPTION_INTEGER(
    int,
    listen_sock_fd,
    -1,
    "listen-sock-fd",
    no_short,
    "Listen socket to take over")

MCROUTER_OPTION_STRING(
    unix_domain_sock,
    "",
    "unix-domain-sock",
    no_short,
    "Unix domain socket path")

MCROUTER_OPTION_INTEGER(
    size_t,
    max_conns,
    0,
    "max-conns",
    no_short,
    "Maximum number of connections maintained by server. Special values: "
    "0 - disable connection eviction logic; 1 - calculate number of maximum "
    "connections based on rlimits. Eviction logic is disabled by default.")

MCROUTER_OPTION_INTEGER(
    int,
    tcp_listen_backlog,
    SOMAXCONN,
    "tcp-listen-backlog",
    no_short,
    "TCP listen backlog size")

MCROUTER_OPTION_INTEGER(
    uint32_t,
    max_client_outstanding_reqs,
    DEFAULT_MAX_CLIENT_OUTSTANDING_REQS,
    "max-client-outstanding-reqs",
    no_short,
    "Maximum requests outstanding per client (0 to disable)")

MCROUTER_OPTION_INTEGER(
    size_t,
    requests_per_read,
    0,
    "reqs-per-read",
    no_short,
    "DEPRECATED. Does nothing.")

MCROUTER_OPTION_TOGGLE(
    retain_source_ip,
    false,
    "retain-source-ip",
    no_short,
    "Look up the source IP address for inbound requests and expose it to"
    " routing logic.")

MCROUTER_OPTION_TOGGLE(
    postprocess_logging_route,
    false,
    "postprocess-logging-route",
    no_short,
    "For all hits logged by LoggingRoute, pass the request & reply pair to "
    "implementation-specific postprocessing logic.")

MCROUTER_OPTION_TOGGLE(
    enable_server_compression,
    false,
    "enable-server-compression",
    no_short,
    "Enable compression on the AsyncMcServer")

MCROUTER_OPTION_INTEGER(
    unsigned int,
    client_timeout_ms,
    1000,
    "client-timeout",
    no_short,
    "Timeout for sending replies back to clients, in milliseconds. "
    "(0 to disable)")

MCROUTER_OPTION_INTEGER(
    uint64_t,
    server_load_interval_ms,
    0,
    "server-load-interval-ms",
    no_short,
    "How often to collect server load data. "
    "(0 to disable exposing server load)")

MCROUTER_OPTION_INTEGER(
    uint32_t,
    tfo_queue_size,
    100000,
    "tfo-queue-size",
    no_short,
    "TFO queue size for SSL connections.  "
    "(only matters if ssl tfo is enabled)")

#ifdef ADDITIONAL_STANDALONE_OPTIONS_FILE
#include ADDITIONAL_STANDALONE_OPTIONS_FILE
#endif

#undef no_short
#undef no_long
#undef MCROUTER_OPTION_GROUP
#undef MCROUTER_OPTION
