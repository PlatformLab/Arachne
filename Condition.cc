#include "Condition.h"
#include "TimeTrace.h"

namespace  Arachne {

using PerfUtils::TimeTrace;
    
condition_variable::condition_variable() { }
condition_variable::~condition_variable() { }

void 
condition_variable::notify_one() {
    if (blockedThreads.empty()) return;
    UserContext *awakenedThread = blockedThreads.front();
    blockedThreads.pop_front();
    TimeTrace::record("Retrieved UserContext!");
    awakenedThread->state = RUNNABLE;
    TimeTrace::record("Added thread pointer to ready queue!");
}

void condition_variable::notify_all() {
    while (!blockedThreads.empty())
        notify_one();
}

void condition_variable::wait(SpinLock& lock) {
    TimeTrace::record("Wait on Core %d", kernelThreadId);
    // Put my thread on the queue.
    running->state = BLOCKED;
    blockedThreads.push_back(running);

    lock.unlock();

    auto& maybeRunnable = possiblyRunnableThreads[kernelThreadId];
    // Poll for incoming task.
    // TODO(hq6): Decide whether this function should actually yield immediately.
    if (taskBoxes[kernelThreadId].data.loadState.load() == FILLED) {
        maybeRunnable.push_back(running);
        createNewRunnableThread();
        lock.lock();
        return;
    }
    TimeTrace::record("Finished checking for new threads on core %d", kernelThreadId);
    // Find a thread to switch to
    for (size_t i = 0; i < maybeRunnable.size(); i++) {
        if (maybeRunnable[i]->state == RUNNABLE) {
            void** saved = &running->sp;

            maybeRunnable.push_back(running);
            running = maybeRunnable[i];
            maybeRunnable.erase(maybeRunnable.begin() + i);

            swapcontext(&running->sp, saved);
            lock.lock();
            return;
        }
    }

    // Create an idle thread that runs the main scheduler loop and swap to it, so
    // we're ready for new work with a new stack as soon as it becomes
    // available.
    maybeRunnable.push_back(running);
    createNewRunnableThread();
    lock.lock();
}

};
