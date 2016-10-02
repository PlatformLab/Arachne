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

#ifndef ARACHNE_PRIVATE_H_
#define ARACHNE_PRIVATE_H_

#include "Common.h"

namespace Arachne {

/**
  * We need to invoke a ThreadInvocation with unknown template types, which has
  * been stored in a character array, and this class enables us to do this.
  */
struct ThreadInvocationEnabler {
    virtual void runThread() = 0;
};

/**
  * This structure is used during thread creation to pass the function and
  * arguments for the new thread's top-level function from a creating thread to
  * the core that runs the new thread. It also ensures that the arguments will
  * fit in a single cache line, since they will be stored in a single cache line.
  *
  * \tparam F
  *     the type of the return value of std::bind, which is a value type of
  *     unspecified class.
  *
  * This wrapper enables us to bypass the dynamic memory allocation that is
  * sometimes performed by std::function.
  */
template<typename F>
struct ThreadInvocation : public ThreadInvocationEnabler {
    // The top-level function of the user thread.
    F mainFunction;
    explicit ThreadInvocation(F mainFunction)
        : mainFunction(mainFunction) {
        static_assert(sizeof(ThreadInvocation<F>) <= CACHE_LINE_SIZE,
                "Arachne requires the function and arguments for a thread to "
                "fit within one cache line.");
    }
    // This is invoked exactly once for each thread to begin its execution.
    void runThread() {
        mainFunction();
    }
};

/*
 * This class holds all the state for managing a user thread.
 */
struct ThreadContext {
    // This holds the value that rsp will be set to when this thread is swapped
    // in.
    void* sp;

    // This points to the thread which called join() on the current thread.
    ThreadContext* waiter;

    // When a thread blocks due to calling sleep(), it will keep its wakeup
    // time in rdtsc cycles here. This field should only be accessed by the
    // same core that the thread runs on.
    uint64_t wakeupTimeInCycles;

    // This flag is a signal that this thread should run at the next
    // opportunity. It should be cleared immediately before control is
    // returned to the application.
    volatile bool wakeup;

    // Unique identifier for this thread among those on the same core.
    // Used to index into various core-specific arrays.
    // This is read-only after Arachne initialization.
    uint8_t idInCore;

    // Storage for the ThreadInvocation object which contains the function and
    // arguments for a new thread.
    // We wrap the char buffer in a struct to enable aligning to a cache line
    // boundary, which eliminates false sharing of cache lines.
    struct alignas(CACHE_LINE_SIZE) {
        char data[CACHE_LINE_SIZE];
    } threadInvocation;
};

typedef ThreadContext* ThreadId;

const int maxThreadsPerCore = 56;

void schedulerMainLoop();
void savecontext(void **target);
void swapcontext(void **saved, void **target);
void setcontext(void **context);
void threadMainFunction(int id);

extern thread_local int kernelThreadId;
extern thread_local ThreadContext *running;
extern thread_local ThreadContext *activeList;
extern std::vector<ThreadContext*> activeLists;

// This structure tracks the active threads on a single core.
struct MaskAndCount{
    // Each bit corresponds to a particular ThreadContext which has the
    // idInCore corresponding to its index
    // 0 means this ThreadContext is available for a new thread.
    // 1 means that a thread exists and is running in this context.
    uint64_t occupied : 56;
    // The number of 1 bits in occupied.
    uint8_t numOccupied : 8;
};

extern std::atomic<MaskAndCount> *occupiedAndCount;
extern thread_local std::atomic<MaskAndCount> *localOccupiedAndCount;

/**
  * A random number generator from the Internet which returns 64-bit integers.
  * It is used for selecting candidate cores to create threads on.
  */
inline uint64_t random(void) {
    // This function came from the following site.
    // http://stackoverflow.com/a/1640399/391161
    static uint64_t x = 123456789, y = 362436069, z = 521288629;
    uint64_t t;
    x ^= x << 16;
    x ^= x >> 5;
    x ^= x << 1;

    t = x;
    x = y;
    y = z;
    z = t ^ x ^ y;

    return z;
}

} // namespace Arachne
#endif
