/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#ifndef MCROUTER_LIB_FBI_ASOX_TIMER_H
#define MCROUTER_LIB_FBI_ASOX_TIMER_H

#include <event.h>

#include "mcrouter/lib/fbi/decls.h"

__BEGIN_DECLS

typedef void* asox_timer_t;

/**
 * Timer event callback
 */
typedef void (asox_on_timer_t)(const asox_timer_t timer, void* arg);

/**
 * Add a timer event.  Note that 'time' is a timeout value, not absolute time
 */
asox_timer_t asox_add_timer(struct event_base* event_base,
                            const struct timeval time,
                            asox_on_timer_t* on_timer,
                            void* arg);

/**
 * Remove a timer event.
 */
void asox_remove_timer(asox_timer_t timer);

__END_DECLS

#endif
