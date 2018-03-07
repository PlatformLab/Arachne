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

#ifndef ARACHNE_CONDITION_H
#define ARACHNE_CONDITION_H

#include <queue>
#include "SpinLock.h"
#include "Arachne.h"


namespace  Arachne {

/**
  * This class implements a subset of the functionality of
  * std::condition_variable.
  * It takes no internal locks, so is assumed that notifications are done with
  * the associated mutex held.
  */
class ConditionVariable {
 public:
    ConditionVariable();
    ~ConditionVariable();
    void notify_one();
    void notify_all();
    void wait(SpinLock& lock);
 private:
    std::deque<ThreadContext*> blockedThreads;
    DISALLOW_COPY_AND_ASSIGN(ConditionVariable);
};

} // namespace Arachne
#endif
