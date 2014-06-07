/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
/**
 * Cpplint enforces include guards, but this is not a typical include file
 * and we do want to include it multiple times.  This hack fools Cpplint
 * into believing we have a proper include guard.
 */
// %flint: pause
#if 0
// %flint: resume
#ifndef FAKE_INCLUDE_GUARD
#define FAKE_INCLUDE_GUARD
#endif
// %flint: pause
#endif


#ifndef mcrouter_option_group
#define mcrouter_option_group(_sep)
#endif

#define no_long ""
#define no_short '\0'

/**
 * Format:
 *
 * mcrouter_option_<string, integer, or toggle>(
 *  [type (integers only), ] name of field in the struct, default value,
 *  long option (or no_long), short option char (or no_short),
 *  docstring)
 *
 * A long option is a requirement for options that can be set from command line.
 *
 * Short options are optional and in short supply (pun overload).
 *
 * A toggle option doesn't accept a command line argument, and specifying
 * it on the command line will set it to the opposite of the default value.
 *
 * mcrouter_option_group(name) starts a new option group (for nicer display)
 */

mcrouter_option_group("Startup")

mcrouter_option_string(
  service_name, "unknown",
  no_long, no_short,
  "Name of the service using this libmcrouter instance")

mcrouter_option_string(
  router_name, "unknown",
  no_long, no_short,
  "Name for this router instance (should reflect the configuration,"
  " the flavor name is usually a good choice)")

mcrouter_option_integer(
  int, standalone, 0,
  no_long, no_short,
  "")

mcrouter_option_toggle(
  sync, false,
  no_long, no_short,
  "Sync mode: don't start proxy threads, don't fire off async requests")

mcrouter_option_toggle(
  asynclog_disable, false,
  "asynclog-disable", no_short,
  "disable async log file spooling")

mcrouter_option_string(
  async_spool, "/var/spool/mcrouter",
  "async-dir", 'a',
  "container directory for async storage spools")

mcrouter_option_toggle(
  use_asynclog_version2, false,
  "use-asynclog-version2", no_short,
  "Enable using the asynclog version 2.0")

mcrouter_option_integer(
  int, num_proxies, 1,
  "num-proxies", no_short,
  "adjust how many proxy threads to run")

mcrouter_option_toggle(
  use_priorities, true,
  "disable-priorities", no_short,
  "don't use event base priorities")

mcrouter_option_toggle(
  realtime_disabled, false,
  "no-realtime", no_short,
  "when run as root, mcrouter is run with realtime priority to "
  "improve latency. Use this option to disable realtime-priority "
  "when run as root")

mcrouter_option_integer(
  int, disable_dynamic_stats, 0,
  no_long, no_short,
  "")

mcrouter_option_toggle(
  disable_global_dynamic_stats, false,
  no_long, no_short,
  "FIXME: Mcrouter global dynamic stat list is badly broken - it doesn't work"
  " with multiple mcrouter instances in a process.")

mcrouter_option_integer(
  size_t, big_value_split_threshold, 0,
  "big-value-split-threshold", no_short,
  "If 0, big value route handle is not part of route handle tree,"
  "else used as threshold for splitting big values internally")

mcrouter_option_integer(
  size_t, fibers_max_pool_size, 1000,
  "fibers-max-pool-size", no_short,
  "Maximum number of preallocated free fibers to keep around")

mcrouter_option_integer(
  size_t, fibers_stack_size, 16 * 1024,
  "fibers-stack-size", no_short,
  "If RouteHandles are enabled, size of stack in bytes to allocate per fiber")

mcrouter_option_toggle(
  fibers_debug_record_stack_size, false,
  "fibers-debug-record-stack-size", no_short,
  "Record exact amount of fibers stacks used (expensive: debug only!)")

mcrouter_option_string(
  runtime_vars_file,
  MCROUTER_RUNTIME_VARS_DEFAULT,
  "runtime-vars-file", no_short,
  "Path to the runtime variables file.")

