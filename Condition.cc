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
    awakenedThread->wakeup = true;
}

void condition_variable::notify_all() {
    while (!blockedThreads.empty())
        notify_one();
}

void condition_variable::wait(SpinLock& lock) {
    TimeTrace::record("Wait on Core %d", kernelThreadId);
    // Put my thread on the queue.
    blockedThreads.push_back(running);

    lock.unlock();

    TimeTrace::record("Finished checking for new threads on core %d", kernelThreadId);

    // Find a thread to switch to
    auto& activeList = *maybeRunnable;
    for (size_t i = 0; i < activeList.size(); i++) {
        if (activeList[i]->wakeup) {
            // If the blocked context is our own, we can simply return.
            if (activeList[i] == running) {
                TimeTrace::record("About to acquire lock after waking up");
                lock.lock();
                running->wakeup = false;
                return;
            }
            void** saved = &running->sp;

            running = activeList[i];

            swapcontext(&running->sp, saved);
            TimeTrace::record("About to acquire lock after waking up");
            lock.lock();
            running->wakeup = false;
            return;
        }
    }

    // Run the main scheduler loop in the context of this thread, since this is
    // the last thread that was runnable.
    block();
    TimeTrace::record("About to acquire lock after waking up");
    lock.lock();
}

};
