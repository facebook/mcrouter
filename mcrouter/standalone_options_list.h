/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
// @nolint
#ifndef mcrouter_option_group
#define mcrouter_option_group(_sep)
#endif

#define no_long ""
#define no_short '\0'

/**
 * Format same as in mcrouter_options_list.h
 */

mcrouter_option_group("Standalone mcrouter options")

mcrouter_option_string(
  log_file, "",
  "log-path", 'L',
  "Log file path")

mcrouter_option_other(
  std::vector<uint16_t>, ports, ,
  "port", 'p',
  "Port(s) to listen on (comma separated)")

mcrouter_option_other(
  std::vector<uint16_t>, ssl_ports, ,
  "ssl-port", no_short,
  "SSL Port(s) to listen on (comma separated)")

mcrouter_option_integer(
  int, listen_sock_fd, -1,
  "listen-sock-fd", no_short,
  "Listen socket to take over")

mcrouter_option_string(
  unix_domain_sock, "",
  "unix-domain-sock", no_short,
  "Unix domain socket path")

mcrouter_option_integer(
  size_t, max_conns, 0,
  "max-conns", no_short,
  "Maximum number of connections maintained by server. Special values: "
  "0 - disable connection eviction logic; 1 - calculate number of maximum "
  "connections based on rlimits. Eviction logic is disabled by default.")

mcrouter_option_integer(
  int, tcp_listen_backlog, SOMAXCONN,
  "tcp-listen-backlog", no_short,
  "TCP listen backlog size")

mcrouter_option_integer(
  uint32_t, max_client_outstanding_reqs, DEFAULT_MAX_CLIENT_OUTSTANDING_REQS,
  "max-client-outstanding-reqs", no_short,
  "Maximum requests outstanding per client (0 to disable)")

mcrouter_option_integer(
  size_t, requests_per_read, 0,
  "reqs-per-read", no_short,
  "DEPRECATED. Does nothing.")

mcrouter_option_toggle(
  retain_source_ip, false,
  "retain-source-ip", no_short,
  "Look up the source IP address for inbound requests and expose it to"
  " routing logic.")

mcrouter_option_toggle(
  postprocess_logging_route, false,
  "postprocess-logging-route", no_short,
  "For all hits logged by LoggingRoute, pass the request & reply pair to "
  "implementation-specific postprocessing logic."
)

mcrouter_option_toggle(
  enable_server_compression, false,
  "enable-server-compression", no_short,
  "Enable compression on the AsyncMcServer")

mcrouter_option_integer(
  unsigned int, client_timeout_ms, 1000,
  "client-timeout", no_short,
  "Timeout for sending replies back to clients, in milliseconds. "
  "(0 to disable)");

#ifdef ADDITIONAL_STANDALONE_OPTIONS_FILE
#include ADDITIONAL_STANDALONE_OPTIONS_FILE
#endif

#undef no_short
#undef no_long
#undef mcrouter_option_group
#undef mcrouter_option
