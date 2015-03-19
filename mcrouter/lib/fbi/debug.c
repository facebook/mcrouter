/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
/** Debug Utilities

    Author:
    Marc Kwiatkowski
    Tony Tung
    $Id:$ */

#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/time.h>
#include <pthread.h>

#include "debug.h"
#define SZ_FORMAT_STR 2048

static volatile assert_hook_fn _assert_hook;

void fbi_set_assert_hook(assert_hook_fn assert_hook) {
  _assert_hook = assert_hook;
}

void _assert_func(const char *assertion,
                  const char *file,
                  int line,
                  const char *func) {
#define ASSERT_FORMAT "%s:%d: %s: Assertion `%s' failed."
  static __thread int tls_inside_assert_hook;
  int len = fprintf(stderr, ASSERT_FORMAT "\n", file, line, func, assertion);
  char *buf = (char*) alloca(len);
  snprintf(buf, len, ASSERT_FORMAT, file, line, func, assertion);
  assert_hook_fn hook = _assert_hook;
  if (hook && ! tls_inside_assert_hook) {
    tls_inside_assert_hook = 1;
    (*hook)(buf);
    tls_inside_assert_hook = 0;
  }
  abort();
#undef ASSERT_FORMAT
}

FILE* dbg_log_file = NULL; // Not static for testing (heisenberg_tao_shims.cpp)
static uint dbg_level = FBI_LOG_DEFAULT;
static nstring_t* dbg_log_fname = NULL;
static fbi_date_format_t dbg_date_format = fbi_date_default;

#if defined(HAVE_EVENT_LOG)
extern void event_set_logfile(FILE* value);

static void
event_logger(int severity, const char* msg) {
  timeval_t now;
  if (dbg_log_file == NULL) {
    return;
  }

  gettimeofday(&now, NULL);
  fprintf(dbg_log_file, "[%10lu.%06lu] [%s]: %s\n",
          now.tv_sec, (unsigned long) now.tv_usec,
          "event", msg);
}
#endif /* defined(HAVE_EVENT_LOG) */

void dbg_exit() {
  if (dbg_log_file != NULL && dbg_log_file != stderr) {
    fclose(dbg_log_file);
    dbg_log_file = NULL;
  }
}

static FILE* dbg_init_0() {

  if (dbg_log_fname == NULL || dbg_log_fname->len == 0) {
    return stderr;
  }

  /* Copy dbg_log_fname to fname, replacing each "%pid" in the
     former with the current process ID. */
  char fname[MAXPATHLEN + 1];
  int si = 0;
  int di = 0;
  while (si < dbg_log_fname->len) {
    if (si + 4 <= dbg_log_fname->len &&
        !memcmp(dbg_log_fname->str + si, "%pid", 4)) {
      di += snprintf(fname + di, sizeof(fname) - di, "%u", getpid());
      si += 4;
    } else {
      fname[di++] = dbg_log_fname->str[si++];
    }
    if (di >= sizeof(fname)) {
      return stderr;
    }
  }
  fname[di] = '\0';

  FILE *result = fopen(fname, "a");
  if (result == NULL) {
    timeval_t now;
    gettimeofday(&now, 0);
    fprintf(stderr, "[%10lu.%06lu] [%s] failed to open %s %u\n",
            now.tv_sec, (unsigned long) now.tv_usec, "init",
            fname, errno);

    return stderr;
  }

  atexit(dbg_exit);

#if defined(HAVE_EVENT_LOG)
  event_set_log_callback(event_logger);
#endif /* defined(HAVE_EVENT_LOG) */

  return result;
}

static FILE *dbg_init() {
  FILE *f = dbg_init_0();
  if (f == stderr) {
    /* Ensure that data logged to stdout is line-buffered, just like stderr.
       Without this, when a test writes progress- and diag-related info to
       stdout, and a test fails, the resulting diagnostic(on stderr) would
       end up being output right away, usually long before the previously
       printf'd output lines that were buffered to stdout.  That made
       interpreting failure logs, um... unnecessarily challenging.

       This comes with caveats: line-buffering stdout means if you write
       a lot of data to stdout, with line-buffering, it will be far less
       efficient, but I've been assured that we write very little to
       standard output, so this should affect only the tests, as desired.

       The problem can arise only when logging to stderr, so take the
       hit only in that case.  */
    setlinebuf(stdout);
  } else {
    setlinebuf(f);
  }
  return f;
}

/* Print current date/time into str of size str_sz using fmt. */
static void format_date(char *str, const uint str_sz,
                        const fbi_date_format_t date_format,
                        timeval_t *now)
{
  struct tm* now_tm;

  switch (date_format) {
    case fbi_date_utc: /* Year-Month-Day Hour:Minute:Seconds.Microseconds */
    now_tm = gmtime(&now->tv_sec);
    snprintf(str, str_sz, "[%4d-%02d-%02d %02d:%02d:%02d.%06lu UTC]",
            now_tm->tm_year + 1900, now_tm->tm_mon + 1, now_tm->tm_mday,
            now_tm->tm_hour, now_tm->tm_min, now_tm->tm_sec,
            (unsigned long)now->tv_usec);
    break;
  case fbi_date_local: /* Year-Month-Day Hour:Minute:Seconds.Microseconds */
    now_tm = localtime(&now->tv_sec);
    snprintf(str, str_sz, "[%4d-%02d-%02d %02d:%02d:%02d.%06lu]",
            now_tm->tm_year + 1900, now_tm->tm_mon + 1, now_tm->tm_mday,
            now_tm->tm_hour, now_tm->tm_min, now_tm->tm_sec,
            (unsigned long)now->tv_usec);
    break;
  case fbi_date_unix: /* SecondsSinceEpoch.MicrosecondsSinceEpoch */
  default:
    snprintf(str, str_sz, "[%10lu.%06lu]", now->tv_sec,
             (unsigned long) now->tv_usec);
    break;
  }
}

