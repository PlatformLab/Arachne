/* Copyright (c) 2015-2016 Stanford University
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR(S) DISCLAIM ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL AUTHORS BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "Condition.h"
#include "TimeTrace.h"

namespace Arachne {

using PerfUtils::TimeTrace;

ConditionVariable::ConditionVariable()
    : blockedThreads() {}

ConditionVariable::~ConditionVariable() { }

/**
  * Awaken one of the threads waiting on this condition variable.
  * The caller should have the associated mutex held.
  */
void ConditionVariable::notifyOne() {
    if (blockedThreads.empty()) return;
    ThreadContext *awakenedThread = blockedThreads.front();
    blockedThreads.pop_front();
    awakenedThread->wakeupTimeInCycles = 0;
}

/**
  * Awaken all of the threads waiting on this condition variable.
  * The caller should have the associated mutex held.
  */
void ConditionVariable::notifyAll() {
    while (!blockedThreads.empty())
        notifyOne();
}

/**
  * Block the current thread until the condition variable is notified.
  * This function releases the lock before blocking, and re-acquires it before
  * returning to the user.
  *
  * \param lock
  *     The mutex associated with this condition variable; should be held by
  *     caller before calling wait.
  */
void ConditionVariable::wait(SpinLock& lock) {
    TimeTrace::record("Wait on Core %d", kernelThreadId);
    blockedThreads.push_back(runningContext);
    lock.unlock();
    block();
    TimeTrace::record("About to acquire lock after waking up");
    lock.lock();
}
} // namespace Arachne
