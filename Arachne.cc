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
  * This is the context that a given kernel thread is currently executing.
  */
thread_local ThreadContext *runningContext;

/**
  * See documentation for MaskAndCount.
  */
std::atomic<MaskAndCount> *occupiedAndCount;
thread_local std::atomic<MaskAndCount> *localOccupiedAndCount;

/**
  * This variable holds the index into the current core's activeList that it
  * will check first the next time it looks for a thread to run.
  */
thread_local size_t nextCandidateIndex = 0;

/**
  * Allocates a block of memory that is aligned at the beginning of a cache line.
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
 * \param coreId
 *     The kernel thread ID for the newly created kernel thread.
 */
void threadMainFunction(int id) {
    // Switch to a user stack, discarding the stack provided by the kernel, so
    // that we are always running user code on a stack controlled by Arachne.
    kernelThreadId = id;
    localOccupiedAndCount = &occupiedAndCount[kernelThreadId];
    activeList = activeLists[kernelThreadId];

    PerfUtils::Util::pinThreadToCore(id);

    runningContext = activeList;

    // Transfers control to the Arachne dispatcher.
    // This context has been pre-initialized by threadInit so it will "return"
    // to the schedulerMainLoop.
    runningContext->sp = reinterpret_cast<char*>(runningContext->sp) +
        SpaceForSavedRegisters;
    asm("movq %0, %%rsp"::"g"(runningContext->sp));
    asm("retq");
}

/**
 * Save the current register values onto one stack and load fresh register
 * values from another stack.
 * This method does not return to its caller immediately. It returns to the
 * caller when another thread on the same core invokes this method with the
 * current value of target as the saved parameter.
 *
 * \param saved
 *     Address of the stack location to load register values from.
 * \param target
 *     Address of the stack location to save register values to.
 */
void __attribute__((noinline)) swapcontext(void **saved, void **target) {
    // This code depends on knowledge of the compiler's calling convention: rdi
    // and rsi are the first two arguments.
    // Alternative approaches tend to run into conflicts with compiler register
    // use.

    // Save the registers and store the stack pointer
    // NB: The space used by the pushed and popped registers must equal the
    // value of SpaceForSavedRegisters, which should be updated atomically with
    // this assembly.
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
  * This is the top level method executed by each thread context. It is never
  * directly invoked. Instead, the thread's context is set up to "return" to
  * this method when we context switch to it the first time.
  */
void schedulerMainLoop() {
    while (true) {
        // No thread to execute yet. This call will not return until we have
        // been assigned a new Arachne thread.
        block();
        reinterpret_cast<ThreadInvocationEnabler*>(
                &runningContext->threadInvocation)->runThread();
        // The thread has exited.
        // Cancel any wakeups the thread may have scheduled for itself before
        // exiting.
        runningContext->wakeupTimeInCycles = ~0L;
        if (runningContext->waiter) {
            signal(runningContext->waiter);
            runningContext->waiter = NULL;
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
            if (slotMap.occupied & (1L << runningContext->idInCore))
                slotMap.numOccupied--;

            slotMap.occupied &=
                ~(1L << runningContext->idInCore) & 0x00FFFFFFFFFFFFFF;
            success = localOccupiedAndCount->compare_exchange_strong(
                    oldSlotMap,
                    slotMap);
        } while (!success);
    }
}

/**
  * This method is used as part of cooperative multithreading to give other
  * Arachne threads on the same core a chance to run.
  * It will return when all other threads have had a chance to run.
  */
void yield() {
    // This thread is still runnable since it is merely yielding.
    runningContext->wakeupTimeInCycles = 0;
    block();
}

/**
  * Sleep for at least ns nanoseconds.
  */
void sleep(uint64_t ns) {
    runningContext->wakeupTimeInCycles =
        Cycles::rdtsc() + Cycles::fromNanoseconds(ns);
    block();
}

/**
  * Return a thread handle to the currently executing thread, identical to the
  * one returned by the createThread call that initially created this thread.
  */
ThreadId getThreadId() {
    return runningContext;
}

/**
  * Deschedule the current thread until it is signaled. All direct and indirect
  * callers of this function must ensure that spurious wakeups are safe.
  */
void block() {
    // Find a thread to switch to
    size_t currentIndex = nextCandidateIndex;
    uint64_t mask = localOccupiedAndCount->load().occupied >> currentIndex;
    uint64_t currentCycles = Cycles::rdtsc();

    for (;;currentIndex++, mask >>= 1L) {
        if (mask == 0) {
            // We have reached the end of the threads, so we should go back to
            // the beginning.
            currentIndex = 0;
            mask = localOccupiedAndCount->load().occupied;
            currentCycles = Cycles::rdtsc();
        }
        // Optimize to eliminate unoccupied contexts
        if (!(mask & 1))
            continue;

        ThreadContext* currentContext = &activeList[currentIndex];
        if (currentCycles >= currentContext->wakeupTimeInCycles) {
            nextCandidateIndex = currentIndex + 1;
            if (nextCandidateIndex == maxThreadsPerCore) nextCandidateIndex = 0;

            if (currentContext == runningContext) {
                runningContext->wakeupTimeInCycles = ~0L;
                return;
            }
            void** saved = &runningContext->sp;
            runningContext = currentContext;
            swapcontext(&runningContext->sp, saved);
            runningContext->wakeupTimeInCycles = ~0L;
            return;
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
     id->waiter = runningContext;
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
            // When schedulerMainLoop gains control, we want the stack to look
            // like this.
            //           +-----------------------+
            //      sp-> |                       |
            //           +-----------------------+
            //           |     Return Address    |
            //           +-----------------------+
            //           |       Registers       |
            //           +-----------------------+
            //           |                       |
            //           |                       |

            freshContext->sp =
                reinterpret_cast<char*>(newStack) + stackSize - 2*sizeof(void*);
            freshContext->waiter = NULL;
            freshContext->wakeupTimeInCycles = ~0L;
            freshContext->idInCore = static_cast<uint8_t>(k);

            // Set up the stack so that the first time we switch context to
            // this thread, we enter schedulerMainLoop.
            *reinterpret_cast<void**>(freshContext->sp) =
                reinterpret_cast<void*>(schedulerMainLoop);
            /**
              * Decrement the stack pointer by the amount of space needed to
              * store the registers in swapcontext.
              */
            freshContext->sp = reinterpret_cast<char*>(freshContext->sp) -
                SpaceForSavedRegisters;
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
