/*
 *  Copyright (c) 2016-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include "mcrouter/lib/mc/msg.h"

namespace facebook {
namespace memcache {

inline int resultSeverity(mc_res_t result) {
  switch (result) {
    case mc_res_ok:
    case mc_res_stored:
    case mc_res_stalestored:
    case mc_res_exists:
    case mc_res_deleted:
    case mc_res_found:
      return 1;

    case mc_res_waiting:
      return 2;

    case mc_res_notfound:
    case mc_res_notstored:
      return 4;

    case mc_res_ooo:
    case mc_res_timeout:
    case mc_res_connect_timeout:
    case mc_res_connect_error:
    case mc_res_busy:
    case mc_res_shutdown:
    case mc_res_try_again:
    case mc_res_tko:
      return 5;

    case mc_res_bad_key:
    case mc_res_bad_value:
    case mc_res_aborted:
      return 6;

    case mc_res_remote_error:
    case mc_res_unknown:
      return 7;

    case mc_res_local_error:
      return 8;

    case mc_res_client_error:
      return 9;

    default:
      return 10;
  }
}

/**
 * mc_res_t convenience functions, useful for replies
 */
/**
 * Is this reply an error?
 */
inline bool isErrorResult(const mc_res_t result) {
  return mc_res_is_err(result);
}

/**
 * Is this reply an error as far as failover logic is concerned?
 */
inline bool isFailoverErrorResult(const mc_res_t result) {
  switch (result) {
    case mc_res_busy:
    case mc_res_shutdown:
    case mc_res_tko:
    case mc_res_try_again:
    case mc_res_local_error:
    case mc_res_connect_error:
    case mc_res_connect_timeout:
    case mc_res_timeout:
    case mc_res_remote_error:
      return true;
    default:
      return false;
  }
}

/**
 * Is this reply a soft TKO error?
 */
inline bool isSoftTkoErrorResult(const mc_res_t result) {
  switch (result) {
    case mc_res_timeout:
      return true;
    default:
      return false;
  }
}

/**
 * Is this reply a hard TKO error?
 */
inline bool isHardTkoErrorResult(const mc_res_t result) {
  switch (result) {
    case mc_res_connect_error:
    case mc_res_connect_timeout:
    case mc_res_shutdown:
      return true;
    default:
      return false;
  }
}

/**
 * Did we not even attempt to send request out because at some point
 * we decided the destination is in TKO state?
 *
 * Used to short-circuit failover decisions in certain RouteHandles.
 *
 * If isTkoResult() is true, isErrorResult() must also be true.
 */
inline bool isTkoResult(const mc_res_t result) {
  return result == mc_res_tko;
}

/**
 * Did we not even attempt to send request out because it is invalid/we hit
 * per-destination rate limit
 */
inline bool isLocalErrorResult(const mc_res_t result) {
  return result == mc_res_local_error;
}

/**
 * Was the connection attempt refused?
 */
inline bool isConnectErrorResult(const mc_res_t result) {
  return result == mc_res_connect_error;
}

/**
 * Was there a timeout while attempting to establish a connection?
 */
inline bool isConnectTimeoutResult(const mc_res_t result) {
  return result == mc_res_connect_timeout;
}

/**
 * Was there a timeout when sending data on an established connection?
 * Note: the distinction is important, since in this case we don't know
 * if the data reached the server or not.
 */
inline bool isDataTimeoutResult(const mc_res_t result) {
  return result == mc_res_timeout;
}

/**
 * Application-specific redirect code. Server is up, but doesn't want
 * to reply now.
 */
inline bool isRedirectResult(const mc_res_t result) {
  return result == mc_res_busy || result == mc_res_try_again;
}

/**
 * Was the data found?
 */
inline bool isHitResult(const mc_res_t result) {
  return result == mc_res_deleted || result == mc_res_found ||
      result == mc_res_touched;
}

/**
 * Was data not found and no errors occured?
 */
inline bool isMissResult(const mc_res_t result) {
  return result == mc_res_notfound;
}

/**
 * Lease hot miss?
 */
inline bool isHotMissResult(const mc_res_t result) {
  return result == mc_res_foundstale || result == mc_res_notfoundhot;
}

/**
 * Was the data stored?
 */
inline bool isStoredResult(const mc_res_t result) {
  return result == mc_res_stored || result == mc_res_stalestored;
}

inline bool worseThan(mc_res_t first, mc_res_t second) {
  return resultSeverity(first) > resultSeverity(second);
}
}
} // facebook::memcache
