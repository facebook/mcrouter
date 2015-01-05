/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
/** This sample code shows how to create, retrieve and clear error message
    using libfbi's error service.  libfbi creates an error-log context for
    each thread, thus no locks are required either in the error log
    implementation or calling application.

    Example:

    $ ./thread-errs
*/


#include <stdio.h>
#include <fbi/error.h>
#include <fbi/time.h>

#include <pthread.h>

typedef struct blob_s {
  int id;
  int count;
  timeval_t delay;
  char* str;
} blob_t;

static void* err_thread(void* arg) {
  blob_t* blob = (blob_t*)arg;
  int ix = 0;

  fbi_err_t* err;

  for(ix = 0; ix < blob->count; ix++) {
    FBI_APPERR(0x80000001, "thread-%u %u %s", blob->id, ix, blob->str);
  }

  // dump errors

  while((err = fbi_get_err())) {
    printf("ts: %u type: %s code: %u source: %s lineno: %u message %s\n",
           timeval_ms(&err->timestamp),
           fbi_errtype_to_string(err->type),
           err->code,
           err->source,
           err->lineno,
           err->message.str);

    fbi_clear_err(err);
  }

  return blob;
}

int main(int argc, char** argv) {

  pthread_t threads[16] ;
  pthread_attr_t  attrs[16];
  blob_t blobs[16];
  int ix;
  int count = 100;
  int result = 0;

  for(ix = 0; ix < 16; ix++) {
    blobs[ix].id = ix;
    blobs[ix].count = count;
    blobs[ix].str = "snarf snarf";

    pthread_attr_init(&attrs[ix]);

    if ((result = pthread_create(&threads[ix], &attrs[ix],
                                 err_thread, &blobs[ix])) != 0) {
      fprintf(stderr, "Can't create thread: %s\n",
              strerror(result));
      break;
    }
  }

  for(ix = 0; ix < 16; ix++) {
    void* result;
    pthread_join(threads[ix], &result);
  }

  return result;
}