mcrouter_option_integer(
  uint32_t, file_observer_poll_period_ms, 100,
  "file-observer-poll-period-ms", no_short,
  "How often to check inotify for updates on the tracked files.")

mcrouter_option_integer(
  uint32_t, file_observer_sleep_before_update_ms, 1000,
  "file-observer-sleep-before-update-ms", no_short,
  "How long to sleep for after an update occured"
  " (a hack to avoid partial writes).")


mcrouter_option_group("Network")

mcrouter_option_integer(
  int, keepalive_cnt, 0,
  "keepalive-count", 'K',
  "set TCP KEEPALIVE count, 0 to disable")

mcrouter_option_integer(
  int, keepalive_interval_s, 60,
  "keepalive-interval", 'i',
  "set TCP KEEPALIVE interval parameter in seconds")

mcrouter_option_integer(
  int, keepalive_idle_s, 300,
  "keepalive-idle", 'I',
  "set TCP KEEPALIVE idle parameter in seconds")

mcrouter_option_integer(
  unsigned int, reset_inactive_connection_interval, 60000,
  "reset-inactive-connection-interval", no_short,
  "Will close open connections without any activity after at most 2 * interval"
  " ms. If value is 0, connections won't be closed.")

mcrouter_option_integer(
  int, tcp_rto_min, -1,
  "tcp-rto-min", no_short,
  "adjust the minimum TCP retransmit timeout (ms) to memcached")

mcrouter_option_integer(
  uint64_t, target_max_inflight_requests, 0,
  "target-max-inflight-requests", no_short,
  "Maximum inflight requests allowed per target per thread"
  " (0 means no throttling)")

mcrouter_option_integer(
  uint64_t, target_max_pending_requests, 100000,
  "target-max-pending-requests", no_short,
  "Only active if target-max-inflight-requests is nonzero."
  " Hard limit on the number of requests allowed in the queue"
  " per target per thread.  Requests that would exceed this limit are dropped"
  " immediately.")

mcrouter_option_toggle(
  no_network, false, "no-network", no_short,
  "Debug only. Return random generated replies, do not use network.")

mcrouter_option_integer(
  size_t, proxy_max_inflight_requests, 0,
  "proxy-max-inflight-requests", no_short,
  "If non-zero, sets the limit on maximum incoming requests that will be routed"
  " in parallel by each proxy thread.  Requests over limit will be queued up"
  " until the number of inflight requests drops.")

mcrouter_option_string(
  pem_cert_path, "",
  "pem-cert-path", no_short,
  "Path of pem-style certificate for ssl")

mcrouter_option_string(
  pem_key_path, "",
  "pem-key-path", no_short,
  "Path of pem-style key for ssl")

mcrouter_option_string(
  pem_ca_path, "",
  "pem-ca-path", no_short,
  "Path of pem-style CA cert for ssl")

mcrouter_option_toggle(
  destination_rate_limiting, false,
  "destination-rate-limiting", no_short,
  "If not enabled, ignore \"rates\" in pool configs.")


mcrouter_option_group("Routing configuration")

mcrouter_option_toggle(
  constantly_reload_configs, false,
  "constantly-reload-configs", no_short,
  "")

mcrouter_option_toggle(
  disable_reload_configs, false,
  "disable-reload-configs", no_short,
  "")

mcrouter_option_string(
  config_file, "",
  "config-file", 'f',
  "load configuration from file")

mcrouter_option_string(
  config_str, "",
  "config-str", no_short,
  "Configuration string provided as a command line argument")

mcrouter_option_string(
  default_route, "/././",
  "route-prefix", 'R',
  "default routing prefix (ex. /oregon/prn1c16/)")

mcrouter_option_toggle(
  async, false,
  "async", 'A',
  "enable asynchronous forwarding of deletes")

mcrouter_option_toggle(
  miss_on_get_errors, true,
  "disable-miss-on-get-errors", no_short,
  "Disable reporting get errors as misses")

