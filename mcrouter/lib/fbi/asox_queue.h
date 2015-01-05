/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#ifndef FB_MEMCACHE_ASOX_QUEUE_H
#define FB_MEMCACHE_ASOX_QUEUE_H 1

#include <event.h>
#include <stdint.h>
#include <sys/types.h>

#include "mcrouter/lib/fbi/decls.h"

__BEGIN_DECLS

/**
 * A signalling prioirty queue based on asox semaphores
 *
 * TOOD(rajeshn): clean up doc once ipc message queues
 *                are implemented
 *
 * This library presents a wrapper around TAILQs
 * that is threadsafe. It locks the queus before adding elements
 * into them. It uses asox semaphore implementation so that threads can
 * enqueue work and be signalled when a new item appears.
 * Because it is based on asox semaphores, the clients
 * can wait on events to be enqued *OR* network events.
 * Libevent will handle this multiplexing for us
 */

struct asox_queue_s;
typedef struct asox_queue_s* asox_queue_t;

/**
 * Flags that indicate the state of a queue entry. Currently
 * only "delete" is supported, and it indicates whether the
 * queue entry is to be deleted by the queue upon completion
 * or if left to the enqueuer.
 */
typedef enum {
  ASOX_QUEUE_ENTRY_DELETE = 0x01,
} asox_queue_entry_flags_t;

/**
 * basic queue entry object to be enqueued
 * and dequeued. Like libevent, the lower the priority number
 * the higher the priority. Thus 0 will be the highest priority.
 * the priority level must be >= 0 and < num_priority_levels
 * specified in asox_queue_init. The type field is an opaque
 * int that is ignored by the library and passed back for
 * convenience
 */
typedef struct asox_queue_entry_s {
  struct asox_queue_entry_s *next;
  void *data;
  size_t nbytes;
  int priority;
  int type;

  /**
   * For internal use of the queue implementation only, must be left
   * untouched.
   */
  asox_queue_entry_flags_t flags;

  /**
   * The time that entry was enqueued by asox.  This is set outside in order
   * to use the application specific clock.
   */
  uint64_t time_enqueued;
} asox_queue_entry_t;

/**
 * the type of the queue
 * Currently only ASOX_QUEUE_INTRA_PROCESS is supported
 */
typedef enum {
  ASOX_QUEUE_INTRA_PROCESS =  0x1,
  ASOX_QUEUE_INTER_PROCESS = 0x2,
} asox_queue_flags_t;

/**
 * Callback to be invoked when a new item is placed
 * on the queue. (this is invoked through asox_loop_once)
 */
typedef void (asox_queue_element_ready)(asox_queue_t queue,
                                        asox_queue_entry_t *entry, void *arg);

/**
 * This callback is invoked when the queue is being deleted and there are still
 * entries in the queue.
 */
typedef void (asox_queue_element_sweep)(asox_queue_t queue,
                                        asox_queue_entry_t *entry, void *arg);

typedef struct asox_queue_callbacks_s {
  asox_queue_element_ready* on_elem_ready;
  asox_queue_element_sweep* on_elem_sweep;
} asox_queue_callbacks_t;

/**
 * initialize a new queue
 *
 * @param base                the event base
 * @param event_priority      the priority of the event associated
 *                            with this queue
 * @param num_priority_levels number of different priority
 *                            levels for the queue
 * @param max_length          the maximum  number of entries
 *                            in the queue
 * @param at_once             the number of requests to process by
 *                            the callback before going to sleep
 * @param callbacks           pointer to callbacks to invoke
 *                            when something is added
 * @param arg                 Opaque context arg passed to callback
 */
asox_queue_t asox_queue_init(struct event_base* base,
                             int event_priority,
                             int num_priority_levels,
                             uint32_t max_length,
                             uint32_t at_once,
                             const asox_queue_callbacks_t* callbacks,
                             asox_queue_flags_t flags, void* arg);

/**
 * mark the queue disconnected. The queue should be marked disconnected
 * when the listener decides to stop processing the incoming message.
 * The queue entries that haven't been processed will be cleaned up
 * with the sweep callback.
 */
void asox_queue_disconnect(asox_queue_t queue);

/**
 * Similar to asox_queue_disconnect() above, except that it has to be
 * called from a "remote" thread, that is, a thread other than the one
 * processing events from the event base.
 */
void asox_queue_remote_disconnect(asox_queue_t q);

/**
 * check if queue is disconnected
 */
int asox_queue_is_disconnected(asox_queue_t queue);

/**
 * Delete the queue object and free associated memory
 */
void asox_queue_del(asox_queue_t queue);

/**
 * Enqueue a asox_queue_entry_t on the queue. This will
 * cause the callback associated with the queue to be invoked
 */
int asox_queue_enqueue(asox_queue_t queue,
                       asox_queue_entry_t *entry);

/**
 * Enqueue an asox_queue_entry_t on the queue, without making
 * a copy of the entry; that is, the caller must ensure that
 * the entry remains valid until its callback is called.
 */
int asox_queue_enqueue_nocopy(asox_queue_t queue,
                              asox_queue_entry_t *entry);

/**
 * Enqueue multiple asox_queue_entry_t's onto the queue.  Returns the
 * number of entries successfully enqueued, either 0 or nentries. Sets
 * errno to ENOMEM if enqueuing failed due to memory allocation failure,
 * ENOSPC if the queue max_length was reached.
 */
int asox_queue_multi_enqueue(asox_queue_t queue,
                             asox_queue_entry_t *entries,
                             size_t nentries);

__END_DECLS

#endif /*FB_MEMCACHE_ASOX_QUEUE_H*/
