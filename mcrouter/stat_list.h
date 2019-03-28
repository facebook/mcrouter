/*
 *  Copyright (c) 2014-present, Facebook, Inc.
 *
 *  This source code is licensed under the MIT license found in the LICENSE
 *  file in the root directory of this source tree.
 *
 */
// @nolint

/**
 * Basic stats/information
 */
#define GROUP basic_stats | detailed_stats
STSS(version, MCROUTER_PACKAGE_STRING, 0)
STSS(commandargs, "", 0)
STSI(pid, 0, 0)
STSI(parent_pid, 0, 0)
STUI(time, 0, 0)
#undef GROUP
#define GROUP ods_stats | basic_stats | detailed_stats
// how long ago we started up libmcrouter instance
STUI(uptime, 0, 0)
#undef GROUP


/**
 * Standalone mcrouter stats (not applicable to libmcrouter)
 */
#define GROUP ods_stats | detailed_stats
STUI(num_client_connections, 0, 1)
#undef GROUP


/**
 * Stats related to connections to servers
 */
#define GROUP ods_stats | basic_stats | detailed_stats
STUI(num_servers, 0, 1) // num of ProxyDestinations possible connections.
STUI(num_servers_new, 0, 1) // num of new connections (not open yet).
STUI(num_servers_up, 0, 1) // num of opened connections.
STUI(num_servers_down, 0, 1) // num of conns that weren't explicitly closed.
STUI(num_servers_closed, 0, 1) // num of connections closed (were open before).
// same as above, but for ssl.
STUI(num_ssl_servers, 0, 1)
STUI(num_ssl_servers_new, 0, 1)
STUI(num_ssl_servers_up, 0, 1)
STUI(num_ssl_servers_down, 0, 1)
STUI(num_ssl_servers_closed, 0, 1)
// number of servers marked-down/suspect
STUI(num_suspect_servers, 0, 1)
// Running total of connection opens/closes
STUI(num_connections_opened, 0, 1)
STUI(num_connections_closed, 0, 1)
// Running total of ssl connection opens/closes.
STUI(num_ssl_connections_opened, 0, 1)
STUI(num_ssl_connections_closed, 0, 1)
// Running total of successful SSL connection attempts/successes
STUI(num_ssl_resumption_attempts, 0, 1)
STUI(num_ssl_resumption_successes, 0, 1)
// time between closing an inactive connection and opening it again.
STAT(inactive_connection_closed_interval_sec, stat_double, 0, .dbl = 0.0)
// Information about connect retries
STUI(num_connect_success_after_retrying, 0, 1)
STUI(num_connect_retries, 0, 1)
// Connections closed due to retransmits
STUI(retrans_closed_connections, 0, 1)
#undef GROUP


/**
 * OutstandingLimitRoute (OLR) queue-related stats, broken down by request type
 * (get-like and update-like).
 */
#define GROUP ods_stats | basic_stats | rate_stats
// Number of requests/second that couldn't be processed immediately in OLR
STUI(outstanding_route_get_reqs_queued, 0, 1)
STUI(outstanding_route_update_reqs_queued, 0, 1)
#undef GROUP
#define GROUP ods_stats | basic_stats
// Average number of requests waiting in OLR at any given time
STAT(outstanding_route_get_avg_queue_size, stat_double, 0, .dbl = 0.0)
STAT(outstanding_route_update_avg_queue_size, stat_double, 0, .dbl = 0.0)
// Average time a request had to wait in OLR
STAT(outstanding_route_get_avg_wait_time_sec, stat_double, 0, .dbl = 0.0)
STAT(outstanding_route_update_avg_wait_time_sec, stat_double, 0, .dbl = 0.0)
#undef GROUP
#define GROUP rate_stats
// OutstandingLimitRoute queue-related helper stats
STUI(outstanding_route_get_reqs_queued_helper, 0, 0)
STUI(outstanding_route_update_reqs_queued_helper, 0, 0)
STUI(outstanding_route_get_wait_time_sum_us, 0, 0)
STUI(outstanding_route_update_wait_time_sum_us, 0, 0)
// Retransmission per byte helper stats
STUI(retrans_per_kbyte_sum, 0, 0)
STUI(retrans_num_total, 0, 0)
#undef GROUP


