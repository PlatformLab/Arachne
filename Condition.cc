#include "Condition.h"

namespace  Arachne {
    
condition_variable::condition_variable() { }
condition_variable::~condition_variable() { }

void 
condition_variable::notify_one() {
    if (blockedThreads.empty()) return;

    // Put the formerly blocked thread on the same core as it used to be
    int targetCoreId = blockedCoreIds.front();
    UserContext *awakenedThread = blockedThreads.front();
    blockedCoreIds.pop_front();
    blockedThreads.pop_front();
    workQueueLocks[targetCoreId].lock();
    workQueues[targetCoreId].push_back(awakenedThread);
    workQueueLocks[targetCoreId].unlock();
}
void condition_variable::notify_all() {
    while (!blockedThreads.empty())
        notify_one();
}
void condition_variable::wait(SpinLock& lock) {
    // Put my thread on the queue.
    blockedThreads.push_back(running);
    blockedCoreIds.push_back(kernelThreadId);

    lock.unlock();

    // Poll for incoming task.
    // TODO(hq6): Decide whether this function should actually yield immediately.
    if (taskBoxes[kernelThreadId].data.loadState.load() == FILLED) {
        createNewRunnableThread();
        lock.lock();
        return;
    }


    workQueueLocks[kernelThreadId].lock();
    if (workQueues[kernelThreadId].empty()) {
        workQueueLocks[kernelThreadId].unlock();
        // Create an idle thread that runs the main scheduler loop and swap to it, so
        // we're ready for new work with a new stack as soon as it becomes
        // available.
        createNewRunnableThread();

        lock.lock();
        // Return after swapcontext returns because that means we have awoken from our sleep
        return;
    }

    // There are other runnable threads, so we simply switch to the first one.
    void** saved = &running->sp;
    running = workQueues[kernelThreadId].front();
    workQueues[kernelThreadId].pop_front();
    workQueueLocks[kernelThreadId].unlock();
    swapcontext(&running->sp, saved);

    lock.lock();
}

};
