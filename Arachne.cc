#include <stdio.h>
#include <vector>
#include <deque>
#include <thread>
#include <mutex>
#include "string.h"
#include "Arachne.h"
#include "SpinLock.h"
#include "Cycles.h"
#include "CacheTrace.h"

namespace Arachne {

using PerfUtils::Cycles;

enum InitializationState {
    NOT_INITIALIZED,
    INITIALIZING,
    INITIALIZED
};

struct WorkUnit {
    // The main function of the user thread.
    std::function<void()> workFunction;

    // This is a pointer to the lowest valid memory address in the user thread stack.
    void* sp;

    // This flag is set on completion of a thread's main function. When set,
    // the thread dispatcher can reclaim the stack for this thread.
    bool finished; 
    
    // The top of the stack of the current workunit, used for both indicating
    // whether this is new or old work, and also for making it easier to
    // recycle the stacks.
    void* stack;

    // When a thread enters the sleep queue, it will keep its wakup time
    // here.
    uint64_t wakeUpTimeInCycles;
};
void threadWrapper();

const int stackSize = 1024 * 1024;
const int stackPoolSize = 1000;

InitializationState initializationState = NOT_INITIALIZED;
unsigned numCores = 1;

/**
 * Work to do on each thread. 
 * TODO(hq6): If vector is too slow, we may switch to a linked list.  */
static std::vector<std::deque<WorkUnit*> > workQueues;

/**
 * Threads that are sleeping and waiting 
 */
static std::vector<std::deque<WorkUnit*> > sleepQueues;

/**
 * Protect each work queue.
 */
static SpinLock *workQueueLocks;
thread_local int kernelThreadId;
static std::vector<std::deque<void*> > stackPool;

/**
 * These two values store the place in the kernel thread to return to when a
 * user task either yields or completes.
 */
thread_local void* libraryStackPointer;
thread_local WorkUnit *running;

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
    numCores = std::thread::hardware_concurrency() / 2; 
    printf("numCores = %u\n", numCores);
    workQueueLocks = new SpinLock[numCores];
    for (unsigned int i = 0; i < numCores; i++) {
        workQueues.push_back(std::deque<WorkUnit*>());
        sleepQueues.push_back(std::deque<WorkUnit*>());

        // Initialize stack pool for each kernel thread
        stackPool.push_back(std::deque<void*>());
        for (int j = 0; j < stackPoolSize; j++)
            stackPool[i].push_back(malloc(stackSize));

        // Leave one thread for the main thread
        if (i != numCores - 1) {
            std::thread(threadMainFunction, i).detach();
        }

    }

    // Set the kernelThreadId for the main thread
    kernelThreadId = numCores - 1;

    initializationState = INITIALIZED;
}

/**
 * Main function for a kernel-level thread participating in the thread pool. 
 * It first allocates a pool of stacks, and then polls its own work queue for
 * work to do.
 */
void threadMainFunction(int id) {
    kernelThreadId = id;

    // Poll for work on my queue
    while (true) {
        checkSleepQueue();
        // Need to lock the queue with a SpinLock
        { // Make the guard go out of scope as soon as it is no longer needed.
            std::lock_guard<SpinLock> guard(workQueueLocks[kernelThreadId]);
            // Record a time here
            if (workQueues[kernelThreadId].empty()) continue;
            // Trace it here only if there was actual work in the queue.
             
            running = workQueues[kernelThreadId].front();
            workQueues[kernelThreadId].pop_front();
        }

        swapcontext(&running->sp, &libraryStackPointer);

        // Resume right after here when user task finishes or yields
        // Check if the currently running user thread is finished and recycle
        // its stack if it is.
        if (running->finished) {
            stackPool[kernelThreadId].push_front(running->stack);
            delete running;
//            PerfUtils::CacheTrace::getGlobalInstance()->record("A thread has just died.");
        }
    }
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
        "popq %r12");
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
 * TODO(hq6): Figure out whether we can omit the argument altogether and just
 * use the 'running' global variable instead.
 *
 * Note that this function will be jumped to directly using setcontext, so
 * there might be some weirdness with local variables.
 */
void threadWrapper() {
   WorkUnit *work = running; 
   work->workFunction();
   work->finished = true;
   __asm__ __volatile__("lfence" ::: "memory");
   setcontext(&libraryStackPointer);
}

/**
 * Restore control back to the thread library.
 * Assume we are the running process in the current kernel thread if we are calling
 * yield.
 */
