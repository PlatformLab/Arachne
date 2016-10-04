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

/**
  * This variable prevents multiple initializations of the library, but does
  * not protect against the user calling Arachne functions without initializing
  * the library, which results in undefined behavior.
  */
InitializationState initializationState = NOT_INITIALIZED;

/**
  * This value should be set by the user before calling threadInit if they want
  * a different number of kernel threads than the number of cores on their
  * machine.
  */
volatile uint32_t numCores = 0;

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
  * This is the context that a given core is currently executing.
  */
thread_local ThreadContext *running;

/**
  * This array holds a bitfield of flags indicating which contexts are occupied
  * and the current number of occupied contexts on each core.
  */
std::atomic<MaskAndCount> *occupiedAndCount;
thread_local std::atomic<MaskAndCount> *localOccupiedAndCount;

/**
  * This variable holds the most recent index into the active threads array for
  * the current core. By maintaining this index to point one higher than the
  * last thread to run, we avoid starvation of higher-indexed runnable threads
  * by lower-indexed runnable threads.
  */
thread_local size_t currentIndex = 0;

/**
  * This function attempts to perform a cache-aligned allocation for the
  * pointer passed in.
  *
  * \param addressOfTargetPointer
  *     The location of a pointer to be allocated.
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
 * Main function for a kernel thread which roughly corresponds to a core in the
 * current design of the system.
 * 
 * \param id
 *     The kernel thread ID for the newly created kernel thread.
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
 * Load a fresh set of register values without saving any current register
 * values.
 * 
 * \param saved
 *     A pointer to the top of the stack to load the register values from.
 */
void __attribute__((noinline)) setcontext(void **saved) {
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
 * Copy the context of the currently executing process onto a stack.
 *
 * Note that if this function is used to set up the context for a new user
 * thread, the address of the new thread's main function should be manually
 * placed on the new stack before invoking this method.
 *
 * \param target
 *     The address of the top of the stack to store the registers in.
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
        "movq %rsp, (%rdi)\n\t"
        "movq %r11, %rsp"
        );
}

/**
 * Save the current register values onto one stack and load fresh register
 * values from another stack.
 *
 * \param saved
 *     Address of the stack location to load register values from. It is passed
 *     in register rdi.
 * \param target
 *     Address of the stack location to save register values to. It is passed
 *     in register rsi.
 */
