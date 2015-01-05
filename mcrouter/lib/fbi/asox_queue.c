/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "asox_queue.h"

#include <stdbool.h>
#include <stdlib.h>
#include "mcrouter/lib/fbi/asox_semaphore.h"
#include "mcrouter/lib/fbi/debug.h"
#include "mcrouter/lib/fbi/waitable_count.h"

struct asox_queue_s {
  waitable_counter_t use_count;
  asox_sem_t sem;
  bool inside_on_elem_ready;
  bool disconnect_deferred;
  asox_queue_entry_t *local_queue;
  const asox_queue_callbacks_t *callbacks;
  void *arg;
  unsigned at_once;
  asox_queue_entry_t * volatile stack;
};

#define ENTRY_SENTINEL ((asox_queue_entry_t*)1)

/**
 * This routine returns a list of entries ready to be processed by the worker
 * thread. Given that the entries are pushed in reverse order (i.e., they form
 * a stack), this routine first retrieves the stack, reverses the elements,
 * and then return them.
 */
static asox_queue_entry_t *fetch_queue(asox_queue_t q,
                                       asox_queue_entry_t *new_head) {
  asox_queue_entry_t *e;
  asox_queue_entry_t *r;
  asox_queue_entry_t *l;

  if ((q->stack == NULL) || (q->stack == ENTRY_SENTINEL)) {
    return NULL;
  }

  r = NULL;
  e = __sync_lock_test_and_set(&q->stack, new_head);
  while (e) {
    l = e;
    e = e->next;

    l->next = r;
    r = l;
  }

  return r;
}

/**
 * This routine is called when a queue is being cleaned up. It goes through
 * all remaining elements and calls the sweep callback on them.
 */
static bool asox_queue_sweep(asox_queue_t q,
                             asox_queue_entry_t *e,
                             unsigned dec,
                             asox_queue_entry_t *new_head) {
  int must_free;
  asox_queue_entry_t *next;
  unsigned i;
  bool done;

  for (i = 0; e || (e = fetch_queue(q, new_head)); i++) {
    next = e->next;
    must_free = e->flags & ASOX_QUEUE_ENTRY_DELETE;
    if (q->callbacks->on_elem_sweep) {
      q->callbacks->on_elem_sweep(q, e, q->arg);
    }
    if (must_free) {
      free(e);
    }
    e = next;
  }

  q->local_queue = NULL;
  dec += i;
  done = waitable_counter_count(&q->use_count) == dec;

  /*
   * If we've processed all entries, we remove the semaphore from the
   * event base.
   */
  if (done) {
    asox_sem_remove_event(q->sem);
  }

  waitable_counter_dec(&q->use_count, dec);

  return done;
}

/**
 * This routine is called when the queue is notified, via the semaphore, that
 * there is work to be done (either process or sweep entries).
 */
static bool queue_on_signal(asox_sem_t sem, uint64_t n, void *arg) {
  int must_free;
  struct asox_queue_s *q = (struct asox_queue_s*) arg;
  asox_queue_entry_t *e;
  asox_queue_entry_t *next;
  size_t i;

  FBI_ASSERT(!q->inside_on_elem_ready);

  e = q->local_queue;
  for (i = q->at_once; i && !waitable_counter_is_stopped(&q->use_count); i--) {
    /*
     * If we don't have a local list anymore, try to fetch it from the
     * shared queue.
     */
    if (!e && !(e = fetch_queue(q, NULL))) {
      break;
    }

    next = e->next;
    must_free = e->flags & ASOX_QUEUE_ENTRY_DELETE;
    q->inside_on_elem_ready = true;
    q->callbacks->on_elem_ready(q, e, q->arg);
    q->inside_on_elem_ready = false;
    if (must_free) {
      free(e);
    }
    e = next;
  }

  bool ret = true;
  /*
   * If the queue is disconnected, we must sweep all remaining items. It is
   * essential that we return 'false' when we've processed all entries because
   * by now we will have woken up any potential waiters, who may in turn free
   * the queue, so it is not safe to touch it.
   */
  if (waitable_counter_is_stopped(&q->use_count)) {
    ret = !asox_queue_sweep(q, e, q->at_once - i, NULL);
  } else {
    /*
     * Store the left-overs in the local queue for the next time. And if there
     * actually is at least one left-over entry, we enqueue ourselves to run
     * again to ensure that we do get woken up to process the remaining items.
     */
    waitable_counter_dec(&q->use_count, q->at_once - i);
    q->local_queue = e;
    if (e) {
      asox_sem_signal(q->sem, 1);
      ret = false;
    }
  }

  if (q->disconnect_deferred) {
    asox_queue_disconnect(q);
    q->disconnect_deferred = false;
  }

  return ret;
}