/**
 * Stats about the process
 */
#define GROUP ods_stats | basic_stats
STUI(ps_rss, 0, 0)
#undef GROUP
#define GROUP basic_stats | detailed_stats
STAT(rusage_system, stat_double, 0, .dbl = 0.0)
STAT(rusage_user, stat_double, 0, .dbl = 0.0)
STUI(ps_num_minor_faults, 0, 0)
STUI(ps_num_major_faults, 0, 0)
STAT(ps_user_time_sec, stat_double, 0, .dbl = 0.0)
STAT(ps_system_time_sec, stat_double, 0, .dbl = 0.0)
STUI(ps_vsize, 0, 0)
#undef GROUP


/**
 * Stats about fibers
 */
#define GROUP ods_stats | basic_stats
STUI(fibers_allocated, 0, 0)
STUI(fibers_pool_size, 0, 0)
STUI(fibers_stack_high_watermark, 0, 0)
#undef GROUP


/**
 * Stats about routing
 */
#define GROUP ods_stats | basic_stats
// number of requests that were spooled to disk
STUI(asynclog_requests, 0, 1)
// Proxy requests that are currently being routed.
STUI(proxy_reqs_processing, 0, 1)
// Proxy requests queued up and not routed yet
STUI(proxy_reqs_waiting, 0, 1)
STAT(client_queue_notify_period, stat_double, 0, .dbl = 0.0)
#undef GROUP
#define GROUP ods_stats | detailed_stats
STUI(proxy_request_num_outstanding, 0, 1)
#undef GROUP
#define GROUP detailed_stats
STUI(dev_null_requests, 0, 1)
#undef GROUP
#define GROUP ods_stats | detailed_stats | count_stats
STUI(rate_limited_log_count, 0, 1)
STUI(load_balancer_load_reset_count, 0, 1)
#undef GROUP
#define GROUP count_stats
STUI(request_sent_count, 0, 1)
STUI(request_error_count, 0, 1)
STUI(request_success_count, 0, 1)
STUI(request_replied_count, 0, 1)
#undef GROUP
#define GROUP ods_stats | detailed_stats | rate_stats
STUIR(request_sent, 0, 1)
STUIR(request_error, 0, 1)
STUIR(request_success, 0, 1)
STUIR(request_replied, 0, 1)
STUIR(client_queue_notifications, 0, 1)
STUIR(failover_all, 0, 1)
STUIR(failover_conditional, 0, 1)
STUIR(failover_all_failed, 0, 1)
STUIR(failover_rate_limited, 0, 1)
STUIR(failover_inorder_policy, 0, 1)
STUIR(failover_inorder_policy_failed, 0, 1)
STUIR(failover_least_failures_policy, 0, 1)
STUIR(failover_least_failures_policy_failed, 0, 1)
STUIR(failover_custom_policy, 0, 1)
STUIR(failover_custom_policy_failed, 0, 1)
#undef GROUP
#define GROUP ods_stats | count_stats
STUI(result_error_count, 0, 1)
STUI(result_error_all_count, 0, 1)
STUI(result_connect_error_count, 0, 1)
STUI(result_connect_error_all_count, 0, 1)
STUI(result_connect_timeout_count, 0, 1)
STUI(result_connect_timeout_all_count, 0, 1)
STUI(result_data_timeout_count, 0, 1)
STUI(result_data_timeout_all_count, 0, 1)
STUI(result_busy_count, 0, 1)
STUI(result_busy_all_count, 0, 1)
STUI(result_tko_count, 0, 1)
STUI(result_tko_all_count, 0, 1)
STUI(result_client_error_count, 0, 1)
STUI(result_client_error_all_count, 0, 1)
STUI(result_local_error_count, 0, 1)
STUI(result_local_error_all_count, 0, 1)
STUI(result_remote_error_count, 0, 1)
STUI(result_remote_error_all_count, 0, 1)
STUI(failover_custom_limit_reached, 0, 1)
STUI(failover_custom_master_region, 0, 1)
STUI(failover_custom_master_region_skipped, 0, 1)
#undef GROUP
#define GROUP ods_stats | detailed_stats | rate_stats
STUIR(final_result_error, 0, 1)
STUIR(result_error, 0, 1)
STUIR(result_error_all, 0, 1)
STUIR(result_connect_error, 0, 1)
STUIR(result_connect_error_all, 0, 1)
STUIR(result_connect_timeout, 0, 1)
STUIR(result_connect_timeout_all, 0, 1)
STUIR(result_data_timeout, 0, 1)
STUIR(result_data_timeout_all, 0, 1)
STUIR(result_busy, 0, 1)
STUIR(result_busy_all, 0, 1)
STUIR(result_tko, 0, 1)
STUIR(result_tko_all, 0, 1)
STUIR(result_client_error, 0, 1)
STUIR(result_client_error_all, 0, 1)
STUIR(result_local_error, 0, 1)
STUIR(result_local_error_all, 0, 1)
STUIR(result_remote_error, 0, 1)
STUIR(result_remote_error_all, 0, 1)
#undef GROUP


