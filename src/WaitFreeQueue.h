/* Copyright (c) 2018 Stanford University
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR(S) DISCLAIM ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL AUTHORS BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#ifndef WAIT_FREE_QUEUE_H
#define WAIT_FREE_QUEUE_H

#include "mpscq.h"

/**
 * A wrapper around Daniel Bittman's implementation of an MPSCQ.
 */
class WaitFreeQueue {
  public:
    bool enqueue(void* item) { return mpscq_enqueue(internalQueue, item); }

    bool dequeue(void** item) {
        *item = mpscq_dequeue(internalQueue);
        return *item != NULL;
    }

    bool contains(void* item) { return mpscq_contains(internalQueue, item); }

    size_t size() { return mpscq_count(internalQueue); }

    bool empty() { return size() == 0; }

    explicit WaitFreeQueue(size_t capacity) {
        internalQueue = mpscq_create(NULL, capacity);
    }
    ~WaitFreeQueue() { mpscq_destroy(internalQueue); }

  private:
    struct mpscq* internalQueue;
};

#endif