mcrouter_option_integer(
  unsigned int, upgrading_l1_exptime, 0,
  "upgrading-l1-exptime", no_short,
  "0 means that we will use the L2 exptime. Otherwise, when upgrading"
  " this value is used as the exptime for all requests")

mcrouter_option_toggle(
  send_invalid_route_to_default, false,
  "send-invalid-route-to-default", no_short,
  "Send request to default route if routing prefix is not present in config")


mcrouter_option_group("TKO probes")

mcrouter_option_toggle(
  global_tko_tracking, false,
  "global-tko-tracking", no_short,
  "If enabled, track TKO per-router instead of per-proxy."
  " This will become the default after testing in production")

mcrouter_option_integer(
  int, disable_tko_tracking, 0,
  no_long, no_short,
  "")

mcrouter_option_integer(
  int, probe_delay_initial_ms, 10000,
  "probe-timeout-initial", 'r',
  "TKO probe retry initial timeout in ms")

mcrouter_option_integer(
  int, probe_delay_max_ms, 60000,
  "probe-timeout-max", no_short,
  "TKO probe retry max timeout in ms")

mcrouter_option_integer(
  int, failures_until_tko, 3,
  "timeouts-until-tko", no_short,
  "Mark as TKO after this many failures")

mcrouter_option_integer(
  size_t, maximum_soft_tkos, 10,
  "maximum-soft-tkos", no_short,
  "The Maximum number of machines we can mark TKO if they don't have a hard"
  " failure.")

mcrouter_option_group("Timeouts")

mcrouter_option_integer(
  unsigned int, server_timeout_ms, 1000,
  "server-timeout", 't',
  "server timeout in ms (DEPRECATED try to use cluster-server-timeout "
  "and regional-server-timeout)")

mcrouter_option_integer(
  unsigned int, cluster_pools_timeout_ms, 0,
  "cluster-pools-timeout", no_short,
  "server timeout for cluster pools in ms. Default value 0 means using "
  "deprecated server-timeout value for the flag")

mcrouter_option_integer(
  unsigned int, regional_pools_timeout_ms, 0,
  "regional-pools-timeout", no_short,
  "server timeout for regional pools in ms. Default value 0 means using "
  "deprecated server-timeout value for the flag")

mcrouter_option_integer(
  unsigned int, cross_region_timeout_ms, 0,
  "cross-region-timeout-ms", no_short,
  "Timeouts for talking to cross region pool. "
  "If specified (non 0) takes precedence over every other timeout.")

mcrouter_option_integer(
  unsigned int, cross_cluster_timeout_ms, 0,
  "cross-cluster-timeout-ms", no_short,
  "Timeouts for talking to pools within same region but different cluster. "
  "If specified (non 0) takes precedence over every other timeout.")

mcrouter_option_integer(
  unsigned int, within_cluster_timeout_ms, 0,
  "within-cluster-timeout-ms", no_short,
  "Timeouts for talking to pools within same cluster. "
  "If specified (non 0) takes precedence over every other timeout.")


mcrouter_option_group("Logging")

mcrouter_option_string(
  stats_root, MCROUTER_STATS_ROOT_DEFAULT,
  "stats-root", no_short,
  "Root directory for stats files")

mcrouter_option_integer(
  unsigned int, stats_logging_interval, 10000,
  "stats-logging-interval", no_short,
  "Time in ms between stats reports, or 0 for no logging")

mcrouter_option_integer(
  unsigned int, logging_rtt_outlier_threshold_us, 0,
  "logging-rtt-outlier-threshold-us", no_short,
  "surpassing this threshold rtt time means we will log it as an outlier. "
  "0 (the default) means that we will do no logging of outliers.")

mcrouter_option_integer(
  unsigned int, stats_async_queue_length, 50,
  "stats-async-queue-length", no_short,
  "Asynchronous queue size for logging.")


#ifdef ADDITIONAL_OPTIONS_FILE
#include ADDITIONAL_OPTIONS_FILE
#endif

#undef no_short
#undef no_long
#undef mcrouter_option_group

// %flint: resume
