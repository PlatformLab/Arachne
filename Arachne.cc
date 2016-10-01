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


#include "Arachne.h"
#include <stdio.h>
#include <string.h>
#include <thread>
#include "Cycles.h"
#include "TimeTrace.h"
#include "Util.h"

namespace Arachne {

using PerfUtils::Cycles;
using PerfUtils::TimeTrace;

enum InitializationState {
    NOT_INITIALIZED,
    INITIALIZED
};

// This variable causes any initialization after the first one to be a no-opt,
// but does not protect against the user calling Arachne functions without
// initializing the library, which will lead to undefined behavior.
InitializationState initializationState = NOT_INITIALIZED;


/**
  * This value should be set by the user before threadInit if they want a
  * different number of kernel threads than the number of cores on their
  * machine.
  */
volatile unsigned numCores = 0;

/**
  * Configurable stack size for all threads.
  */
int stackSize = 1024 * 1024;

/**
 * The collection of possibly runnable contexts for each core.
 */
std::vector<ThreadContext*> activeLists;

/**
  * This pointer allows fast access to the current kernel thread's activeList
  * without computing an offset from the global activeLists vector on each
  * access.
  */
thread_local ThreadContext* activeList;

/**
  * This variable provides the position in global data structures which
  * corresponds to a particular kernel thread. This should eventually become a
  * coreId, when we support multiple kernel threads per core to handle blocking
  * system calls.
  */
thread_local int kernelThreadId;

/**
  * This is the context that a given core is currently executing on.
  */
thread_local ThreadContext *running;

/**
  * This variable holds a bitfield of flags indicating occupancy and the current
  * number of threads on the core.
  */
std::atomic<MaskAndCount>  *occupiedAndCount;
thread_local std::atomic<MaskAndCount>  *localOccupiedAndCount;


/**
  * This variable holds the most recent index into the active threads array for
  * the current core. By maintaining this index, we can avoid starvation of
  * higher-indexed runnable threads by lower-indexed runnable threads.
  */
thread_local size_t currentIndex = 0;

/**
  * This function takes the address of a pointer to be allocated and attempts
  * to make a cache-aligned allocation for the pointer.
  */
void cache_align_alloc(void* addressOfTargetPointer, size_t size) {
    void ** trueAddress = reinterpret_cast<void**>(addressOfTargetPointer);
    int result = posix_memalign(trueAddress, CACHE_LINE_SIZE, size);
    if (result != 0) {
        fprintf(stderr, "posix_memalign returned %s", strerror(result));
        exit(1);
    }
    assert((reinterpret_cast<uint64_t>(*trueAddress) & 0x3f) == 0);
}


/**
 * Main function for a kernel thread which roughly corresponds to a core in this
 * system.
 */
void threadMainFunction(int id) {
    // Switch to a user stack, discarding the stack provided by the kernel, so
    // that we are always running user code on a stack controlled by Arachne.
    kernelThreadId = id;
    localOccupiedAndCount = &occupiedAndCount[kernelThreadId];
    activeList = activeLists[kernelThreadId];

    PerfUtils::Util::pinThreadToCore(id);

    running = activeList;
    setcontext(&running->sp);
}

/**
 * Load a new context without saving any curent state.
 */
void __attribute__((noinline)) setcontext(void **saved) {

    // Load the stack pointer and restore the registers
    asm("movq (%rdi), %rsp");

    asm("popq %rbp\n\t"
        "popq %rbx\n\t"
        "popq %r15\n\t"
        "popq %r14\n\t"
        "popq %r13\n\t"
        "popq %r12\n\t"
        );
}

/**
 * Save a context of the currently executing process.
 * Note that if this function is used to set up the context for a new user
 * thread, the address of the new thread's main function should be manually
 * placed on the new stack before invoking this method.
 */
void __attribute__((noinline)) savecontext(void **target) {
    asm("movq %rsp, %r11\n\t"
        "movq (%rdi), %rsp\n\t"
        "pushq %r12\n\t"
        "pushq %r13\n\t"
        "pushq %r14\n\t"
        "pushq %r15\n\t"
        "pushq %rbx\n\t"
        "pushq %rbp\n\t"
        "movq  %rsp, (%rdi)\n\t"
        "movq %r11, %rsp"
        );
}

/**
 * Save one set of registers and load another set.
 * %rdi, %rsi are the addresses of where stack pointers are stored.
 *
 * Load from saved and store into target.
 */

void __attribute__((noinline)) swapcontext(void **saved, void **target) {
    // Save the registers and store the stack pointer
    asm("pushq %r12\n\t"
        "pushq %r13\n\t"
        "pushq %r14\n\t"
        "pushq %r15\n\t"
        "pushq %rbx\n\t"
        "pushq %rbp\n\t"
        "movq  %rsp, (%rsi)");

    // Load the stack pointer and restore the registers
    asm("movq (%rdi), %rsp\n\t"
        "popq %rbp\n\t"
        "popq %rbx\n\t"
        "popq %r15\n\t"
        "popq %r14\n\t"
        "popq %r13\n\t"
        "popq %r12");
}

/**
  * Top-level function for each user stack. This function never terminates.
  */
void schedulerMainLoop() {
    // At most one user thread on each core should be going through this loop
    // at any given time.  Most threads should be inside runThread, and only
    // re-enter the thread library by making an API call into Arachne.
    while (true) {
        block();
        reinterpret_cast<AbstractThreadInvocation*>(
                &running->threadInvocation)->runThread();
        running->wakeup = false;
        if (running->waiter) {
            signal(running->waiter);
            running->waiter = NULL;
        }

        // Clear the occupied flag
        // While this may logically come before the block(), it is here to
        // prevent it from racing against thread creations that come before
        // this while loop starts, since the occupied flags for such creations
        // would get wiped out by this code.
        bool success;
        do {
            MaskAndCount slotMap = *localOccupiedAndCount;
            MaskAndCount oldSlotMap = slotMap;

            // Decrement numOccupied only if the flag was originally set
            if (slotMap.occupied & (1L << running->idInCore))
                slotMap.numOccupied--;

            slotMap.occupied &= ~(1L << running->idInCore);
            success = localOccupiedAndCount->compare_exchange_strong(
                    oldSlotMap,
                    slotMap);
        } while (!success);

    }

}

/**
 * Return control back to the thread library, but run again if there are no
 * other runnable threads.
 */
void yield() {
    // This thread is still runnable since it is merely yielding.
    running->wakeup = true;
    block();
}

/**
  * Sleep for at least ns nanoseconds.
  */
void sleep(uint64_t ns) {
    running->wakeupTimeInCycles = Cycles::rdtsc() + Cycles::fromNanoseconds(ns);
    block();
}

/**
  * Returns a thread handle to the currently executing thread, which can be
  * passed into the signal function.
  */
ThreadId getThreadId() {
    return running;
}

/**
  * Examine a particular ThreadContext and checks whether the conditions are
  * correct for it to awaken. If they are appropriate, then it will awaken the
  * thread and return true to the newly active context.  Otherwise, it will
  * return false to the current context.
  */
bool attemptWakeup(size_t i, uint64_t currentCycles) {
    if (activeList[i].wakeup ||
            (activeList[i].wakeupTimeInCycles != 0 &&
             currentCycles > activeList[i].wakeupTimeInCycles)
       ) {
        activeList[i].wakeupTimeInCycles = 0;
        currentIndex = i + 1;
        if (currentIndex == maxThreadsPerCore) currentIndex = 0;

        if (&activeList[i] == running) {
            running->wakeup = false;
            return true;
        }
        void** saved = &running->sp;
        running = &activeList[i];
        swapcontext(&running->sp, saved);
        running->wakeup = false;
        return true;
    }
    return false;
}

/**
  * Deschedule the current thread until another thread signals using the
  * current thread's ThreadId. All direct and indirect callers of this function
  * must ensure that spurious wakeups are safe.
  */
void block() {
    while (1) {
        uint64_t currentCycles = Cycles::rdtsc();

        // Find a thread to switch to
        uint64_t occupied = localOccupiedAndCount->load().occupied;
        uint64_t firstHalf = occupied >> currentIndex;

        for (size_t i = currentIndex; firstHalf; i++, firstHalf >>= 1) {
            if (!(firstHalf & 1)) continue;
            if (attemptWakeup(i, currentCycles)) return;
        }

        for (size_t i = 0; i < currentIndex && occupied; i++, occupied >>= 1) {
            if (!(occupied & 1)) continue;
            if (attemptWakeup(i, currentCycles)) return;
        }
    }
}

/*
 * Cause the thread referred to by ThreadId runnable once again.
 * It is safe to call this function without knowing whether the target thread
 * has already exited.
 */
void signal(ThreadId id) {
    id->wakeup = true;
}

/**
  * Blocks the current thread until the thread identified by id finishes its
  * execution. If the join was successful, then we will return true when we unblock.
  *
  * Otherwise return false immediately if the thread has already been joined.
  */
bool join(ThreadId id) {
     if (id->waiter) return false;
     // If the thread has already exited, we should not block, since doing so
     // may result in blocking forever.
     MaskAndCount slotMap = *localOccupiedAndCount;
     if (!(slotMap.occupied & (1L << id->idInCore))) return true;
     id->waiter = running;
     block();
     return true;
}


/**
 * This is a special function to allow the main thread to join the thread pool
 * after seeding initial tasks for itself and possibly other threads. It must
 * be the last statement in the main function since it does not return.
 *
 * An alternative way of enabling the main thread to join the pool is to change
 * threadInit to take a real main function as an argument, and have the
 * standard main invoke only threadInit. Under such an implementation,
 * threadInit would never * return.
 */
void mainThreadJoinPool() {
    threadMainFunction(numCores - 1);
}

/**
 * This function sets up all state for the thread library to run, and must
 * return before any other function in the thread library is invoked, lest the
 * result be undefined behavior.
 *
 * Calling the function twice is a no-op.
 */
void threadInit() {
    if (initializationState != NOT_INITIALIZED) {
        return;
    }

    // Allocate stacks. Note that number of cores is actually number of
    // hyperthreaded cores, rather than necessarily real CPU cores.
    if (numCores == 0)
        numCores = std::thread::hardware_concurrency();
    printf("numCores = %u\n", numCores);

    cache_align_alloc(&occupiedAndCount, sizeof(MaskAndCount) * numCores);
    memset(occupiedAndCount, 0, sizeof(MaskAndCount));

    for (unsigned int i = 0; i < numCores; i++) {
        // Here we will create all the user contexts and user stacks
        ThreadContext *contexts;
        cache_align_alloc(&contexts, sizeof(ThreadContext) * maxThreadsPerCore);
        for (int k = 0; k < maxThreadsPerCore; k++) {
            ThreadContext *freshContext = &contexts[k];
            void* newStack = malloc(stackSize);
            freshContext->sp = reinterpret_cast<char*>(newStack) + stackSize;
            freshContext->waiter = NULL;
            freshContext->wakeupTimeInCycles = 0;
            freshContext->wakeup = false;
            freshContext->idInCore = k;

            // Set up the stack to return to the main thread function.
            *reinterpret_cast<void**>(freshContext->sp) =
                reinterpret_cast<void*>(schedulerMainLoop);
            savecontext(&freshContext->sp);
        }
        activeLists.push_back(contexts);
    }
    // Ensure that data structure and stack allocation completes before we
    // begin to use it in a new thread.
    PerfUtils::Util::serialize();

    // Leave one core for the main thread
    for (unsigned int i = 0; i < numCores - 1; i++) {
        std::thread(threadMainFunction, i).detach();
    }

    // Set the kernelThreadId for the main thread
    kernelThreadId = numCores - 1;
    initializationState = INITIALIZED;
}
} // namespace Arachne