void yield() {
    checkSleepQueue();
    workQueueLocks[kernelThreadId].lock();
    if (workQueues[kernelThreadId].empty()) {
        workQueueLocks[kernelThreadId].unlock();
        return; // Yield is noop if there is no longer work to be done.
    }

    void** saved = &running->sp;

    workQueues[kernelThreadId].push_back(running);
    running = workQueues[kernelThreadId].front();
    workQueues[kernelThreadId].pop_front();
    workQueueLocks[kernelThreadId].unlock();

    // Loop until we find a thread to run, in case there are new threads that
    // do not have a thread.
    swapcontext(&running->sp, saved);
}
void checkSleepQueue() {
    uint64_t currentCycles = Cycles::rdtsc();
    auto& sleepQueue = sleepQueues[kernelThreadId];

    workQueueLocks[kernelThreadId].lock();
    // Assume sorted and move it off the list
    while (sleepQueue.size() > 0 && sleepQueue[0]->wakeUpTimeInCycles < currentCycles) {
        // Move onto the ready queue
        workQueues[kernelThreadId].push_back(sleepQueue[0]);
        sleepQueue.pop_front();  
    }
    workQueueLocks[kernelThreadId].unlock();
}

// Sleep for at least the argument number of ns.
// We keep in core-resident to avoid cross-core cache coherency concerns.
// It must be the case that this function returns after at least ns have
// passed.
void sleep(uint64_t ns) {
    running->wakeUpTimeInCycles = Cycles::rdtsc() + Cycles::fromNanoseconds(ns);

    auto& sleepQueue = sleepQueues[kernelThreadId];
    // TODO(hq6): Sort this by wake-up time using possibly binary search
    if (sleepQueue.size() == 0) sleepQueue.push_back(running);
    else {
        auto it = sleepQueue.begin();
        for (; it != sleepQueue.end() ; it++)
            if ((*it)->wakeUpTimeInCycles > running->wakeUpTimeInCycles) {
                sleepQueue.insert(it, running);
                break;
            }
        // Insert now
        if (it == sleepQueue.end()) {
            sleepQueue.push_back(running);
        }
    }
        
    checkSleepQueue();

    workQueueLocks[kernelThreadId].lock();
    if (workQueues[kernelThreadId].empty()) {
        workQueueLocks[kernelThreadId].unlock();
        // Swap into library context, somehow get control again cleanly after.
        // Same scenario as nothing on the ready queue.
        swapcontext(&libraryStackPointer, &running->sp);
        // Return after swapcontext returns because that means we're supposed to run
        return;
    }

    // There is work to do, so we switch to the new work.
    void** saved = &running->sp;
    running = workQueues[kernelThreadId].front();
    workQueues[kernelThreadId].pop_front();
    workQueueLocks[kernelThreadId].unlock();
    swapcontext(&running->sp, saved);
}

/**
 * Create a WorkUnit for the given task, on the same queue as the current
 * function.
 */
int createThread(std::function<void()> task, int coreId) {
    if (coreId == -1) coreId = kernelThreadId;
    PerfUtils::CacheTrace::getGlobalInstance()->record("Before workQueueLock", PerfUtils::Util::serialReadPmc(1));
    std::lock_guard<SpinLock> guard(workQueueLocks[coreId]);
    PerfUtils::CacheTrace::getGlobalInstance()->record("Acquired workQueueLock", PerfUtils::Util::serialReadPmc(1));
    if (stackPool[coreId].empty()) return -1;
    PerfUtils::CacheTrace::getGlobalInstance()->record("Checked stackPool", PerfUtils::Util::serialReadPmc(1));

    WorkUnit *work = new WorkUnit; // TODO: Get rid of the new here.
    work->finished = false;
    work->workFunction = task;
    PerfUtils::CacheTrace::getGlobalInstance()->record("Allocated WorkUnit and initialized", PerfUtils::Util::serialReadPmc(1));

    work->stack = stackPool[coreId].front();
    stackPool[coreId].pop_front();
    workQueues[coreId].push_back(work);
    PerfUtils::CacheTrace::getGlobalInstance()->record("Added work to workQueue", PerfUtils::Util::serialReadPmc(1));

    // Wrap the function to restore control when the user thread
    // terminates instead of yielding.
    work->sp = (char*) work->stack + stackSize - 64; 
    // set up the stack to pass the single argument in this case.
    *(void**) work->sp = (void*) threadWrapper;

    // Set up the initial stack with the registers from the current thread.
    savecontext(&work->sp);
    PerfUtils::CacheTrace::getGlobalInstance()->record("Finished saving context", PerfUtils::Util::serialReadPmc(1));
    return 0;
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
