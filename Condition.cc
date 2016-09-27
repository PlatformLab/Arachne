#include "Condition.h"
#include "TimeTrace.h"

namespace  Arachne {

using PerfUtils::TimeTrace;

ConditionVariable::ConditionVariable() { }
ConditionVariable::~ConditionVariable() { }

/**
  * Awaken one of the threads that waited on this condition variable.
  * The caller should have the associated mutex held.
  */
void ConditionVariable::notify_one() {
    if (blockedThreads.empty()) return;
    UserContext *awakenedThread = blockedThreads.front();
    blockedThreads.pop_front();
    awakenedThread->wakeup = true;
}

/**
  * Awaken all of the threads currently waiting on this condition varible.
  * The caller should have the associated mutex held.
  */
void ConditionVariable::notify_all() {
    while (!blockedThreads.empty())
        notify_one();
}

/**
  * Block the current thread until the condition variable is notified.
  * This function releases the lock before blocking, and re-acquires it before
  * returning to the user.
  */
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
