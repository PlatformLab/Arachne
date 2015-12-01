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
thread_local jmp_buf libraryContext;
//thread_local void* libraryStackPointer;
//void* libraryInstructionPointer;

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
    for (int i = 0; i < numCores; i++) {
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

    // Resume here when user task finishes or yields
    int error = setjmp(libraryContext);
    if (error == 1) {
        // Check if the currently running user thread is finished and recycle
        // its stack if it is.
        if (running.finished) {
            stackPool.push_front(running.stack);
        }
    }
    else if (error != 0) {
        fprintf(stderr, "Library setjmp failed! Exiting...");
        exit(1);
    }

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
        }
        // Wrap the function to restore control when the user thread
        // terminates instead of yielding.
        // TODO(hq6): Need to create and swap context here, not just invoke the
        // function directly.
        threadWrapper(running);
    }
}

/**
 * TODO(hq6): We are missing memory barriers.
 */
void threadWrapper(WorkUnit work) {
   work.workFunction();
   work.finished = true;
   longjmp(libraryContext, 0);
}

/**
 * Create a WorkUnit for the given task, on the same queue as the current
 * function.
 */
void createTask(std::function<void()> task) {
    WorkUnit work;
    work.finished = false;
    work.workFunction = task;
    std::lock_guard<SpinLock> guard(workQueueLocks[kernelThreadId]);
    workQueues[kernelThreadId].push_back(work);
}

/**
 * This is a special function to allow the main thread to join the thread pool
 * after seeding initial tasks for itself and possibly other threads.
 *
 * The other way of implementing this is to merge this function with the
 * threadInit, and ask the user to provide an initial to run in the main
 * thread, which will presumably spawn other tasks.
 */
void mainThreadJoinPool() {
    threadMainFunction(numCores - 1);
}

}