void __attribute__((noinline)) swapcontext(void **saved, void **target) {
    // Save the registers and store the stack pointer
    asm("pushq %r12\n\t"
        "pushq %r13\n\t"
        "pushq %r14\n\t"
        "pushq %r15\n\t"
        "pushq %rbx\n\t"
        "pushq %rbp\n\t"
        "movq %rsp, (%rsi)");

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
  * This function is the entry point for application code and never terminates.
  */
void schedulerMainLoop() {
    // At most one user thread on each core should be going through this loop
    // at any given time. Most threads should be inside runThread, and only
    // re-enter the thread library by making an API call into Arachne.
    while (true) {
        block();
        reinterpret_cast<ThreadInvocationEnabler*>(
                &running->threadInvocation)->runThread();
        running->wakeupTimeInCycles = ~0L;
        if (running->waiter) {
            signal(running->waiter);
            running->waiter = NULL;
        }

        // The code below attempts to clear the occupied flag for the current
        // ThreadContext, and continues to retry until success.
        //
        // While this logically comes before block(), it is here to prevent it
        // from racing against thread creations that come before the start of
        // the outer loop, since the occupied flags for such creations would
        // get wiped out by this code.
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
 * The application calls this function to transfer control to the thread
 * library. It returns immediately if there are no other runnable threads on
 * the same core.
 */
void yield() {
    // This thread is still runnable since it is merely yielding.
    running->wakeupTimeInCycles = 0;
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
  * Return a thread handle to the currently executing thread, identical to the
  * one returned by the createThread call that initially created this thread.
  */
ThreadId getThreadId() {
    return running;
}

/**
  * Examine the ThreadContext with idInCore equal to i, checking whether the
  * thread should run. If so, then it will switch to the thread and return true
  * to that thread's context. Otherwise, it will return false to the current
  * context.
  * 
  * \param i
  *     The index into the current core's list of threads of the thread to
  *     examine.
  * \param currentCycles
  *     The current value of the cycle counter.
  */
bool attemptWakeup(size_t i, uint64_t currentCycles) {
    if (currentCycles > activeList[i].wakeupTimeInCycles) {
        currentIndex = i + 1;
        if (currentIndex == maxThreadsPerCore) currentIndex = 0;

        if (&activeList[i] == running) {
            running->wakeupTimeInCycles = ~0L;
            return true;
        }
        void** saved = &running->sp;
        running = &activeList[i];
        swapcontext(&running->sp, saved);
        running->wakeupTimeInCycles = ~0L;
        return true;
    }
    return false;
}

/**
  * Deschedule the current thread until it is signaled. All direct and indirect
  * callers of this function must ensure that spurious wakeups are safe.
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
 * Make the thread referred to by ThreadId runnable.
 * It is safe to call this function without knowing whether the target thread
 * has already exited.
 */
void signal(ThreadId id) {
    id->wakeupTimeInCycles = 0L;
}

/**
  * Block the current thread until the thread identified by id finishes its
  * execution. 
  *
  * \param id
  *     The id of the thread to join.
  * \return
  *     A bool indicating whether the join was successful. The join is only
  *     unsuccessful if the target has already been joined by another thread.
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
 * This is a special function to allow the main kernel thread to join the
 * thread pool after seeding at least one initial Arachne thread. It must be
 * the last statement in the main function since it never returns.
 *
 * An alternative way of enabling the main thread to join the pool is to change
 * threadInit to take a real main function as an argument, and have the
 * standard main invoke only threadInit. Under such an implementation,
 * threadInit would never return.
 */
void mainThreadJoinPool() {
    threadMainFunction(numCores - 1);
}

/**
 * This function sets up state needed by the thread library, and must be
 * invoked before any other function in the thread library is invoked. It is
 * undefined behavior to invoke other Arachne functions before this one.
 *
 * The following configuration parameters should be set before invoking this
 * function, if desired.
 * 
 * numCores
 *     The degree of parallelism between user threads. If this is set higher
 *     than the number of physical cores, the kernel will multiplex, which is
 *     usually undesirable except when running unit tests on a single-core
 *     system. 
 * stackSize
 *     The maximum size of a thread stack.
 */
void threadInit() {
    if (initializationState != NOT_INITIALIZED)
        return;
    if (numCores == 0)
        numCores = std::thread::hardware_concurrency();
    printf("numCores = %u\n", numCores);

    cache_align_alloc(&occupiedAndCount, sizeof(MaskAndCount) * numCores);
    memset(occupiedAndCount, 0, sizeof(MaskAndCount));

    for (unsigned int i = 0; i < numCores; i++) {
        // Here we will allocate all the thread contexts and stacks
        ThreadContext *contexts;
        cache_align_alloc(&contexts, sizeof(ThreadContext) * maxThreadsPerCore);
        for (int k = 0; k < maxThreadsPerCore; k++) {
            ThreadContext *freshContext = &contexts[k];
            void* newStack = malloc(stackSize);
            freshContext->sp = reinterpret_cast<char*>(newStack) + stackSize;
            freshContext->waiter = NULL;
            freshContext->wakeupTimeInCycles = ~0L;
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

    // We only loop to numCores - 1 to leave one core for the main thread to
    // run on.
    for (unsigned int i = 0; i < numCores - 1; i++) {
        // These threads are started with threadMainFuncion instead of
        // schedulerMainLoop because we want schedulerMainLoop to run on a user
        // stack rather than a kernel-provided stack
        std::thread(threadMainFunction, i).detach();
    }

    // Set the kernelThreadId for the main thread.
    // This is necessary to enable the original main function to schedule
    // Arachne threads onto its own core before the main thread joins the
    // thread pool.
    kernelThreadId = numCores - 1;
    initializationState = INITIALIZED;
}
} // namespace Arachne
