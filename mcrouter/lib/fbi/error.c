/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <assert.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>

#include "error.h"
#include "queue.h"

#define fbi_err_alloc(s) malloc(s)
#define fbi_err_free(b) free(b)

static pthread_key_t error_context_key;
static pthread_once_t error_context_key_once = PTHREAD_ONCE_INIT;

typedef struct fbi_err_enclosure_s {
  fbi_err_t err;
  TAILQ_ENTRY(fbi_err_enclosure_s) item;
} fbi_err_enclosure_t;

/* strictly speaking, we don't need this, but having this is more forgiving of
 * additions of fields before err. */
#define FBI_ERR_OFFSET                     ((intptr_t) &((fbi_err_enclosure_t*) 0)->err)

typedef TAILQ_HEAD(_errs_s, fbi_err_enclosure_s) _errs_t;

typedef struct _context_s {
  size_t nerrs;
  _errs_t errs;
} _context_t;

static void error_context_key_del(void* arg) {
  fbi_err_free(arg);
}

static void error_context_key_new() {
  (void)pthread_key_create(&error_context_key, error_context_key_del);
}

static inline _context_t* get_context() {
  _context_t* context;

  (void)pthread_once(&error_context_key_once, error_context_key_new);

  if ((context = (_context_t*)pthread_getspecific(error_context_key)) == NULL) {
    context = (_context_t*)fbi_err_alloc(sizeof(_context_t));
    TAILQ_INIT(&context->errs);
    context->nerrs = 0;
    (void)pthread_setspecific(error_context_key, context);
  }

  return context;
}


static inline fbi_err_enclosure_t* fbi_get_err_enclosure(fbi_err_t* err) {
  intptr_t addr = (intptr_t) err;
  fbi_err_enclosure_t* enclosure = (fbi_err_enclosure_t*) (addr - FBI_ERR_OFFSET);

  return enclosure;
}


static inline fbi_err_enclosure_t* fbi_err_new(const char* source,
                                               const int line,
                                               const fbi_errtype_t type,
                                               const int code,
                                               const char* message,
                                               const size_t nmessage) {
  fbi_err_enclosure_t* _err;
  fbi_err_t* err;

  size_t size = sizeof(fbi_err_enclosure_t) + nmessage + 1;

  if ((_err = fbi_err_alloc(size)) == NULL) {
    return _err;
  }
  memset(_err, '\0', size);

  err = &_err->err;
  gettimeofday(&err->timestamp, NULL);

  err->message.str = (char*)&_err[1];
  err->source = source;
  err->lineno = line;
  err->type = type;
  err->code = code;
  memcpy(err->message.str, message, nmessage);
  err->message.len = nmessage;
  return _err;
}

/** Frees an error object. */

static inline void fbi_err_del(fbi_err_enclosure_t* err) {
  fbi_err_free((void*)err);
}

static fbi_err_flush_cb flush_cb = NULL;

static void fbi_flush_errors() {
  _context_t* context = get_context();
  fbi_err_enclosure_t* _err = NULL;

  while ((_err = TAILQ_FIRST(&context->errs)) != NULL) {
    if (flush_cb != NULL) {
      flush_cb(&_err->err);
    }
    TAILQ_REMOVE(&context->errs, _err, item);
    context->nerrs--;
    fbi_err_del(_err);
  }
  FBI_ASSERT(context->nerrs == 0);
}

// Set the callback for flushing errors
// This should be set only by the final consumer (i.e. mcrouter, not libmc)
void fbi_set_err_flush_cb(fbi_err_flush_cb cb) {
  // We never want to overwrite ourselves.  If it's absolutely
  // necessary, set NULL first.
  FBI_ASSERT(flush_cb == NULL || cb == NULL);

  flush_cb = cb;
}

void fbi_add_err(const char* source,
                 const int line,
                 const fbi_errtype_t type,
                 const int code,
                 const char* format,
                 ...) {
  va_list ap;

  char buf[1024];
  int nbuf;
  fbi_err_enclosure_t* err;
  _context_t* context = get_context();

  va_start(ap, format);

  if ((nbuf = vsnprintf(buf, sizeof(buf) - 1, format, ap)) < 0) {
    goto epilogue;
  }

  err = fbi_err_new(source, line, type, code, &buf[0], nbuf);
  if (err == NULL) {
    goto epilogue;
  }

  TAILQ_INSERT_TAIL(&context->errs, err, item);
  context->nerrs++;

  if (context->nerrs > MAX_NUMBER_OF_STORED_ERRORS) {
    if (flush_cb != NULL) {
      fbi_flush_errors();
    } else {
      /* No callback, so just get and clear the first error */
      fbi_err_t *first_err = fbi_get_err();
      fbi_clear_err(first_err);
      first_err = NULL;
      FBI_ASSERT(context->nerrs <= MAX_NUMBER_OF_STORED_ERRORS);
    }
  }

epilogue:
  va_end(ap);
  return;
}

size_t fbi_get_nerrs() {

  _context_t* context = get_context();

  return context->nerrs;
}

fbi_err_t* fbi_get_last_err() {

  _context_t* context = get_context();
  fbi_err_enclosure_t* last = TAILQ_LAST(&context->errs, _errs_s);

  return &last->err;
}

fbi_err_t* fbi_get_err() {

  _context_t* context = get_context();
  fbi_err_enclosure_t* first = TAILQ_FIRST(&context->errs);

  return &first->err;
}

void fbi_clear_err(fbi_err_t* err) {
  _context_t* context = get_context();
  fbi_err_enclosure_t* _err = fbi_get_err_enclosure(err);

  if (err != NULL) {

    TAILQ_REMOVE(&context->errs, _err, item);

    context->nerrs--;

    fbi_err_del(_err);

  } else {
    fbi_flush_errors();
  }
}
