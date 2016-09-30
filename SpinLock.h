/* Copyright (c) 2015-2016 Stanford University
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

#ifndef ARACHNE_SPINLOCK_H
#define ARACHNE_SPINLOCK_H
#include <atomic>
#include "Common.h"

namespace Arachne {

/**
 * A simple SpinLock that occupies its own cache line to avoid cache thrashing
 * with neighboring data structures.
 */
class SpinLock {
 public:
    SpinLock() : state(false) {}
    ~SpinLock(){}
    void lock() {
        while (state.exchange(true, std::memory_order_acquire) != false);
    }

    bool try_lock() {
        // If the original value was false, then we successfully acquired the
        // lock.  Otherwise we failed.
        return !state.exchange(true, std::memory_order_acquire);
    }

    void unlock() {
        state.store(false, std::memory_order_release);
    }

 private:
    // Implements the lock: false means free, true means locked
    std::atomic<bool> state;

    // Pad this data structure out to a cache line size to mitigate false
    // sharing.
    char cachePad[CACHE_LINE_SIZE-sizeof(state)];
} __attribute__((aligned(CACHE_LINE_SIZE)));
} // namespace Arachne
#endif
