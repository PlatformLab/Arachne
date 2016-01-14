#include <stdio.h>
#include <vector>
#include <deque>
#include <thread>
#include <mutex>
#include "Arachne.h"
#include "SpinLock.h"

namespace Arachne {

enum InitializationState {
    NOT_INITIALIZED,
    INITIALIZING,
    INITIALIZED
};

struct WorkUnit {
    std::function<void()> workFunction;
    UserContext context;
    bool finished;
    // The top of the stack of the current workunit, used for both indicating
    // whether this is new or old work, and also for making it easier to
    // recycle the stacks.
    void* stack;
};
void threadWrapper(WorkUnit* work);

const int stackSize = 1024 * 1024;
const int stackPoolSize = 100;

InitializationState initializationState = NOT_INITIALIZED;
unsigned numCores = 1;
std::vector<std::thread> threadPool;

/**
 * Work to do on each thread. 
 * TODO(hq6): If vector is too slow, we may switch to a linked list.
 */
static std::vector<std::deque<WorkUnit> > workQueues;

/**
 * Protect each work queue.
 */
static SpinLock *workQueueLocks;

thread_local int kernelThreadId;
thread_local std::deque<void*> stackPool;

/**
 * These two values store the place in the kernel thread to return to when a
 * user task either yields or completes.
 */
thread_local UserContext libraryContext;
thread_local WorkUnit running;

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
    workQueueLocks = new SpinLock[numCores];
    for (unsigned int i = 0; i < numCores; i++) {
        workQueues.push_back(std::deque<WorkUnit>());

        // Leave one thread for the main thread
        if (i != numCores - 1) {
            threadPool.push_back(std::thread(threadMainFunction, i));
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
    // Initialize stack pool for this thread
    for (int i = 0; i < stackPoolSize; i++) {
        stackPool.push_back(malloc(stackSize));
    }

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

        // No stack implies this thread has never run before
        if (!running.stack) {
            if (!stackPool.empty()) {
                running.stack = stackPool.front();
                stackPool.pop_front();
            } else {
                // Yield to someone else who does have a stack to run on
                std::lock_guard<SpinLock> guard(workQueueLocks[kernelThreadId]);
                workQueues[kernelThreadId].push_back(running);
                continue;
            }
            // Wrap the function to restore control when the user thread
            // terminates instead of yielding.
            running.context.esp = (char*) running.stack + stackSize - 64; 

            // set up the stack to pass the single argument in this case.
            *(void**) running.context.esp = (void*) &running;
            running.context.esp = (char*) running.context.esp - sizeof (void*);
            *(void**) running.context.esp = (void*) threadWrapper;

            swapcontext(&running.context, &libraryContext);

            // Resume right after here when user task finishes or yields
            // Check if the currently running user thread is finished and recycle
            // its stack if it is.
            if (running.finished) {
                stackPool.push_front(running.stack);
            }
        } else {
            // Resume where we left off
            setcontext(&running.context);
        }
    }
}

/**
 * Load a new context without saving anything.
 */
void  __attribute__ ((noinline))  setcontext(UserContext *context) {
//   asm("movq  0(%rdi), %rsp");
//   asm("movq  8(%rdi), %rax"); // TODO: Switch to using push / pop instructions
//   asm("movq  %rax, 0(%rsp)");
   // Return to that address

    // Try manual setting and jump
    asm("movq (%rdi), %rsp");
    asm("movq (%rsp), %rax");
    asm("jmp *%rax");
   
}

/**
 * Save one set of registers and load another set.
 * %rdi, %rsi are the two arguments.
 */
void  __attribute__ ((noinline))  swapcontext(UserContext *saved, UserContext *target) {
//   asm("movq  %rsp, (%rdi)");
//   asm("movq  (%rsp), %rax");
//   asm("movq  %rax, 8(%rdi)");
//
//   // Load (need more registers but this is the high levle idea)
//   asm("movq  0(%rdi), %rsp");
//   asm("movq  8(%rdi), %rax");
//   asm("movq  %rax, 0(%rsp)");

//   asm("pushq %rax"); // Desired return pointer
//   asm("pushq %rbp"); // former base pointer
//   asm("movq %rsp, %rbp"); // move the current base pointer to the stack pointer.
//   asm("ret");

    // Try saving the previous instruction pointer for the next jump
    asm("movq  %rsp, (%rsi)");

    // Try manual setting stack and jump
    asm("movq (%rdi), %rsp");
    asm("movq (%rsp), %rax");
    asm("jmp *%rax");
}

/**
 * TODO(hq6): We are missing memory barriers, so performance measurements will
 * be off.
 * TODO(hq6): Figure out whether we can omit the argument altogether and just
 * use the 'running' global variable instead.
 *
 * Note that this function will be jumped to directly using setcontext, so
 * there might be some weirdness with local variables.
 */
void threadWrapper(WorkUnit* work) {
   asm("movq 8(%rsp), %rdi");
   work->finished = true;
//   __asm__ __volatile__("lfence" ::: "memory");
   setcontext(&libraryContext);
}

/**
 * Restore control back to the thread library.
 * Assume we are the running process in the current kernel thread if we are calling
 * yield.
 */
void yield() {
    workQueueLocks[kernelThreadId].lock();
    if (workQueues[kernelThreadId].empty()) {
        workQueueLocks[kernelThreadId].unlock();
        return; // Yield is noop if there is no longer work to be done.
    }

    // Swap to library context and save the current context into the library
    workQueues[kernelThreadId].push_back(running);
    workQueueLocks[kernelThreadId].unlock();
    swapcontext(&running.context, &libraryContext);
}

/**
 * Create a WorkUnit for the given task, on the same queue as the current
 * function.
 */
void createTask(std::function<void()> task) {
    WorkUnit work;
    work.finished = false;
    work.workFunction = task;
    work.stack = NULL;
    std::lock_guard<SpinLock> guard(workQueueLocks[kernelThreadId]);
    workQueues[kernelThreadId].push_back(work);
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
