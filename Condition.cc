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

    // Run the main scheduler loop in the context of this thread, since this is
    // the last thread that was runnable.
    block();
    TimeTrace::record("About to acquire lock after waking up");
    lock.lock();
}

};
