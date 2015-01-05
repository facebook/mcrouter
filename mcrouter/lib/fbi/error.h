/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#ifndef FBI_ERROR_H
#define FBI_ERROR_H

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "mcrouter/lib/fbi/decls.h"
#include "mcrouter/lib/fbi/nstring.h"
#include "mcrouter/lib/fbi/time.h"

__BEGIN_DECLS

// Store no more than 1024 of these things to avoid memory bloat in
// failure cases
#define MAX_NUMBER_OF_STORED_ERRORS 1024

typedef enum fbi_errtype_e {
  fbi_no_error = 0,
  fbi_sys_error = 1,
  fbi_app_error = 2,
  fbi_remote_error = 3,
  fbi_unknown_error = 4,
} fbi_errtype_t;

static inline const char* fbi_errtype_to_string(const fbi_errtype_t type) {
  static const char* strings[] = {
    "no-error",
    "sys-error",
    "app-error",
    "remote-error",
    "unknown_error"};

  return strings[type <= fbi_unknown_error ? type : fbi_unknown_error];
}

/** Memcache error record. */

typedef struct fbi_err_s {

  fbi_errtype_t type; //!< Type of error record

  timeval_t timestamp;

  /** For system errors this is the system errno value
      For other errors is is an appliation specfic code. */

  int code; //!< Error code.

  int lineno; //!< Source code line number

  const char* source; //!< Source file name

  nstring_t message; //!< Default error message

} fbi_err_t;

void fbi_add_err(const char* source,
                 const int lineno,
                 const fbi_errtype_t type,
                 const int code,
                 const char* fmt,
                 ...);

size_t fbi_get_nerrs();

fbi_err_t* fbi_get_err();

fbi_err_t* fbi_get_last_err();

void fbi_clear_err(fbi_err_t* err);

// Make the err parameter const so they don't do something stupid like free it
typedef void (*fbi_err_flush_cb)(const fbi_err_t *err);

// Set the callback for flushing errors
// This should be set only by the final consumer (i.e. mcrouter, not libmc)
void fbi_set_err_flush_cb(fbi_err_flush_cb cb);

#define FBI_SYSERR(m, args...) \
  (1 ? fbi_add_err(__FILE__, __LINE__, fbi_sys_error, errno, m, ##args) : \
   printf(m, ##args))

#define FBI_SOCKERR(e, m, args...) \
  (1 ? fbi_add_err( __FILE__, __LINE__, fbi_sys_error, (e), m, ##args) : \
   printf(m, ##args))

#define FBI_APPERR(e, m, args...) \
  (1 ? fbi_add_err( __FILE__, __LINE__, fbi_app_error, (e), m, ##args) : \
   printf(m, ##args))

#define FBI_REMOTEERR(e, m, args...) \
  (1 ? fbi_add_err(__FILE__, __LINE__, fbi_remote_error, (e), m, ##args) : \
   printf(m, ##args))

__END_DECLS

#endif
