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
#ifndef likely
#ifdef __GNUC__
// Note that the double negation here is used for coercion to the boolean type.
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#else
#define likely(x) (x)
#define unlikely(x) (x)
#endif
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
     * True means Arachne has finished migrating threads off this core and it's
     * now safe to call blockUntilCoreAvailable.
     */
    bool coreReadyForReturnToArbiter = false;

    /**
     * True means that this core has already been scheduled for descheduling,
     * after the core arbiter requested it back.
     */
    bool coreDeschedulingScheduled;

    /**
     * This pointer allows fast access to the current kernel thread's
     * localThreadContexts without computing an offset from the global
     * allThreadContexts vector on each access.
     */
    ThreadContext** localThreadContexts;

    /**
     * The unique identifier given by the Linux kernel for this core.
     */
    int id = -1;

    /**
     * This is the context of the thread that a given core is currently
     * executing. If the core is not executing a context, it polls for threads
     * to execute using this context's stack.
     */
    ThreadContext* loadedContext;

    /**
     * This points at the occupied bitmask that describes occupancy for this
     * core.
     */
    std::atomic<MaskAndCount>* localOccupiedAndCount;

    /**
     * The ith bit is set to prevent migration of the ThreadContext at index i.
     * To prevent migration of active contexts, runtime code must set the bit
     * corresponding to loadedContext before clearing the occupied flag at
     * thread exit.
     */
    std::atomic<uint64_t>* localPinnedContexts;

    /**
     * Setting a jth bit indicates that the priority of the thread living at
     * index j is temporarily raised.
     */
    std::atomic<uint64_t>* highPriorityThreads;

    /**
     * A bitmask in which set bits represent contexts that should run with
     * elevated priority.
     * Each call to dispatch() will examine this bitmask before searching other
     * contexts. When reducing the number of cores, this value (if nonzero)
     * should be cleared, since all non-terminated threads on this core will be
     * migrated away from this thread.
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
     * This is the highest-indexed context known to be occupied on a given
     * core. Additional occupied contexts are immediately above this context
     * and have no unoccupied contexts between them.
     */
    uint8_t highestOccupiedContext;
};

void* alignedAlloc(size_t size, size_t alignment = CACHE_LINE_SIZE);
}  // namespace Arachne

#endif