/* Print given debug level and type into str of size str_sz. */
static void format_level(char *str, const uint str_sz, const char* type)
{
  snprintf(str, str_sz, "%s", type ?: "");
}

struct msg_info {
  const char *component;
  const char *function;
  const char *type;
  int line;
  int suppressed;
  int repeated;
  char msg[SZ_FORMAT_STR];
  timeval_t last_time;
  float backoff;
};

/* catch repeated log messages (per-thread tracked by @info) within an
 * exponential backoff window
 */
#define MAXIMUM_BACKOFF 60.0 /* seconds */
static int ratelimit(struct msg_info *info, timeval_t *now, int same_loc,
                     const char* msg)
{
#define msec 1e-3
#define usec 1e-6
  int repeat = 0;

  if (same_loc && strcmp(msg, info->msg) == 0) {
    float elapsed = (now->tv_sec + now->tv_usec * usec) -
      (info->last_time.tv_sec + info->last_time.tv_usec * usec);

    if (elapsed >= info->backoff) {
      if (info->backoff < MAXIMUM_BACKOFF)
        info->backoff *= 2;
    } else {
      repeat = 1;
    }
  } else {
    // different msg, reset
    info->backoff = 1 * msec;
  }

  info->repeated += repeat;

  return repeat;
#undef msec
#undef usec
}

static const char *component_to_file(const char *component)
{
  const char *file;

  if ( (file = strrchr(component, '/')) ) {
    file++;
  } else if ( (file = strrchr(component, '\\')) ) {
    file++;
  } else {
    file = component;
  }

  return file;
}

void fbi_dbg_log(const char *principal, const char* component,
                 const char* function, const int line,
                 const char* type, uint level, const int indent_offset,
                 const char* format, ...) {
  va_list ap;
  const char* file;
  static __thread struct msg_info last = { .backoff = 1e-3, };
  static __thread uint tls_indent_level = 0;
  char formatted_str[SZ_FORMAT_STR];
  char level_str[256];
  char date_str[256];
  timeval_t now;
  int same_loc;

  if (level > dbg_level) {
    return;
  }

  if (indent_offset < 0) {
    FBI_ASSERT(tls_indent_level >= -indent_offset);
    if (tls_indent_level < -indent_offset) {
        tls_indent_level = 0;
    } else {
        tls_indent_level += indent_offset;
    }
  }

  va_start(ap, format);

  if (dbg_log_file == NULL) {
    if ((dbg_log_file = dbg_init()) == NULL) {
      return;
    }
  }

  if (vsnprintf(formatted_str, sizeof(formatted_str), format, ap) < 0) {
    formatted_str[0] = 0;
  }

  pthread_t tid = pthread_self();

  gettimeofday(&now, NULL);
  same_loc = last.component == component && last.function == function &&
             last.line == line;
  if (ratelimit(&last, &now, same_loc, formatted_str))
        goto epilogue;

  if (last.suppressed) {
    format_date(date_str, 256, dbg_date_format, &now);
    format_level(level_str, 256, last.type);
    fprintf(dbg_log_file, "%20lu %s [%7s] (%8s) %*s"
            "suppressed %d message%s\n",
            tid, date_str, level_str, principal, tls_indent_level, "",
            last.suppressed, last.suppressed > 1 ? "s" : "");
    last.suppressed = 0;
  }

  if (last.repeated) {
    file = component_to_file(last.component);
    format_date(date_str, 256, dbg_date_format, &now);
    format_level(level_str, 256, last.type);
    fprintf(dbg_log_file, "%20lu %s [%7s] (%8s) %*s%s:%u %s "
            "last message repeated %d time%s\n",
            tid, date_str, level_str, principal, tls_indent_level, "",
            file, last.line, last.function,
            last.repeated, last.repeated> 1 ? "s" : "");
    last.repeated = 0;
  }

  file = component_to_file(component);
  format_level(level_str, 256, type);
  format_date(date_str, 256, dbg_date_format, &now);

  last.component = component;
  last.function = function;
  last.last_time = now;
  last.line = line;
  last.type = type;
  strcpy(last.msg, formatted_str);

  fprintf(dbg_log_file, "%20lu %s [%7s] (%8s) %*s%s:%u %s %s\n",
          tid, date_str, level_str, principal, tls_indent_level, "",
          file, line, function, formatted_str);
epilogue:
  if (indent_offset > 0) {
    tls_indent_level += indent_offset;
  }

  va_end(ap);
}

uint fbi_get_debug() {return dbg_level;}
void fbi_set_debug(const uint level) {dbg_level = level;}

const nstring_t* fbi_get_debug_logfile() {return dbg_log_fname;}
void fbi_set_debug_logfile(const nstring_t* value) {
  if (dbg_log_fname != NULL) {
    nstring_del(dbg_log_fname);
    dbg_log_fname = NULL;
  }

  dbg_log_fname = nstring_dup(value);
  dbg_exit();
  dbg_log_file = dbg_init();

#if defined(HAVE_EVENT_LOG)
  event_set_logfile(dbg_log_file);
#endif /* defined(HAVE_EVENT_LOG) */
}

void fbi_set_debug_date_format(fbi_date_format_t fmt) {
  if ((fmt < fbi_date_max) && (fmt > 0)) {
    dbg_date_format = fmt;
  }
}