/**
 * Stats about RPC (AsyncMcClient)
 */
#define GROUP basic_stats | rate_stats
STUI(destination_batches_sum, 0, 1)
STUI(destination_requests_sum, 0, 1)
#undef GROUP
#define GROUP basic_stats | detailed_stats
// count the dirty requests (i.e. requests that needed reserialization)
STUI(destination_reqs_dirty_buffer_sum, 0, 1)
STUI(destination_reqs_total_sum, 0, 1)
#undef GROUP
#define GROUP ods_stats | basic_stats | max_max_stats
STUI(retrans_per_kbyte_max, 0, 1)
#undef GROUP
#define GROUP ods_stats | basic_stats
// Total reqs in AsyncMcClient yet to be sent to memcache.
STUI(destination_pending_reqs, 0, 1)
// Total reqs waiting for reply from server.
STUI(destination_inflight_reqs, 0, 1)
STAT(destination_batch_size, stat_double, 0, .dbl = 0.0)
STAT(destination_reqs_dirty_buffer_ratio, stat_double, 0, .dbl = 0.0)
// duration of the rpc call
STAT(duration_us, stat_double, 0, .dbl = 0.0)
// Duration microseconds, broken down by request type (get-like and update-like)
STAT(duration_get_us, stat_double, 0, .dbl = 0.0)
STAT(duration_update_us, stat_double, 0, .dbl = 0.0)
#undef GROUP
#define GROUP ods_stats | basic_stats | max_stats
STUI(destination_max_pending_reqs, 0, 1)
STUI(destination_max_inflight_reqs, 0, 1)
#undef GROUP
#define GROUP ods_stats | count_stats
// stats about sending lease-set to where the lease-get came from
STUI(redirected_lease_set_count, 0, 1)
#undef GROUP
#define GROUP ods_stats | detailed_stats | rate_stats
// This is the total number of times we called write to a socket.
STUIR(num_socket_writes, 0, 1)
// This is the number of times we couldn't complete a full write.
// Note: this can be larger than num_socket_writes. E.g. if we called write
// on a huge buffer, we would partially write it many times.
STUIR(num_socket_partial_writes, 0, 1)
#undef GROUP
#define GROUP detailed_stats | rate_stats
STUIR(replies_compressed, 0, 1)
STUIR(replies_not_compressed, 0, 1)
STUIR(reply_traffic_before_compression, 0, 1)
STUIR(reply_traffic_after_compression, 0, 1)
#undef GROUP
#define GROUP ods_stats | detailed_stats
STAT(retrans_per_kbyte_avg, stat_double, 0, .dbl = 0.0)
#undef GROUP


/**
 * Stats about configuration
 */
#define GROUP ods_stats | detailed_stats
STUI(config_age, 0, 0)
STUI(config_last_attempt, 0, 0)
STUI(config_last_success, 0, 0)
STUI(config_failures, 0, 0)
STUI(configs_from_disk, 0, 0)
#undef GROUP
