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

/* Note that debug level is handled specially in main.cpp
   due to -v and -d options, so no long/short options here */
mcrouter_option_integer(
  int, debug_level, FBI_LOG_NOTIFY,
  no_long, no_short,
  "Debug level")

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
  rlim_t, fdlimit, DEFAULT_FDLIMIT,
  "connection-limit", 'n',
  "Connection limit")

mcrouter_option_integer(
  size_t, max_conns, 0,
  "max-conns", no_short,
  "Maximum number of connections maintained by server")

mcrouter_option_integer(
  uint32_t, max_global_outstanding_reqs, DEFAULT_MAX_GLOBAL_OUTSTANDING_REQS,
  "max-global-outstanding-reqs", no_short,
  "Maximum requests outstanding globally (0 to disable)")

mcrouter_option_integer(
  uint32_t, max_client_outstanding_reqs, DEFAULT_MAX_CLIENT_OUTSTANDING_REQS,
  "max-client-outstanding-reqs", no_short,
  "Maximum requests outstanding per client (0 to disable)")

mcrouter_option_integer(
  size_t, requests_per_read, 0,
  "reqs-per-read", no_short,
  "Adjusts server buffer size to process this many requests per read."
  " Smaller values may improve latency.")

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

#ifdef ADDITIONAL_STANDALONE_OPTIONS_FILE
#include ADDITIONAL_STANDALONE_OPTIONS_FILE
#endif

#undef no_short
#undef no_long
#undef mcrouter_option_group
#undef mcrouter_option
