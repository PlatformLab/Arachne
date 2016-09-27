#include "Condition.h"
#include "TimeTrace.h"

namespace  Arachne {

using PerfUtils::TimeTrace;
    
ConditionVariable::ConditionVariable() { }
ConditionVariable::~ConditionVariable() { }

void 
ConditionVariable::notify_one() {
    if (blockedThreads.empty()) return;
    UserContext *awakenedThread = blockedThreads.front();
    blockedThreads.pop_front();
    awakenedThread->wakeup = true;
}

void ConditionVariable::notify_all() {
    while (!blockedThreads.empty())
        notify_one();
}

void ConditionVariable::wait(SpinLock& lock) {
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