/**
 * This routine releases all resources associated with the given asox queue,
 * it should only be called after a successfull disconnect.
 */
static void asox_queue_free(asox_queue_t q) {
  if (q->sem) {
    asox_sem_del(q->sem);
  }
  free(q);
}

/**
 * This routine creates a new asox queue and returns it to the caller.
 */
asox_queue_t asox_queue_init(struct event_base* base,
                             int event_priority,
                             int num_priority_levels,
                             uint32_t max_length,
                             uint32_t at_once,
                             const asox_queue_callbacks_t *callbacks,
                             asox_queue_flags_t flags, void *arg) {
  static asox_sem_callbacks_t const sem_callbacks = {
    .on_signal = queue_on_signal,
  };
  struct asox_queue_s *q = calloc(1, sizeof(*q));
  if (!q) {
    return NULL;
  }

  // create a sem associated with this queue
  // (see the comment in queue_on_signal of why ASOX_SEM_MULTI is used)
  q->sem = asox_sem_new(base, &sem_callbacks, event_priority,
                        (asox_sem_flags_t)ASOX_SEM_MULTI, (void*) q);
  if (!q->sem) {
    free(q);
    return NULL;
  }

  waitable_counter_init(&q->use_count, max_length);

  q->callbacks = callbacks;
  q->arg = arg;
  q->at_once = at_once ? at_once : UINT_MAX;
  q->inside_on_elem_ready = false;
  q->disconnect_deferred = false;

  return q;
}


/**
 * This routine disconnects the given queue, that is, any subsequent attempt
 * to enqueue entries will fail and already queued entries will be flushed.
 *
 * N.B. This routine can only be safely called from the thread processing the
 *      event base. The only exception is when there is no thread processing
 *      the event base, in which case this routine is safe to be called from
 *      one arbitrary thread.
 */
void asox_queue_disconnect(asox_queue_t q) {
  bool b;

  if (q->inside_on_elem_ready) {
    q->disconnect_deferred = true;
    return;
  }

  /* Prevent new requests from being queued. */
  waitable_counter_stop(&q->use_count);

  /*
   * Take care of all the remaining entries.
   *
   * N.B. If another thread was halfway through enqueueing, the count might not
   *      be zero yet, so we have to wait for it. It will become zero when the
   *      enqueueing thread notices that the queue was disconnected and drops
   *      the usage_count (before failing the enqueue).
   *
   *      We put a timeout on the wait so that we can catch errors in core
   *      dumps rather than wait indefinitely.
   */
  b = asox_queue_sweep(q, q->local_queue, 0, ENTRY_SENTINEL);
  if (!b) {
    waitable_counter_wait(&q->use_count, 30000);
    b = asox_queue_sweep(q, q->local_queue, 0, ENTRY_SENTINEL);
    FBI_ASSERT(b);
  }
}

/**
 * This routine is similar to asox_queue_disconnect(), except that it can only
 * be called from a "remote" thread (i.e., a thread other than the owner of the
 * event base).
 *
 * N.B. This routine cannot be safely called from the event base thread. If
 *      there are pending entries in the queue, this routine will deadlock
 *      because it will wait for them to be flushed before continuing, but
 *      they won't be flushed because the thread is sleeping.
 */
void asox_queue_remote_disconnect(asox_queue_t q) {
  waitable_counter_stop(&q->use_count);
  asox_sem_signal(q->sem, 1);
  waitable_counter_wait(&q->use_count, -1);
}

/**
 * This routine determines if the given queue is disconnected.
 */
int asox_queue_is_disconnected(asox_queue_t q) {
  return waitable_counter_is_stopped(&q->use_count);
}

