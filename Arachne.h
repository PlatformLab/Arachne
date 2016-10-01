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

#ifndef ARACHNE_H_
#define ARACHNE_H_

#include <assert.h>
#include <functional>
#include <vector>
#include <mutex>
#include <deque>
#include <atomic>


#include "Common.h"
#include "ArachnePrivate.h"

namespace  Arachne {

extern volatile unsigned numCores;


/**
  * Spawn a new thread.
  * TODO: Fix this documentation for the user.
  * TODO: Return a ThreadID with a distinguished value (Arachne::NullThread) for failure.
  */
template<typename _Callable, typename... _Args>
    int createThread(_Callable&& __f, _Args&&... __args) {

    // Find a core to enqueue to by picking two at random and choose the one
    // with the fewest threads.

    int coreId;
    int choice1 = random() % numCores;
    int choice2 = random() % numCores;
    while (choice2 == choice1) choice2 = random() % numCores;

    if (occupiedAndCount[choice1].load().numOccupied <
            occupiedAndCount[choice2].load().numOccupied)
        coreId = choice1;
    else
        coreId = choice2;

    return createThread(coreId, __f, __args...);
}

void threadInit();
void mainThreadJoinPool();
void yield();
void sleep(uint64_t ns);
void block();
void signal(ThreadId id);
bool join(ThreadId id);
ThreadId getThreadId();

} // namespace Arachne
#endif // ARACHNE_H_
