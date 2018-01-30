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
#define GROUP mcproxy_stats | detailed_stats
STSS(version, MCROUTER_PACKAGE_STRING, 0)
STSS(commandargs, "", 0)
STSI(pid, 0, 0)
STSI(parent_pid, 0, 0)
STUI(time, 0, 0)
#undef GROUP
#define GROUP ods_stats | mcproxy_stats | detailed_stats
STUI(uptime, 0, 0)
STUI(num_servers, 0, 1)
STUI(num_servers_new, 0, 1)
STUI(num_servers_up, 0, 1)
STUI(num_servers_down, 0, 1)
STUI(num_servers_closed, 0, 1)
STUI(num_ssl_servers_up, 0, 1)
STUI(num_clients, 0, 1)
// Current number of open SSL connections
STUI(num_suspect_servers, 0, 1)
// Running total of successful SSL connection attempts
STUI(num_ssl_connection_successes, 0, 1)
STUI(num_ssl_resumption_attempts, 0, 1)
STUI(num_ssl_resumption_successes, 0, 1)
#undef GROUP
#define GROUP mcproxy_stats | rate_stats
STUI(destination_batches_sum, 0, 1)
STUI(destination_requests_sum, 0, 1)
#undef GROUP
/**
 * OutstandingLimitRoute (OLR) queue-related stats, broken down by request type
 * (get-like and update-like).
 */
#define GROUP ods_stats | mcproxy_stats | rate_stats
/* Number of requests/second that couldn't be processed immediately in OLR */
STUI(outstanding_route_get_reqs_queued, 0, 1)
STUI(outstanding_route_update_reqs_queued, 0, 1)
#undef GROUP
#define GROUP ods_stats | mcproxy_stats
/* Average number of requests waiting in OLR at any given time */
STAT(outstanding_route_get_avg_queue_size, stat_double, 0, .dbl = 0.0)
STAT(outstanding_route_update_avg_queue_size, stat_double, 0, .dbl = 0.0)
/* Average time a request had to wait in OLR */
STAT(outstanding_route_get_avg_wait_time_sec, stat_double, 0, .dbl = 0.0)
STAT(outstanding_route_update_avg_wait_time_sec, stat_double, 0, .dbl = 0.0)
/* Connections closed due to retransmits */
STUI(retrans_closed_connections, 0, 1)
#undef GROUP
#define GROUP rate_stats
/* OutstandingLimitRoute queue-related helper stats */
STUI(outstanding_route_get_reqs_queued_helper, 0, 0)
STUI(outstanding_route_update_reqs_queued_helper, 0, 0)
STUI(outstanding_route_get_wait_time_sum_us, 0, 0)
STUI(outstanding_route_update_wait_time_sum_us, 0, 0)
/* Retransmission per byte helper stats */
STUI(retrans_per_kbyte_sum, 0, 0)
STUI(retrans_num_total, 0, 0)
#undef GROUP
#define GROUP ods_stats | mcproxy_stats
/* Total reqs in mc client yet to be sent to memcache. */
STUI(destination_pending_reqs, 0, 1)
/* Total reqs waiting for reply from memcache. */
STUI(destination_inflight_reqs, 0, 1)
STAT(destination_batch_size, stat_double, 0, .dbl = 0.0)
STUI(asynclog_requests, 0, 1)
/* Proxy requests we started routing */
STUI(proxy_reqs_processing, 0, 1)
/* Proxy requests queued up and not routed yet */
STUI(proxy_reqs_waiting, 0, 1)
STAT(client_queue_notify_period, stat_double, 0, .dbl = 0.0)
//  STUI(bytes_read, 0)
//  STUI(bytes_written, 0)
//  STUI(get_hits, 0)
//  STUI(get_misses, 0)
STAT(rusage_system, stat_double, 0, .dbl = 0.0)
STAT(rusage_user, stat_double, 0, .dbl = 0.0)
STUI(ps_num_minor_faults, 0, 0)
STUI(ps_num_major_faults, 0, 0)
STAT(ps_user_time_sec, stat_double, 0, .dbl = 0.0)
STAT(ps_system_time_sec, stat_double, 0, .dbl = 0.0)
STUI(ps_vsize, 0, 0)
STUI(ps_rss, 0, 0)
STUI(fibers_allocated, 0, 0)
STUI(fibers_pool_size, 0, 0)
STUI(fibers_stack_high_watermark, 0, 0)
//  STUI(failed_client_connections, 0)
STUI(successful_client_connections, 0, 1)
STAT(duration_us, stat_double, 0, .dbl = 0.0)
#undef GROUP
#define GROUP ods_stats | mcproxy_stats | max_stats
STUI(destination_max_pending_reqs, 0, 1)
STUI(destination_max_inflight_reqs, 0, 1)
#undef GROUP
#define GROUP ods_stats | detailed_stats | count_stats
STUI(rate_limited_log_count, 0, 1)
STUI(load_balancer_load_reset_count, 0, 1)
#undef GROUP
#define GROUP ods_stats | count_stats
STUI(redirected_lease_set_count, 0, 1)
#undef GROUP
#define GROUP ods_stats | detailed_stats | rate_stats
  STUIR(replies_compressed, 0, 1)
  STUIR(replies_not_compressed, 0, 1)
  STUIR(reply_traffic_before_compression, 0, 1)
  STUIR(reply_traffic_after_compression, 0, 1)
#undef GROUP
#define GROUP ods_stats | detailed_stats
STUI(config_age, 0, 0)
STUI(config_last_attempt, 0, 0)
STUI(config_last_success, 0, 0)
STUI(config_failures, 0, 0)
STUI(configs_from_disk, 0, 0)
STUI(start_time, 0, 0)
STUI(dev_null_requests, 0, 1)
STUI(proxy_request_num_outstanding, 0, 1)
STAT(retrans_per_kbyte_avg, stat_double, 0, .dbl = 0.0)
#undef GROUP
#define GROUP ods_stats | mcproxy_stats | max_max_stats
STUI(retrans_per_kbyte_max, 0, 1)
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
STUI(result_local_error_count, 0, 1)
STUI(result_local_error_all_count, 0, 1)
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
STUIR(result_local_error, 0, 1)
STUIR(result_local_error_all, 0, 1)
#undef GROUP
