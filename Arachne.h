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

#ifndef ARACHNE_H_
#define ARACHNE_H_

#include <assert.h>
#include <functional>
#include <vector>
#include <mutex>
#include <deque>
#include <atomic>


#include "Common.h"

namespace  Arachne {

/**
  * We need to invoke a ThreadInvocation with unknown template types, which has
  * been stored in a character array, and this class enables us to do this.
  *
  * TODO: Can we find a better name for this?
  */
struct AbstractThreadInvocation {
    virtual void runThread() = 0;
};

/**
  * This structure is used during thread creation to pass the function and
  * arguments for the new thread's top-level function from a creating thread to
  * the core that runs the new thread. It also ensures that the arguments will
  * fit in a single cache line, since they will be stored in a single cache line.
  *
  * TODO: Look up the doxygen syntax for documenting types for templates.
  * TODO: Move internal things to ArachnePrivate.h and include it here.
  *
  * F is the type of the return value of std::bind, which is a value
  * type of unspecified class.
  *
  * This wrapper enables us to bypass the dynamic memory allocation that is
  * sometimes performed by std::function.
  */
template<typename F>
struct ThreadInvocation : public AbstractThreadInvocation {
    // The top-level function of the user thread.
    F mainFunction;
    explicit ThreadInvocation(F mainFunction) : mainFunction(mainFunction) {
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
 * This class holds all the state for a managing a user thread.
 */
struct ThreadContext {
    // This holds the value that rsp will be set to when this thread is swapped
    // in.
    void* sp;

    // This points to the thread which called join() on the current thread.
    ThreadContext* waiter;

    // When a thread blocks due to calling sleep(), it will keep its wakeup time
    // here. This field should only be accessed by the same core that the
    // thread is resident on.
    uint64_t wakeupTimeInCycles;

    // This flag is a signal that this thread should run at the next
    // opportunity.  It should be cleared immediately before control is
    // returned to the application and set by either remote cores as a signal
    // or when a thread yields.
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
extern volatile unsigned numCores;

const int maxThreadsPerCore = 56;

void schedulerMainLoop();
void  savecontext(void **target);
void  swapcontext(void **saved, void **target);
void setcontext(void **context);
void threadMainFunction(int id);
void mainThreadJoinPool();

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

extern std::atomic<MaskAndCount>  *occupiedAndCount;
extern thread_local std::atomic<MaskAndCount>  *localOccupiedAndCount;

/**
  * Create a user thread to run the function f with the given args on the provided core.
  * Pass in -1 as a core ID to use the creator's core.
  *
  * This function should usually only be invoked directly in tests, since it
  * does not perform load balancing.
  */
template<typename _Callable, typename... _Args>
    int createThread(int coreId, _Callable&& __f, _Args&&... __args) {
    if (coreId == -1) coreId = kernelThreadId;

    auto task = std::bind(
            std::forward<_Callable>(__f), std::forward<_Args>(__args)...);

    bool success;
    int index;
    do {
        // Attempt to enqueue the task to the specific core in this case.
        MaskAndCount slotMap = occupiedAndCount[coreId];
        MaskAndCount oldSlotMap = slotMap;

        // Search for a non-occupied slot and attempt to reserve the slot
        index = 0;
        while ((slotMap.occupied & (1L << index)) && index < maxThreadsPerCore)
            index++;

        if (index == maxThreadsPerCore) {
            return -1;
        }

        slotMap.occupied |= (1L << index);
        slotMap.numOccupied++;

        success = occupiedAndCount[coreId].compare_exchange_strong(oldSlotMap,
                slotMap);
    } while (!success);

    // Copy the thread invocation into the byte array.
    new (&activeLists[coreId][index].threadInvocation)
        Arachne::ThreadInvocation<decltype(task)>(task);
    activeLists[coreId][index].wakeup = true;

    return 0;
}

/**
  * A random number generator from the Internet, used for selecting candidate
  * cores to create threads on.
  */
inline uint64_t random(void) {
    // TODO(hq6): Google for this and find the link: xorshf96
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

/**
  * Spawn a new thread.
  * TODO: Fix this documentation for the user.
  * TODO: Return a ThreadID with a distinguished value (Arachne::NullThread) for failure.
  */
template<typename _Callable, typename... _Args>
    int createThread(_Callable&& __f, _Args&&... __args) {

    // Find a core to enqueue to by picking two at random and choose the one
    // with the fewest threads.

    int coreId;
    int choice1 = random() % numCores;
    int choice2 = random() % numCores;
    while (choice2 == choice1) choice2 = random() % numCores;

    if (occupiedAndCount[choice1].load().numOccupied <
            occupiedAndCount[choice2].load().numOccupied)
        coreId = choice1;
    else
        coreId = choice2;

    return createThread(coreId, __f, __args...);
}

void threadInit();
void yield();
void sleep(uint64_t ns);
ThreadId getThreadId();
void block();
void signal(ThreadId id);
bool join(ThreadId id);

} // namespace Arachne
#endif // ARACHNE_H_
