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
#include <queue>

#include "Common.h"

namespace Arachne {

// Forward declare to break circular dependency between ThreadContext and
// ConditionVariable
struct ThreadContext;

/**
  * These configuration parameters should be set before calling threadInit and
  * are documented with threadInit.
  */
extern volatile uint32_t numCores;

struct ThreadId {
    /**
      * This ThreadId does not simply use std::pair because std::pair does not
      * support assignment of volatile from non-volatile variables, and it
      * makes sense for applications to keep volatile ThreadIds which are
      * shared between threads.
      */
    ThreadContext* context;
    uint32_t generation;

    ThreadId(ThreadContext* context, uint32_t generation)
        : context(context)
        , generation(generation) { }

    ThreadId()
        : context(NULL)
        , generation(0) { }

    bool operator==(const ThreadId& other) const {
        return context == other.context && generation == other.generation;
    }

    bool operator!=(const ThreadId& other) const {
        return !(*this == other);
    }
};

/**
 * A simple SpinLock based on std::atomic.
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
        // lock. Otherwise we failed.
        return !state.exchange(true, std::memory_order_acquire);
    }

    void unlock() {
        state.store(false, std::memory_order_release);
    }

 private:
    // Implements the lock: false means free, true means locked
    std::atomic<bool> state;
};

/**
  * This class implements a subset of the functionality of
  * std::condition_variable.
  * It takes no internal locks, so it is assumed that notifications are
  * performed with the associated mutex held.
  */
class ConditionVariable {
 public:
    ConditionVariable();
    ~ConditionVariable();
    void notifyOne();
    void notifyAll();
    void wait(SpinLock& lock);
 private:
    std::deque<ThreadId> blockedThreads;
    DISALLOW_COPY_AND_ASSIGN(ConditionVariable);
};

////////////////////////////////////////////////////////////////////////////////
// The code in following section are private to the thread library.
////////////////////////////////////////////////////////////////////////////////

/**
  * We need to invoke a ThreadInvocation with unknown template types, which has
  * been stored in a character array, and this class enables us to do this.
  */
struct ThreadInvocationEnabler {
    virtual void runThread() = 0;
    virtual ~ThreadInvocationEnabler() { }
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

    // Keep a reference to a original allocation so that we can release the
    // memory if we need to.
    void* stack;

    // When a thread blocks due to calling sleep(), it will keep its wakeup
    // time in rdtsc cycles here.
    // The special value 0 is a signal that this thread should run at the next
    // opportunity.
    // The special value ~0 is a signal that the thread should not be run, and
    // wakeupTimeInCycles should be set to this value immediately before
    // returning control to the application.
    volatile uint64_t wakeupTimeInCycles;

    // This variable is incremented whenever a new thread begins or ends
    // execution in this ThreadContext. It is used to differentiate between
    // threads which existed at different points in time in the same
    // ThreadContext, and ensure that thread joins do not inadvertently join a
    // new thread living at the same ThreadContext rather than the original
    // thread they were waiting for.
    uint32_t generation;

    // This lock and condition variable is used for synchronizing threads that
    // attempt to join this thread.
    // They are pointers rather than values to avoid circular dependencies
    // between Condition Variables and ThreadContext objects.
    SpinLock joinLock;
    ConditionVariable joinCV;

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

    ThreadContext() = delete;
    ThreadContext(ThreadContext&) = delete;
};

const int maxThreadsPerCore = 56;

/**
  * This is the amount of space needed on the stack to store the callee-saved
  * registers that are defined by the current processor and operating system's
  * calling convention.
  */
const size_t SpaceForSavedRegisters = 48;

void schedulerMainLoop();
void swapcontext(void **saved, void **target);
void threadMainFunction(int id);

extern thread_local int kernelThreadId;
extern thread_local ThreadContext *runningContext;
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

////////////////////////////////////////////////////////////////////////////////
// The ends the private section of the thread library.
////////////////////////////////////////////////////////////////////////////////

/**
  * This value is returned by createThread when there are not enough resources
  * to create a new thread.
  */
const Arachne::ThreadId NullThread;

/**
  * Spawn a thread with main function f invoked with the given args on the core
  * with coreId. Pass in -1 for coreId to use the creator's core. This can be
  * useful if the created thread will share a lot of state with the current
  * thread, since it will improve locality.
  *
  * This function should usually only be invoked directly in tests, since it
  * does not perform load balancing. However, it can also be used if the
  * application wants to do its own load balancing.
  *
  * \param __f
  *     The main function for the new thread.
  * \param __args
  *     The arguments for __f.
  * \return
  *     The return value is an identifier which can be passed to other
  *     functions as an identifier.
  */
template<typename _Callable, typename... _Args>
ThreadId createThread(int coreId, _Callable&& __f, _Args&&... __args) {
    if (coreId == -1) coreId = kernelThreadId;

    auto task = std::bind(
            std::forward<_Callable>(__f), std::forward<_Args>(__args)...);

    bool success;
    uint32_t index;
    do {
        // Attempt to enqueue the task to the specific core in this case.
        MaskAndCount slotMap = occupiedAndCount[coreId];
        MaskAndCount oldSlotMap = slotMap;

        // Search for a non-occupied slot and attempt to reserve the slot
        index = 0;
        while ((slotMap.occupied & (1L << index)) && index < maxThreadsPerCore)
            index++;

        if (index == maxThreadsPerCore) {
            return NullThread;
        }

        slotMap.occupied =
            (slotMap.occupied | (1L << index)) & 0x00FFFFFFFFFFFFFF;
        slotMap.numOccupied++;

        success = occupiedAndCount[coreId].compare_exchange_strong(oldSlotMap,
                slotMap);
    } while (!success);

    // Copy the thread invocation into the byte array.
    new (&activeLists[coreId][index].threadInvocation)
        Arachne::ThreadInvocation<decltype(task)>(task);
    activeLists[coreId][index].wakeupTimeInCycles = 0;
    return ThreadId(&activeLists[coreId][index],
            activeLists[coreId][index].generation);
}

/**
  * Spawn a new thread with a function and arguments. The total size of the
  * arguments cannot exceed 48 bytes, and arguments are taken by value, so any
  * reference must be wrapped with std::ref.
  *
  * \param __f
  *     The main function for the new thread.
  * \param __args
  *     The arguments for __f.
  * \return
  *     The return value is an identifier which can be passed to other
  *     functions as an identifier.
  */
template<typename _Callable, typename... _Args>
ThreadId createThread(_Callable&& __f, _Args&&... __args) {
    // Find a core to enqueue to by picking two at random and choose the one
    // with the fewest threads.
    int coreId;
    int choice1 = static_cast<int>(random()) % numCores;
    int choice2 = static_cast<int>(random()) % numCores;
    while (choice2 == choice1) choice2 = static_cast<int>(random()) % numCores;

    if (occupiedAndCount[choice1].load().numOccupied <
            occupiedAndCount[choice2].load().numOccupied)
        coreId = choice1;
    else
        coreId = choice2;

    return createThread(coreId, __f, __args...);
}

void threadInit();
void threadDestroy();
void mainThreadJoinPool();
void yield();
void sleep(uint64_t ns);
void block();
void signal(ThreadId id);
void join(ThreadId id);
ThreadId getThreadId();

} // namespace Arachne

#endif // ARACHNE_H_
