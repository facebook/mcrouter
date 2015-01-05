/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "mc_fbtrace_info.h"
#include "mcrouter/lib/fbi/debug.h"

static mc_fbtrace_t* mc_fbtrace_incref(mc_fbtrace_t* fbt) {
  FBI_ASSERT(fbt);
  int newrefcount = __sync_add_and_fetch(&fbt->_refcount, 1);
  FBI_ASSERT(newrefcount > 0);
  return fbt;
}

static void mc_fbtrace_decref(mc_fbtrace_t* fbt) {
  FBI_ASSERT(fbt);
  int newrefcount = __sync_add_and_fetch(&fbt->_refcount, -1);
  FBI_ASSERT(newrefcount >= 0);
  if (newrefcount == 0) {
    free(fbt);
  }
}

static mc_fbtrace_t* new_mc_fbtrace() {
  mc_fbtrace_t* fbt = calloc(1, sizeof(*fbt));
  if (fbt == NULL) {
    dbg_debug("Error while allocating memory for mc_fbtrace_t object");
    return fbt;
  }
  mc_fbtrace_incref(fbt);
  return fbt;
}

mc_fbtrace_info_t* new_mc_fbtrace_info(int is_copy) {
  mc_fbtrace_info_t* fbt_i = calloc(1, sizeof(*fbt_i));
  if (fbt_i == NULL) {
    dbg_debug("Error while allocating memory for mc_fbtrace_info_t object");
    return fbt_i;
  }
  fbt_i->_refcount = 1;
  if (!is_copy) {
    fbt_i->fbtrace = new_mc_fbtrace();
    if (fbt_i->fbtrace == NULL) {
      free(fbt_i);
      return NULL;
    }
  }
  return fbt_i;
}

mc_fbtrace_info_t* mc_fbtrace_info_deep_copy(const mc_fbtrace_info_t* orig) {
  mc_fbtrace_info_t* new_copy = new_mc_fbtrace_info(1);
  memcpy(new_copy, orig, sizeof(mc_fbtrace_info_t));
  if (orig->fbtrace) {
    mc_fbtrace_incref(orig->fbtrace);
  }
  return new_copy;
}

void mc_fbtrace_info_decref(mc_fbtrace_info_t* fbt_i) {
  if (!fbt_i) {
    return;
  }
  int newrefcount = __sync_add_and_fetch(&fbt_i->_refcount, -1);
  FBI_ASSERT(newrefcount >= 0);
  if (newrefcount == 0) {
    if (fbt_i->fbtrace) {
      mc_fbtrace_decref(fbt_i->fbtrace);
    }
    free(fbt_i);
  }
}

mc_fbtrace_info_t* mc_fbtrace_info_incref(mc_fbtrace_info_t* fbt_i) {
  if (!fbt_i) {
    return NULL;
  }
  int newrefcount = __sync_add_and_fetch(&fbt_i->_refcount, 1);
  FBI_ASSERT(newrefcount > 0);
  return fbt_i;
}
