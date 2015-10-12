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

mcrouter_option_string(
  flavor_name, "unknown",
  no_long, no_short,
  "Name of the flavor used to configure this router instance.")

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
  size_t, num_proxies, 1,
  "num-proxies", no_short,
  "adjust how many proxy threads to run")

mcrouter_option_integer(
  size_t, client_queue_size, 1024,
  no_long, no_short,
  "McrouterClient -> ProxyThread queue size.")

mcrouter_option_integer(
  size_t, client_queue_no_notify_rate, 0,
  no_long, no_short,
  "Each client will only notify on every Nth request."
  "  If 0, normal notification logic is used - i.e. notify on every request,"
  " best effort avoid notifying twice.  Higher values decrease CPU utilization,"
  " but increase average latency.")

mcrouter_option_integer(
  size_t, client_queue_wait_threshold_us, 0,
  no_long, no_short,
  "Force client queue notification if last drain was at least this long ago."
  "  If 0, this logic is disabled.")

mcrouter_option_integer(
  size_t, big_value_split_threshold, 0,
  "big-value-split-threshold", no_short,
  "If 0, big value route handle is not part of route handle tree,"
  "else used as threshold for splitting big values internally")

mcrouter_option_integer(
  size_t, big_value_batch_size, 10,
  "big-value-batch-size", no_short,
  "If nonzero, big value chunks are written/read in batches of at most"
  " this size.  Used to prevent queue build up with really large values")

mcrouter_option_integer(
  size_t, fibers_max_pool_size, 1000,
  "fibers-max-pool-size", no_short,
  "Maximum number of preallocated free fibers to keep around")

mcrouter_option_integer(
  size_t, fibers_stack_size, 24 * 1024,
  "fibers-stack-size", no_short,
  "Size of stack in bytes to allocate per fiber."
  " 0 means use fibers library default.")

mcrouter_option_integer(
  size_t, fibers_record_stack_size_every, 100000,
  "fibers-record-stack-size-every", no_short,
  "Record exact amount of fibers stacks used for every N fiber. "
  "0 disables stack recording.")

mcrouter_option_toggle(
  fibers_use_guard_pages, true,
  "disable-fibers-use-guard-pages", no_short,
  "If enabled, protect limited amount of fiber stacks with guard pages")

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

mcrouter_option_integer(
  size_t, target_max_shadow_requests, 1000,
  "target-max-shadow-requests", no_short,
  "Hard limit on the number of shadow requests allowed in the queue"
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

mcrouter_option_integer(
  size_t, proxy_max_throttled_requests, 0,
  "proxy-max-throttled-requests", no_short,
  "Only active if proxy-max-inflight-requests is non-zero. "
  "Hard limit on the number of requests to queue per proxy after "
  "there are already proxy-max-inflight-requests requests in flight for the "
  "proxy. Further requests will be rejected with an error immediately. 0 means "
  "disabled.")

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

mcrouter_option_toggle(
  enable_qos, false,
  "enable-qos", no_short,
  "If enabled, sets the DSCP field in IP header according "
  "to the specified qos class.")

mcrouter_option_integer(
  unsigned int, default_qos_class, 0,
  "default-qos-class", no_short,
  "Default qos class to use if qos is enabled and the class is not specified "
  "in pool/server config. The classes go from 0 (lowest priority) to "
  "4 (highest priority) and act on the hightest-order bits of DSCP.")

mcrouter_option_integer(
  unsigned int, default_qos_path, 0,
  "default-qos-path", no_short,
  "Default qos path priority class to use if qos is enabled and it is not "
  "specified in the pool/server config. The path priority classes go from "
  "0 (lowest priority) to 3 (highest priority) and act on the lowest-order "
  "bits of DSCP.")


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

mcrouter_option(
  facebook::memcache::mcrouter::RoutingPrefix, default_route, "/././",
  "route-prefix", 'R',
  "default routing prefix (ex. /oregon/prn1c16/)", routing_prefix)

mcrouter_option_toggle(
  miss_on_get_errors, true,
  "disable-miss-on-get-errors", no_short,
  "Disable reporting get errors as misses")

mcrouter_option_toggle(
  group_remote_errors, false,
  "group-remote-errors", no_short,
  "Groups all remote (i.e. non-local) errors together, returning a single "
  "result for all of them: mc_res_remote_error")

mcrouter_option_toggle(
  send_invalid_route_to_default, false,
  "send-invalid-route-to-default", no_short,
  "Send request to default route if routing prefix is not present in config")

mcrouter_option_toggle(
  enable_flush_cmd, false,
  "enable-flush-cmd", no_short,
  "Enable flush_all command")

mcrouter_option_integer(
  int, reconfiguration_delay_ms, 1000,
  "reconfiguration-delay-ms", no_short,
  "Delay between config files change and mcrouter reconfiguration.")

mcrouter_option_string_map(
  config_params, "config-params", no_short,
  "Params for config preprocessor in format 'name1:value1,name2:value2'. "
  "All values will be passed as strings.")

mcrouter_option_toggle(
  enable_lease_pairing, false,
  "enable-lease-pairing", no_short,
  "Send lease set to the same destination as failed-over lease get.")

mcrouter_option_group("TKO probes")

mcrouter_option_toggle(
  disable_tko_tracking, false,
  "disable-tko-tracking", no_short,
  "Disable TKO tracker (marking a host down for fast failover after"
  " a number of failures, and sending probes to check if the server"
  " came back up).")

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
  size_t, maximum_soft_tkos, 40,
  "maximum-soft-tkos", no_short,
  "The maximum number of machines we can mark TKO if they don't have a hard"
  " failure.")

mcrouter_option_toggle(
  allow_only_gets, false,
  "allow-only-gets", no_short,
  "Testing only. Allow only get-like operations: get, metaget, lease get. "
  "For any other operation return a default reply (not stored/not found).")


mcrouter_option_group("Timeouts")

mcrouter_option_integer(
  unsigned int, server_timeout_ms, 1000,
  "server-timeout", 't',
  "server timeout in ms (DEPRECATED try to use cluster-pools-timeout "
  "and regional-pools-timeout)")

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

mcrouter_option_string(
  debug_fifo_root, "",
  "debug-fifo-root", no_short,
  "Root directory for debug fifos. If empty, debug fifos are disabled.")

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

mcrouter_option_toggle(
  enable_failure_logging, true,
  "disable-failure-logging", no_short,
  "Disable failure logging.")

mcrouter_option_toggle(
  cpu_cycles, false,
  "cpu-cycles", no_short,
  "Enables CPU cycles counting for performance measurement.")

mcrouter_option_toggle(
  test_mode, false,
  "test-mode", no_short,
  "Starts mcrouter in test mode - with logging disabled.")

mcrouter_option_toggle(
  enable_logging_route, false,
  "enable-logging-route", no_short,
  "Log every request via LoggingRoute.")

mcrouter_option_integer(
  int, asynclog_port_override, 0, no_long, no_short,
  "If non-zero use this port while logging to async log")

#ifdef ADDITIONAL_OPTIONS_FILE
#include ADDITIONAL_OPTIONS_FILE
#endif

#undef no_short
#undef no_long
#undef mcrouter_option_group
