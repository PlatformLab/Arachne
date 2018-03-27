/* Copyright (c) 2015-2018 Stanford University
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

#ifndef ARACHNE_COMMON_H
#define ARACHNE_COMMON_H

#include <stdint.h>
#include <stdlib.h>
#include <atomic>

// This file exists to resolve circular dependencies.

namespace Arachne {

#define CACHE_LINE_SIZE 64
#define PAGE_SIZE 4096

// The following macros issue hints to the compiler that a particular branch is
// more likely than another.
#ifdef __GNUC__
// Note that the double negation here is used for coercion to the boolean type.
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#else
#define likely(x) (x)
#define unlikely(x) (x)
#endif

// Largest number of Arachne threads that can be simultaneously created on each
// core.
const int maxThreadsPerCore = 56;

struct ThreadContext;
struct MaskAndCount;
/**
 * This class holds all the state associated with a particular core in Arachne.
 */
struct Core {
    /**
     * Alert the kernel thread that cleanup is complete and it should block for
     * ramp-down.
     */
    bool threadShouldYield;

    /**
     * This pointer allows fast access to the current kernel thread's
     * localThreadContexts without computing an offset from the global
     * allThreadContexts vector on each access.
     */
    ThreadContext** localThreadContexts;

    /**
     * Holds the identifier for the thread in which it is stored: allows each
     * kernel thread to identify itself. This should eventually become a
     * coreId, when we support multiple kernel threads per core to handle
     * blocking system calls.
     */
    int kernelThreadId = -1;

    /**
     * This is the context that a given kernel thread is currently executing.
     */
    ThreadContext* loadedContext;

    /**
     * See documentation for MaskAndCount.
     */
    std::atomic<MaskAndCount>* localOccupiedAndCount;

    /**
     * A bit is set to prevent migration; this should be set before the
     * occupied flag is cleared.
     */
    std::atomic<uint64_t>* localPinnedContexts;

    /**
     * This represents each core's local copy of the high-priority mask. Each
     * call to dispatch() will first examine this bitmask. It will clear the
     * first set bit and switch to that context. If there are no set bits, it
     * will copy the current value of publicPriorityMasks for the current core
     * to here, and then atomically clear those bits using an atomic OR.
     *
     * When ramping down cores, this value (if nonzero) should be cleared,
     * since all non-terminated threads on this core will be migrated away
     * from this thread.
     */
    uint64_t privatePriorityMask;

    /**
     * This variable holds the index into the current kernel thread's
     * localThreadContexts that it will check first the next time it looks for
     * a thread to run. It is used to implement round-robin scheduling of
     * Arachne threads.
     */
    uint8_t nextCandidateIndex = 0;

    /**
     * This is the highest-indexed context known to be occupied by the dispatch
     * loop of a given core.
     */
    uint8_t highestOccupiedContext;
};

void* alignedAlloc(size_t size, size_t alignment = CACHE_LINE_SIZE);
}  // namespace Arachne

#endif
