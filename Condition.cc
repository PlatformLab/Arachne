#include "Condition.h"
#include "TimeTrace.h"

namespace  Arachne {

using PerfUtils::TimeTrace;
    
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
    TimeTrace::record("Retrieved UserContext!");
    workQueueLocks[targetCoreId].lock();
    TimeTrace::record("Acquired workQueueLock!");
    workQueues[targetCoreId].push_back(awakenedThread);
    TimeTrace::record("Added thread pointer to ready queue!");
    workQueueLocks[targetCoreId].unlock();
    TimeTrace::record("Released workQueueLocks!");
}
void condition_variable::notify_all() {
    while (!blockedThreads.empty())
        notify_one();
}
void condition_variable::wait(SpinLock& lock) {
    TimeTrace::record("Wait on Core %d", kernelThreadId);
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
    TimeTrace::record("Finished checking for new threads on core %d", kernelThreadId);

    workQueueLocks[kernelThreadId].lock();
    if (workQueues[kernelThreadId].empty()) {
        workQueueLocks[kernelThreadId].unlock();
        // Create an idle thread that runs the main scheduler loop and swap to it, so
        // we're ready for new work with a new stack as soon as it becomes
        // available.
        createNewRunnableThread();
        TimeTrace::record("Woke up after regaining control on core %d", kernelThreadId);
        lock.lock();
        TimeTrace::record("Reacquired lock on core %d", kernelThreadId);
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
