#include <stdio.h>
#include <vector>
#include <deque>
#include <thread>
#include <mutex>
#include "string.h"
#include "Arachne.h"
#include "SpinLock.h"
//#include "TimeTrace.h"

namespace Arachne {

enum InitializationState {
    NOT_INITIALIZED,
    INITIALIZING,
    INITIALIZED
};

struct WorkUnit {
    std::function<void()> workFunction;
    void* sp;
    bool finished; // The top of the stack of the current workunit, used for both indicating
    // whether this is new or old work, and also for making it easier to
    // recycle the stacks.
    void* stack;
};
void threadWrapper();

const int stackSize = 1024 * 1024;
const int stackPoolSize = 100;

InitializationState initializationState = NOT_INITIALIZED;
unsigned numCores = 1;

/**
 * Work to do on each thread. 
 * TODO(hq6): If vector is too slow, we may switch to a linked list.  */
static std::vector<std::deque<WorkUnit*> > workQueues;

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
        // Need to lock the queue with a SpinLock
        { // Make the guard go out of scope as soon as it is no longer needed.
            std::lock_guard<SpinLock> guard(workQueueLocks[kernelThreadId]);
            if (workQueues[kernelThreadId].empty()) continue;

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
        }
    }
}

/**
 * Load a new context without saving anything.
 */
void  __attribute__ ((noinline))  setcontext(void **saved) {
//   asm("movq  0(%rdi), %rsp");
//   asm("movq  8(%rdi), %rax"); // TODO: Switch to using push / pop instructions
//   asm("movq  %rax, 0(%rsp)");

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
//    PerfUtils::TimeTrace::getGlobalInstance()->record("About to yield");
    workQueueLocks[kernelThreadId].lock();
//    PerfUtils::TimeTrace::getGlobalInstance()->record("Acquired workQueueLock");
    if (workQueues[kernelThreadId].empty()) {
        workQueueLocks[kernelThreadId].unlock();
        return; // Yield is noop if there is no longer work to be done.
    }

    void** saved = &running->sp;

    workQueues[kernelThreadId].push_back(running);
//    PerfUtils::TimeTrace::getGlobalInstance()->record("Pushed running onto the queue");
    running = workQueues[kernelThreadId].front();
    workQueues[kernelThreadId].pop_front();
//    PerfUtils::TimeTrace::getGlobalInstance()->record("Popped new from the queue");
    workQueueLocks[kernelThreadId].unlock();
//    PerfUtils::TimeTrace::getGlobalInstance()->record("Unlocked the workQueueLock");

    // Loop until we find a thread to run, in case there are new threads that
    // do not have a thread.
//    PerfUtils::TimeTrace::getGlobalInstance()->record("About to perform actual thread yield");
    swapcontext(&running->sp, saved);
//    PerfUtils::TimeTrace::getGlobalInstance()->record("Returned from thread yield");
}

/**
 * Create a WorkUnit for the given task, on the same queue as the current
 * function.
 */
int createTask(std::function<void()> task, int coreId) {
    if (coreId == -1) coreId = kernelThreadId;
    std::lock_guard<SpinLock> guard(workQueueLocks[coreId]);
    if (stackPool[coreId].empty()) return -1;

    WorkUnit *work = new WorkUnit; // TODO: Get rid of the new here.
    work->finished = false;
    work->workFunction = task;

    work->stack = stackPool[coreId].front();
    stackPool[coreId].pop_front();
    workQueues[coreId].push_back(work);

    // Wrap the function to restore control when the user thread
    // terminates instead of yielding.
    work->sp = (char*) work->stack + stackSize - 64; 
    // set up the stack to pass the single argument in this case.
    *(void**) work->sp = (void*) threadWrapper;

    // Set up the initial stack with the registers from the current thread.
    savecontext(&work->sp);
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
