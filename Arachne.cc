#include <stdio.h>
#include <thread>
#include "string.h"
#include "Arachne.h"
#include "Cycles.h"
#include "TimeTrace.h"
#include "Util.h"

namespace Arachne {

using PerfUtils::Cycles;
using PerfUtils::TimeTrace;

enum InitializationState {
    NOT_INITIALIZED,
    INITIALIZING,
    INITIALIZED
};

void schedulerMainLoop();


InitializationState initializationState = NOT_INITIALIZED;
unsigned numCores = 1;

/**
 * The state for each user thread.
 */
std::vector<UserContext*> activeLists;
thread_local UserContext* activeList;


/**
 * Threads that are sleeping and waiting.
 */
static std::vector<std::deque<UserContext*> > sleepQueues;

thread_local int kernelThreadId;

/**
  * This is the context that a given core is currently executing in.
  */
thread_local UserContext *running;

/**
  * This variable holds the array of flags indicating occupancy and the current
  * number of slots in the core.
  */
std::atomic<MaskAndCount>  *occupiedAndCount;
thread_local std::atomic<MaskAndCount>  *localOccupiedAndCount;


/**
  * This variable holds the most recent index into the active threads array for
  * the current core. By maintaining this index, we can avoid starvation of
  * runnable threads later on in the list by runnable threads earlier in the
  * list.
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
 * This function will allocate stacks and create kernel threads pinned to particular cores.
 *
 * Note that the setting of initializationState should probably use memory
 * barriers, but we are currently assuming that the application will only call
 * it from a single thread of execution.
 *
 * Calling the function twice is a no-op.
 */
void threadInit() {
    if (initializationState != NOT_INITIALIZED) {
        return;
    }
    initializationState = INITIALIZING;

    // Allocate stacks. Note that number of cores is actually number of
    // hyperthreaded cores, rather than necessarily real CPU cores.
    numCores = std::thread::hardware_concurrency(); 
    printf("numCores = %u\n", numCores);

    cache_align_alloc(&occupiedAndCount, sizeof(MaskAndCount) * numCores);

    for (unsigned int i = 0; i < numCores; i++) {
        sleepQueues.push_back(std::deque<UserContext*>());

        // Here we will create all the user contexts and user stacks
        UserContext *contexts;
        cache_align_alloc(&contexts, sizeof(UserContext) * maxThreadsPerCore);
        for (int k = 0; k < maxThreadsPerCore; k++) {
            UserContext *freshContext = &contexts[k];
            freshContext->stack = malloc(stackSize);
            freshContext->index = k;
            freshContext->wakeup = false;

            // Set up the stack to return to the main thread function.
            freshContext->sp = (char*) freshContext->stack + stackSize - 64; 
            *(void**) freshContext->sp = (void*) schedulerMainLoop;
            savecontext(&freshContext->sp);
        }
        activeLists.push_back(contexts);


    }
    // Ensure that data structure and stack allocation completes before we
    // begin to use it in a new thread.
    PerfUtils::Util::serialize();

    // Leave one thread for the main thread
    for (unsigned int i = 0; i < numCores - 1; i++) {
        std::thread(threadMainFunction, i).detach();
    }

    // Set the kernelThreadId for the main thread
    kernelThreadId = numCores - 1;

    initializationState = INITIALIZED;
}

/**
 * Main function for a kernel-level thread participating in the thread pool. 
 */
void threadMainFunction(int id) {
    // Switch to a user stack for symmetry, so that we can deallocate it ourselves later.
    // If we are the last user thread on this core, we will never return since we will simply poll for work.
    // If we are not the last, we will context switch out of this function and have our
    // stack deallocated by the thread we swap to (perhaps in the unblock
    // function), the next time Arachne gets control.
    // The original thread given by the kernel is simply discarded in the
    // current implementation.
    kernelThreadId = id;
    localOccupiedAndCount = &occupiedAndCount[kernelThreadId];
    *localOccupiedAndCount = MaskAndCount {0,0};
    activeList = activeLists[kernelThreadId];

    PerfUtils::Util::pinThreadToCore(id);

    running = activeList;
    setcontext(&running->sp);

    // TODO: Delete the function below and restructure
    // block vs ArachneMainLoop.
}

/**
 * Load a new context without saving anything.
 */
void  __attribute__ ((noinline))  setcontext(void **saved) {

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
 */
void  __attribute__ ((noinline))  savecontext(void **target) {
    // Load the new stack pointer, push the registers, and then restore the old stack pointer.

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
 * %rdi, %rsi are the two addresses of where stack pointers are stored.
 *
 * Load from saved and store into target.
 */

void  __attribute__ ((noinline))  swapcontext(void **saved, void **target) {

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
  * This function runs the scheduler in the context of the most recently run
  * thread, unless there is already a new thread creation request on this core.
  */
void schedulerMainLoop() {
    // At most one user thread on each core should be going through this loop
    // at any given time.  Most threads should be inside runThread, and only
    // re-enter the thread library by making an API call into Arachne.
    while (true) {
        // Clear the occupied flag
        bool success;
        do {
            MaskAndCount slotMap = *localOccupiedAndCount;
            MaskAndCount oldSlotMap = slotMap;

            // Decrement count only if the flag was originally set
            if (slotMap.occupied & (1 << running->index))
                slotMap.count--;

            slotMap.occupied &= ~(1 << running->index);
            success = localOccupiedAndCount->compare_exchange_strong(
                    oldSlotMap,
                    slotMap);
        } while (!success);

        block();
        reinterpret_cast<TaskBase*>(&running->task)->runThread();
		running->wakeup = false;
    }

}

/**
 * Return control back to the thread library.
 * Assume we are the running process in the current kernel thread if we are calling
 * yield.
 *
 * TODO: Start from a different point in the list every time to avoid two
 * runnable threads from starving all later ones on the list.
 */
void yield() {
    checkSleepQueue();

    // This thread is still runnable since it is merely yielding.
    running->wakeup = true; 

    size_t size = maxThreadsPerCore;

    currentIndex++;
    if (currentIndex == size) currentIndex = 0;

    // Splitting into two loops instead of a combined loop with an extra conditional save us 8 ns on average.
    for (size_t i = currentIndex; i < size; i++) {
        if (activeList[i].wakeup && &activeList[i] != running) {
            void** saved = &running->sp;
            running = &activeList[i];
            swapcontext(&running->sp, saved);
            running->wakeup = false;
            return;
        }
    }
    for (size_t i = 0; i < currentIndex; i++) {
        if (activeList[i].wakeup && &activeList[i] != running) {
            void** saved = &running->sp;
            running = &activeList[i];
            swapcontext(&running->sp, saved);
            running->wakeup = false;
            return;
        }
    }
}
void checkSleepQueue() {
    uint64_t currentCycles = Cycles::rdtsc();
    auto& sleepQueue = sleepQueues[kernelThreadId];

    // Assume sorted and move it off the list
    while (sleepQueue.size() > 0 && sleepQueue[0]->wakeUpTimeInCycles < currentCycles) {
        // Move onto the ready queue
        sleepQueue[0]->wakeup = true;
        sleepQueue.pop_front();  
    }
}

// Sleep for at least the argument number of ns.
// We keep in core-resident to avoid cross-core cache coherency concerns.
// It must be the case that this function returns after at least ns have
// passed.
void sleep(uint64_t ns) {
    running->wakeUpTimeInCycles = Cycles::rdtsc() + Cycles::fromNanoseconds(ns);

    auto& sleepQueue = sleepQueues[kernelThreadId];
    if (sleepQueue.size() == 0) {
        sleepQueue.push_back(running);
    }
    else {
        auto it = sleepQueue.begin();
        printf("sleepQueueSize = %zu\n", sleepQueue.size());
        for (; it != sleepQueue.end() ; it++)
            if ((*it)->wakeUpTimeInCycles > running->wakeUpTimeInCycles) {
                sleepQueue.insert(it, running);
                break;
            }

        printf("sleepQueueSize = %zu\n", sleepQueue.size());
        // Insert now
        if (it == sleepQueue.end()) {
            sleepQueue.push_back(running);
        }
        printf("sleepQueueSize = %zu\n", sleepQueue.size());
    }

    block();
}

/**
  * Returns a thread handle to the application, which can be passed into the signal function.
  */
ThreadId getThreadId() {
    return running;
}

/**
  * Deschedule the current thread until the application signals using the a
  * ThreadId. The caller of this function must ensure that spurious wakeups are
  * safe, in the same manner as a caller of a condition variable's wait()
  * function.
  */
void block() {
    while (1) {
        // Check the sleep queue
        checkSleepQueue();

        // Find a thread to switch to
        size_t size = maxThreadsPerCore;

        for (size_t i = currentIndex; i < size; i++) {
            if (activeList[i].wakeup) {
                currentIndex = i + 1;
                if (currentIndex == size) currentIndex = 0;

                if (&activeList[i] == running) {
                    running->wakeup = false;
                    return;
                }
                void** saved = &running->sp;
                running = &activeList[i];
                swapcontext(&running->sp, saved);
                running->wakeup = false;
                return;
            }
        }
        for (size_t i = 0; i < currentIndex; i++) {
            if (activeList[i].wakeup) {
                currentIndex = i + 1;
                if (currentIndex == size) currentIndex = 0;

                if (&activeList[i] == running) {
                    running->wakeup = false;
                    return;
                }
                void** saved = &running->sp;
                running = &activeList[i];
                swapcontext(&running->sp, saved);
                running->wakeup = false;
                return;
            }
        }
    }
}

/* 
 * Make the thread referred to by ThreadId runnable once again. 
 * It is safe to call this function without knowing whether the target thread
 * has already exited.
 */
void signal(ThreadId id) {
    id->wakeup = true;
}


/**
 * This is a special function to allow the main thread to join the thread pool
 * after seeding initial tasks for itself and possibly other threads.
 *
 * The other way of implementing this is to merge this function with the
 * threadInit, and ask the user to provide an initial task to run in the main
 * thread, which will presumably spawn other tasks.
 */
void mainThreadJoinPool() {
    threadMainFunction(numCores - 1);
}

}