/**
 * This routine frees up all resources associated with the given queue.
 *
 * N.B. If called from a "remote" thread, this function must be preceded by a
 *      call to asox_queue_remote_disconnect() to ensure that all work that
 *      needs to be peformed in the event base thread has already been
 *      performed.
 */
void asox_queue_del(asox_queue_t q) {
  if (q) {
    asox_queue_disconnect(q);
    asox_queue_free(q);
  }
}

/**
 * This routine atomically enqueues entries to the given asox queue.
 */
static int asox_queue_multi_enqueue_nocopy(asox_queue_t queue,
                                           asox_queue_entry_t **e,
                                           size_t nentries) {
  size_t i;
  asox_queue_entry_t *head;
  asox_queue_entry_t *tail;
  asox_queue_entry_t *qhead;
  asox_queue_entry_t *latest;

  /*
   * Try to reserve a quota before adding entries to the queue. This can fail
   * either because we'd go over the limit (if there is one) or because the
   * queue is disconnected.
   */
  if (!waitable_counter_inc(&queue->use_count, nentries)) {
    errno = ENOSPC;
    return 0;
  }

  /* Prepare a reversed list for pushing into the stack. */
  head = e[nentries - 1];
  tail = e[0];
  for (i = nentries - 1; i > 0; i--) {
    e[i]->next = e[i - 1];
  }

  /*
   * Push the new entries into the stack.
   *
   * N.B. If we find a sentinel, the queue was disconnected before we could
   *      push the entries but after we incremented the use-count; in that case
   *      fail the request after decrementing the use-count back.
   */
  latest = queue->stack;
  do {
    qhead = latest;
    if (qhead == ENTRY_SENTINEL) {
      waitable_counter_dec(&queue->use_count, nentries);
      errno = ENOSPC;
      return 0;
    }
    tail->next = qhead;
    latest = __sync_val_compare_and_swap(&queue->stack, qhead, head);
  } while (latest != qhead);

  /*
   * Wake up worker thread if the queue was previously empty. The reader clears
   * the queue on each wake up - this means only one notification is required
   * per flush, regardless of how full the queue is.
   */
  if (latest == NULL) {
    asox_sem_signal(queue->sem, 1);
  }

  return 1;
}

int asox_queue_enqueue(asox_queue_t queue,
                       asox_queue_entry_t *entry) {
  return asox_queue_multi_enqueue(queue, entry, 1);
}

int asox_queue_enqueue_nocopy(asox_queue_t queue,
                              asox_queue_entry_t *entry) {
  entry->flags = 0;
  return asox_queue_multi_enqueue_nocopy(queue, &entry, 1);
}

int asox_queue_multi_enqueue(asox_queue_t queue,
                             asox_queue_entry_t *entries,
                             size_t nentries) {
  uint32_t i, j;
  asox_queue_entry_t *scratch[100];
  asox_queue_entry_t **wrappers;

  // do all of the malloc-ing outside the lock to reduce contention
  if (nentries <= sizeof(scratch)/sizeof(scratch[0])) {
      wrappers = scratch;
  } else {
      wrappers = malloc(sizeof(wrappers[0]) * nentries);
      if (wrappers == NULL) {
          // errno is ENOMEM
          return 0;
      }
  }
  for (i = 0; i < nentries; i++) {
    wrappers[i] = malloc(sizeof(*wrappers[0]));
    if (wrappers[i] == NULL) {
      for (j = 0; j < i; j++) {
        free(wrappers[j]);
      }
      if (wrappers != scratch) {
          free(wrappers);
      }
      errno = ENOMEM;
      return 0;
    }
    *wrappers[i] = entries[i];
    wrappers[i]->flags = ASOX_QUEUE_ENTRY_DELETE;
  }

  if (!asox_queue_multi_enqueue_nocopy(queue, wrappers, nentries)) {
      for (i = 0; i < nentries; i++) {
        free(wrappers[i]);
      }
      if (wrappers != scratch) {
          free(wrappers);
      }
      return 0;
  }

  if (wrappers != scratch) {
      free(wrappers);
  }

  return nentries;
}
