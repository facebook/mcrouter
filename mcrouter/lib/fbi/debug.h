/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#ifndef FBI_DEBUG_H
#define FBI_DEBUG_H

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "mcrouter/lib/fbi/decls.h"
#include "mcrouter/lib/fbi/time.h"

__BEGIN_DECLS

/** Handy format defines for 64-bit ints
    @deprecated use PRIu64 per C99 */
#include <inttypes.h>
#define u64fmt PRIu64
#define d64fmt PRId64

#define FBI_DBG_LOGFILE_ENVVAR "FBDBG_LOGFILE"

#define FBI_STATIC_ASSERT(COND)                 \
  typedef char static_assertion[(COND)?1:-1] __attribute__((__unused__))

#ifndef __ASSERT_FUNCTION
#define __ASSERT_FUNCTION __func__
#endif

/** FBI_ASSERT always checks, even with NDEBUG */
extern void _assert_func(const char *assertion, const char *file,
                         int line, const char *function);
#define FBI_ASSERT(expr) \
  ((expr)                       \
   ? __ASSERT_VOID_CAST (0)                                             \
   : (_assert_func (__STRING(expr), __FILE__, __LINE__, __ASSERT_FUNCTION), \
      __ASSERT_VOID_CAST (0)))

#define FBI_VERIFY(X) FBI_ASSERT(X)

typedef void (*assert_hook_fn)(const char *assert_msg);
extern void fbi_set_assert_hook(assert_hook_fn assert_hook);

#include "nstring.h"

void fbi_dbg_log(const char* principal, const char* component,
                 const char* function, const int line,
                 const char* type, const uint level,
                 const int indent_offset, const char* format, ...)
    __attribute__ ((__format__ (__printf__, 8, 9))); /* tell gcc to check printf args */

uint fbi_get_debug();
void fbi_set_debug(const uint level);

const nstring_t*  fbi_get_debug_logfile();
void fbi_set_debug_logfile(const nstring_t* value);

typedef enum fbi_data_format_e {
  fbi_date_default  = 0,  /* default is unix time stamps */
  fbi_date_unix     = 0,  /* unix time */
  fbi_date_local,         /* human readable format in local time */
  fbi_date_utc,           /* human readable format in UTC */
  fbi_date_max
} fbi_date_format_t;

void fbi_set_debug_date_format(fbi_date_format_t fmt);

void dbg_exit();

#if !defined(DEBUG_PRINCIPAL)
#define DEBUG_PRINCIPAL ""
#endif

/* Messages get logged up to DEBUG_MAX_LEVEL (defaults to UINT_MAX if
 * not #defined before including this header) or up to the level set
 * by calling fbi_set_debug() (defaults to FBI_LOG_DEFAULT if
 * fbi_set_debug() is never called), whichever is lower. The
 * difference is that DEBUG_MAX_LEVEL operates statically, causing
 * superfluous debug calls to be eliminated at compile time. If you
 * want performance, set DEBUG_MAX_LEVEL to something low, like
 * FBI_LOG_INFO (FBI_LOG_DEFAULT). If this is an fbcode optimized build
 * and DEBUG_MAX_LEVEL is unset then we assume you want that level of
 * performance and compile debug calls out by setting DEBUG_MAX_LEVEL to
 * FBI_LOG_DEFAULT. */

#if !defined(DEBUG_MAX_LEVEL)
#ifdef FBCODE_OPT_BUILD
#define DEBUG_MAX_LEVEL FBI_LOG_DEFAULT
#else
#define DEBUG_MAX_LEVEL UINT_MAX
#endif
#endif

#define dbg_log(t, l, io, f, args...) do {                              \
    const uint __level = (l);                                           \
    if (__level <= DEBUG_MAX_LEVEL && __level <= fbi_get_debug()) {     \
      fbi_dbg_log(DEBUG_PRINCIPAL, __FILE__, __FUNCTION__, __LINE__,    \
                  (t), __level, (io), (f), ##args);                     \
    }                                                                   \
  } while (0)

#define FBI_LOG_CRITICAL 10
#define FBI_LOG_ERROR 20
#define FBI_LOG_WARNING 30
#define FBI_LOG_NOTIFY 40
#define FBI_LOG_INFO 50
#define FBI_LOG_DEBUG 60
#define FBI_LOG_SPEW 70
#define FBI_LOG_LOW FBI_LOG_SPEW
#define FBI_LOG_MEDIUM FBI_LOG_DEBUG
#define FBI_LOG_HIGH FBI_LOG_INFO
#define FBI_LOG_DEFAULT FBI_LOG_INFO

#define _dbg_at(l, t, io, f, args...) dbg_log(t, l, io, f, ##args)
#define dbg_at(l, f, args...)   _dbg_at(l, NULL, 0, f, ##args)

#define dbg_fentry(f, args...)  _dbg_at(FBI_LOG_SPEW, "fentry", 1, f, ##args)
#define dbg_fexit(f, args...)   _dbg_at(FBI_LOG_SPEW, "fexit", -1, f, ##args)
#define dbg_error(f, args...)   _dbg_at(FBI_LOG_ERROR, "error", 0, f, ##args)
#define dbg_perror(f, args...)  _dbg_at(FBI_LOG_ERROR, "error", 0, f ": %s", \
                                        ##args, strerror(errno))
#define dbg_critical(f, args...) _dbg_at(FBI_LOG_CRITICAL, "critical", 0, f, ##args)
#define dbg_error(f, args...) _dbg_at(FBI_LOG_ERROR, "error", 0, f, ##args)
#define dbg_warning(f, args...) _dbg_at(FBI_LOG_WARNING, "warning", 0, f, ##args)
#define dbg_notify(f, args...) _dbg_at(FBI_LOG_NOTIFY, "notify", 0, f, ##args)
#define dbg_info(f, args...) _dbg_at(FBI_LOG_INFO, "info", 0, f, ##args)
#define dbg_debug(f, args...) _dbg_at(FBI_LOG_DEBUG, "debug", 0, f, ##args)
#define dbg_spew(f, args...) _dbg_at(FBI_LOG_SPEW, "spew", 0, f, ##args)
#define dbg_low(f, args...) _dbg_at(FBI_LOG_LOW, "low", 0, f, ##args)
#define dbg_medium(f, args...) _dbg_at(FBI_LOG_MEDIUM, "medium", 0, f, ##args)
#define dbg_high(f, args...) _dbg_at(FBI_LOG_HIGH, "high", 0, f, ##args)


__END_DECLS

#endif
