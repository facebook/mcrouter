namespace cpp2 carbon.thrift

cpp_include "<mcrouter/lib/carbon/Keys.h>"

typedef binary (cpp.type = "carbon::Keys<folly::IOBuf>",
                cpp.indirection = ".rawUnsafe()") IOBufKey

typedef binary (cpp.type = "carbon::Keys<std::string>",
                cpp.indirection = ".rawUnsafe()") StringKey

/**
 * This is mirroring the mc_res_t enum from mcrouter/lib/mc/msg.h
 */
enum Result {
  mc_res_unknown = 0,
  mc_res_deleted = 1
  mc_res_touched = 2
  mc_res_found = 3
  mc_res_foundstale = 4 /* hot-miss w/ stale data */
  mc_res_notfound = 5
  mc_res_notfoundhot = 6 /* hot-miss w/o stale data */
  mc_res_notstored = 7
  mc_res_stalestored = 8
  mc_res_ok = 9
  mc_res_stored = 10
  mc_res_exists = 11
  /* soft errors -- */
  mc_res_ooo = 12 /* out of order (UDP) */
  mc_res_timeout = 13 /* request timeout (connection was already established) */
  mc_res_connect_timeout = 14
  mc_res_connect_error = 15
  mc_res_busy = 16 /* the request was refused for load shedding */
  mc_res_try_again = 17 /* this request was refused, but we should keep sending
                       requests to this box */
  mc_res_shutdown = 18 /* Server is shutting down. Use failover destination. */
  mc_res_tko = 19 /* total knock out - the peer is down for the count */
  /* hard errors -- */
  mc_res_bad_command = 20
  mc_res_bad_key = 21
  mc_res_bad_flags = 22
  mc_res_bad_exptime = 23
  mc_res_bad_lease_id = 24
  mc_res_bad_cas_id = 25
  mc_res_bad_value = 26
  mc_res_aborted = 27
  mc_res_client_error = 28
  mc_res_local_error = 29, /* an error internal to libmc */
  /* an error was reported by the remote server.

     TODO I think this can also be triggered by a communications problem or
     disconnect, but I think these should be separate errors. (fugalh) */
  mc_res_remote_error = 30
  /* in progress -- */
  mc_res_waiting = 31
  mc_nres = 32// placeholder
}
