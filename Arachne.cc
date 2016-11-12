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
#include <getopt.h>
#include <thread>
#include "Cycles.h"
#include "TimeTrace.h"
#include "Util.h"

namespace Arachne {

// Change 0 -> 1 in the following line to compile detailed time tracing in
// this file.
#define TIME_TRACE 0

using PerfUtils::Cycles;
using PerfUtils::TimeTrace;

/**
  * This variable prevents multiple initializations of the library, but does
  * not protect against the user calling Arachne functions without initializing
  * the library, which results in undefined behavior.
  */
bool initialized = false;

// The following configuration options can be passed into threadInit.

/**
  * The degree of parallelism between Arachne threads. If this is set higher
  * than the number of physical cores, the kernel will multiplex, which is
  * usually undesirable except when running unit tests on a single-core
  * system.
  */
volatile uint32_t numCores = 0;

/**
  * Configurable maximum stack size for all threads.
  */
int stackSize = 1024 * 1024;

/**
  * Keep track of the kernel threads we are running so that we can join them on
  * destruction. Also, store a pointer to the original stacks to facilitate
  * switching back and joining.
  */
std::vector<std::thread> kernelThreads;
std::vector<void*> kernelThreadStacks;

/**
  * Alert the kernel threads that they should exit if there are no further
  * threads to run.
  */
volatile bool shutdown;

/**
  * The collection of possibly runnable contexts for each kernel thread.
  */
std::vector<ThreadContext*> activeLists;

/**
  * This pointer allows fast access to the current kernel thread's activeList
  * without computing an offset from the global activeLists vector on each
  * access.
  */
thread_local ThreadContext* activeList;

/**
  * Holds the identifier for the thread in which it is stored: allows each
  * kernel thread to identify itself. This should eventually become a coreId,
  * when we support multiple kernel threads per core to handle blocking system
  * calls.
  */
thread_local int kernelThreadId;

/**
  * This is the context that a given kernel thread is currently executing.
  */
thread_local ThreadContext *loadedContext;

/**
  * See documentation for MaskAndCount.
  */
std::atomic<MaskAndCount> *occupiedAndCount;
thread_local std::atomic<MaskAndCount> *localOccupiedAndCount;

/**
  * This variable holds the index into the current kernel thread's activeList
  * that it will check first the next time it looks for a thread to run. It is
  * used to implement round-robin scheduling of Arachne threads.
  */
thread_local size_t nextCandidateIndex = 0;

/**
  * Allocate a block of memory aligned at the beginning of a cache line.
  *
  * \param size
  *     The amount of memory to allocate.
  */
void*
cacheAlignAlloc(size_t size) {
    void *temp;
    int result = posix_memalign(&temp, CACHE_LINE_SIZE, size);
    if (result != 0) {
        fprintf(stderr, "posix_memalign returned %s", strerror(result));
        exit(1);
    }
    assert((reinterpret_cast<uint64_t>(temp) & (CACHE_LINE_SIZE - 1)) == 0);
    return temp;
}

/**
 * Main function for a kernel thread, which roughly corresponds to a core in the
 * current design of the system.
 * 
 * \param kId
 *     The kernel thread ID for the newly created kernel thread.
 */
void
threadMain(int kId) {
    kernelThreadId = kId;
    localOccupiedAndCount = &occupiedAndCount[kernelThreadId];
    activeList = activeLists[kernelThreadId];

    PerfUtils::Util::pinThreadToCore(kId);

    loadedContext = activeList;

    // Transfers control to the Arachne dispatcher.
    // This context has been pre-initialized by threadInit so it will "return"
    // to the schedulerMainLoop.
    // This call will return iff threadDestroy is called from the main thread
    // and the main thread never joined the Arachne thread pool. This situation
    // normally occurs only in unit tests.
    swapcontext(&loadedContext->sp, &kernelThreadStacks[kId]);
}

/**
  * Save the current register values onto one stack and load fresh register
  * values from another stack.
  * This method does not return to its caller immediately. It returns to the
  * caller when another thread on the same kernel thread invokes this method
  * with the current value of target as the saved parameter.
  *
  * \param saved
  *     Address of the stack location to load register values from.
  * \param target
  *     Address of the stack location to save register values to.
  */
void __attribute__((noinline))
swapcontext(void **saved, void **target) {
    // This code depends on knowledge of the compiler's calling convention: rdi
    // and rsi are the first two arguments.
    // Alternative approaches tend to run into conflicts with compiler register
    // use.

    // Save the registers that the compiler expects to persist across method
    // calls and store the stack pointer's location after saving these
    // registers.
    // NB: The space used by the pushed and
    // popped registers must equal the value of SpaceForSavedRegisters, which
    // should be updated atomically with this assembly.
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
void
schedulerMainLoop() {
    while (true) {
        // No thread to execute yet. This call will not return until we have
        // been assigned a new Arachne thread.
        dispatch();
        reinterpret_cast<ThreadInvocationEnabler*>(
                &loadedContext->threadInvocation)->runThread();
        // The thread has exited.
        // Cancel any wakeups the thread may have scheduled for itself before
        // exiting.
        loadedContext->wakeupTimeInCycles = UNOCCUPIED;

        // Bump the generation number for the next newborn thread.
        loadedContext->generation++;
        {

            // Handle joins
            std::lock_guard<SpinLock> joinGuard(loadedContext->joinLock);
            loadedContext->joinCV.notifyAll();
        }

        // The code below clears the occupied flag for the current
        // ThreadContext.
        //
        // While this logically comes before dispatch(), it is here to prevent
        // it from racing against thread creations that come before the start
        // of the outer loop, since the occupied flags for such creations would
        // get wiped out by this code.
        bool success;
        do {
            MaskAndCount slotMap = *localOccupiedAndCount;
            MaskAndCount oldSlotMap = slotMap;

            slotMap.numOccupied--;

            slotMap.occupied &=
                ~(1L << loadedContext->idInCore) & 0x00FFFFFFFFFFFFFF;
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
void
yield() {
    // This thread is still runnable since it is merely yielding.
    loadedContext->wakeupTimeInCycles = 0L;
    dispatch();
}

/**
  * Sleep for at least ns nanoseconds.
  */
void
sleep(uint64_t ns) {
    loadedContext->wakeupTimeInCycles =
        Cycles::rdtsc() + Cycles::fromNanoseconds(ns);
    dispatch();
}

/**
  * Return a thread handle for the currently executing thread, identical to the
  * one returned by the createThread call that initially created this thread.
  */
ThreadId
getThreadId() {
    return ThreadId(loadedContext, loadedContext->generation);
}

/**
  * Deschedule the current thread until its wakeup time is reached (which may
  * have already happened) and find another thread to run. All direct and
  * indirect callers of this function must ensure that spurious wakeups are
  * safe.
  */
void
dispatch() {
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

            // Check for termination
            if (shutdown)
                swapcontext(
                        &kernelThreadStacks[kernelThreadId],
                        &loadedContext->sp);
        }
        // Optimize to eliminate unoccupied contexts
        if (!(mask & 1))
            continue;

        ThreadContext* currentContext = &activeList[currentIndex];
        if (currentCycles >= currentContext->wakeupTimeInCycles) {
            nextCandidateIndex = currentIndex + 1;
            if (nextCandidateIndex == maxThreadsPerCore) nextCandidateIndex = 0;

            if (currentContext == loadedContext) {
                loadedContext->wakeupTimeInCycles = BLOCKED;
                return;
            }
            void** saved = &loadedContext->sp;
            loadedContext = currentContext;
            swapcontext(&loadedContext->sp, saved);
            // After the old context is swapped out above, this line executes
            // in the new context.
            loadedContext->wakeupTimeInCycles = BLOCKED;
            return;
        }
    }
}

/*
 * Make the thread referred to by ThreadId runnable.
 * If one thread exits and another is created between the check and the setting
 * of the wakeup flag, this signal will result in a spurious wake-up.
 * If this method is invoked on a currently running thread, it will have the
 * effect of causing the thread to immediately unblock the next time it blocks.
 */
void
signal(ThreadId id) {
    uint64_t oldWakeupTime = id.context->wakeupTimeInCycles;
    if (oldWakeupTime != UNOCCUPIED) {
        // We do the CAS in assembly because we do not want to pay for the
        // extra memory fences for ordinary stores that std::atomic adds.
        uint64_t newValue = 0L;
        __asm__ __volatile__("lock; cmpxchgq %0,%1" : "=r" (newValue), 
                "=m" (id.context->wakeupTimeInCycles),
                "=a" (oldWakeupTime) : "0" (newValue), "2" (oldWakeupTime));
    }
}

/**
  * Block the current thread until the thread identified by id finishes its
  * execution.
  *
  * \param id
  *     The id of the thread to join.
  */
void
join(ThreadId id) {
    std::lock_guard<SpinLock> joinGuard(id.context->joinLock);
    // Thread has already exited.
    if (id.generation != id.context->generation) return;
    MaskAndCount slotMap = *localOccupiedAndCount;
    // The thread we are waiting for has already exited, so we can return
    // immediately.
    if (!(slotMap.occupied & (1L << id.context->idInCore))) return;
    id.context->joinCV.wait(id.context->joinLock);
    return;
}

/**
 * This is a special function to allow the main kernel thread to join the
 * thread pool after seeding at least one initial Arachne thread. It must be
 * the last statement in the main function since it never returns.
 */
void
mainThreadJoinPool() {
    /*
     * An alternative way of enabling the main thread to join the pool is to change
     * threadInit to take a real main function as an argument, and have the
     * standard main invoke only threadInit. Under such an implementation,
     * threadInit would never return.
     */
    threadMain(numCores - 1);
}

/**
  * This function parses out the arguments intended for the thread library from
  * a command line, and adjusts the values of argc and argv to eliminate the
  * arguments that the thread library consumed.
  *
  * Here are valid sequences of arguments in argv, and the final state of argv.
  *
  * 1. Library options followed by '--' followed by application options.
  *        ApplicationName <libraryOptionA> <libraryOptionB> -- <applicationOptionA>...
  *
  *    Argv after the call:
  *        ApplicationName <applicationOptionA>...
  *
  * 2. Library options only.
  *        ApplicationName <libraryOptionA> <libraryOptionB>
  *
  *    Argv after the call:
  *        ApplicationName
  *
  * 3. Application options only.
  *        ApplicationName <applicationOptionA> <applicationOptionB>...
  *    Argv after the call:
  *        ApplicationName <applicationOptionA> <applicationOptionB>...
 */
void
parseOptions(int* argcp, const char*** argvp) {
    if (argcp == NULL) return;

    // Disable printing to stderr when we see an unrecognized option, since
    // options that aren't recognized by us may still be recognized by the
    // application.
    opterr = 0;

    int argc = *argcp;
    char* const * argv = const_cast<char* const*>(*argvp);
    int option;
    static struct option longOptions[] = {
        {"numCores", required_argument, NULL, 'c'},
        {"stackSize", required_argument, NULL, 's'},
        {0, 0, 0, 0}
    };
    while (1) {
        /* getopt_long stores the option index here. */
        int optionIndex = 0;
        option = getopt_long(argc, argv, "+c:s:", longOptions, &optionIndex);
        if (option == -1)
            break;
        if (option == '?') { // Unrecognized option, let application handle it
            // Reverse the increment of optind, which still happens when we
            // encounter an unrecognized option.
            optind--;
            break;
        }
        switch (option) {
            case 'c':
                numCores = atoi(optarg);
                break;
            case 's':
                stackSize = atoi(optarg);
                break;
            default:
                fprintf(stderr, "Unrecognized option %d found!\n", option);
                abort();
        }
    }
    *argcp -= optind - 1;
    *argvp += optind - 1;
    // Move the program's name to one position before the unparsed options, so
    // the application's argument parser will be able to look at an argc, argv
    // pair that looks like it never contained Arachne options.
    **argvp = *argv;

    // Reset optind to 0, so that the application can have a clean state in
    // case it wants to use getopt also.
    optind = 0;
    // Restore error reporting to getopts
    opterr = 1;
}

ThreadContext::ThreadContext(uint8_t idInCore)
    : stack(malloc(stackSize))
    , sp(reinterpret_cast<char*>(stack) +
            stackSize - 2*sizeof(void*))
    , wakeupTimeInCycles(UNOCCUPIED)
    , generation(1)
    , joinLock()
    , joinCV()
    , idInCore(idInCore)
{
    // Immediately before schedulerMainLoop gains control, we want the
    // stack to look like this, so that the swapcontext call will
    // transfer control to schedulerMainLoop.
    //           +-----------------------+
    //           |                       |
    //           +-----------------------+
    //           |     Return Address    |
    //           +-----------------------+
    //     sp->  |       Registers       |
    //           +-----------------------+
    //           |                       |
    //           |                       |
    //
    // Set up the stack so that the first time we switch context to
    // this thread, we enter schedulerMainLoop.
    *reinterpret_cast<void**>(sp) = reinterpret_cast<void*>(schedulerMainLoop);

    /**
     * Decrement the stack pointer by the amount of space needed to
     * store the registers in swapcontext.
     */
    sp = reinterpret_cast<char*>(sp) - SpaceForSavedRegisters;
}

/**
 * This function sets up state needed by the thread library, and must be
 * invoked before any other function in the thread library is invoked. It is
 * undefined behavior to invoke other Arachne functions before this one.
 *
 * Arachne will take configuration options from the command line specified by
 * argc and argv, and then update the values of argv and argc to reflect the
 * remaining arguments.
 */
void
threadInit(int* argcp, const char*** argvp) {
    if (initialized)
        return;
    initialized = true;
    parseOptions(argcp, argvp);

    if (numCores == 0)
        numCores = std::thread::hardware_concurrency();

    occupiedAndCount = reinterpret_cast<std::atomic<Arachne::MaskAndCount>* >(
            cacheAlignAlloc(sizeof(MaskAndCount) * numCores));
    memset(occupiedAndCount, 0, sizeof(MaskAndCount) * numCores);

    for (unsigned int i = 0; i < numCores; i++) {
        // Here we will allocate all the thread contexts and stacks
        ThreadContext *contexts = reinterpret_cast<ThreadContext*>(
                cacheAlignAlloc(sizeof(ThreadContext) * maxThreadsPerCore));
        for (uint8_t k = 0; k < maxThreadsPerCore; k++) {
            new (&contexts[k]) ThreadContext(k);
        }
        activeLists.push_back(contexts);
    }

    // Allocate space to store all the original kernel pointers
    kernelThreadStacks.reserve(numCores);
    shutdown = false;

    // Ensure that data structure and stack allocation completes before we
    // begin to use it in a new thread.
    PerfUtils::Util::serialize();

    // We only loop to numCores - 1 to leave one core for the main thread to
    // run on.
    for (unsigned int i = 0; i < numCores - 1; i++) {
        // These threads are started with threadMain instead of
        // schedulerMainLoop because we want schedulerMainLoop to run on a user
        // stack rather than a kernel-provided stack
        kernelThreads.emplace_back(threadMain, i);
    }

    // Set the kernelThreadId for the main thread.
    // This is necessary to enable the original main function to schedule
    // Arachne threads onto its own core before the main thread joins the
    // thread pool.
    kernelThreadId = numCores - 1;
}

/**
  * This function tears down all state created by threadInit, and restores the
  * state of the system to the time before threadInit is called.
  *
  * It is primarily used in testing, and only works in the absence of new
  * thread creations. It should be invoked only from a thread not managed by
  * Arachne.
  */
void
threadDestroy() {
    // Wait for all contexts to finish executing
    while (true) {
        bool quiescent = true;
        for (size_t i = 0; i < numCores; i++) {
            MaskAndCount slotMap = occupiedAndCount[i];
            if (slotMap.numOccupied > 0) {
                quiescent = false;
                break;
            }
        }
        if (quiescent)
            break;
    }
    // Join the kernel threads.
    shutdown = true;
    for (size_t i = 0; i < kernelThreads.size(); i++) {
        kernelThreads[i].join();
    }
    kernelThreads.clear();

    // We now assume that all threads are done executing.
    PerfUtils::Util::serialize();

    free(occupiedAndCount);
    for (size_t i = 0; i < numCores; i++) {
        for (int k = 0; k < maxThreadsPerCore; k++) {
            free(activeLists[i][k].stack);
            activeLists[i][k].joinLock.~SpinLock();
            activeLists[i][k].joinCV.~ConditionVariable();
        }
        free(activeLists[i]);
    }
    activeLists.clear();
    PerfUtils::Util::serialize();
    initialized = false;
}

ConditionVariable::ConditionVariable()
    : blockedThreads() {}

ConditionVariable::~ConditionVariable() { }

/**
  * Awaken one of the threads waiting on this condition variable.
  * The caller must hold the mutex that waiting threads held when they called
  * wait().
  */
void
ConditionVariable::notifyOne() {
    if (blockedThreads.empty()) return;
    ThreadId awakenedThread = blockedThreads.front();
    blockedThreads.pop_front();
    signal(awakenedThread);
}

/**
  * Awaken all of the threads waiting on this condition variable.
  * The caller must hold the mutex that waiting threads held when they called
  * wait().
  */
void
ConditionVariable::notifyAll() {
    while (!blockedThreads.empty())
        notifyOne();
}

/**
  * Block the current thread until the condition variable is notified.
  *
  * \param lock
  *     The mutex associated with this condition variable; must be held by
  *     caller before calling wait. This function releases the mutex before
  *     blocking, and re-acquires it before returning to the user.
  
  */
void
ConditionVariable::wait(SpinLock& lock) {
#if TIME_TRACE
    TimeTrace::record("Wait on Core %d", kernelThreadId);
#endif
    blockedThreads.push_back(
            ThreadId(loadedContext, loadedContext->generation));
    lock.unlock();
    dispatch();
#if TIME_TRACE
    TimeTrace::record("About to acquire lock after waking up");
#endif
    lock.lock();
}
} // namespace Arachne
