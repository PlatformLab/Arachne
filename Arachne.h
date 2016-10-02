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

#include "ArachnePrivate.h"
#include "Common.h"

namespace Arachne {

/**
  * These configuration parameters should be set before calling threadInit and
  * are documented with threadInit.
  */
extern volatile uint32_t numCores;

/**
  * This value is returned by createThread when there are not enough resources
  * to create a new thread.
  */
const Arachne::ThreadId NullThread = NULL;

/**
  * Spawn a thread with main function f invoked with the given args on the core
  * with coreId. Pass in -1 for coreId to use the creator's core. This can be
  * useful if the created thread will share a lot of state with the current
  * thread, since it will improve locality.
  *
  * This function should usually only be invoked directly in tests, since it
  * does not perform load balancing. However, it can also be used if the
  * application wants to do its own load balancing.
  *
  * \param __f
  *     The main function for the new thread.
  * \param __args
  *     The arguments for __f.
  * \return
  *     The return value is an identifier which can be passed to other
  *     functions as an identifier.
  */
template<typename _Callable, typename... _Args>
ThreadId createThread(int coreId, _Callable&& __f, _Args&&... __args) {
    if (coreId == -1) coreId = kernelThreadId;

    auto task = std::bind(
            std::forward<_Callable>(__f), std::forward<_Args>(__args)...);

    bool success;
    int index;
    do {
        // Attempt to enqueue the task to the specific core in this case.
        MaskAndCount slotMap = occupiedAndCount[coreId];
        MaskAndCount oldSlotMap = slotMap;

        // Search for a non-occupied slot and attempt to reserve the slot
        index = 0;
        while ((slotMap.occupied & (1L << index)) && index < maxThreadsPerCore)
            index++;

        if (index == maxThreadsPerCore) {
            return NullThread;
        }

        slotMap.occupied |= (1L << index);
        slotMap.numOccupied++;

        success = occupiedAndCount[coreId].compare_exchange_strong(oldSlotMap,
                slotMap);
    } while (!success);

    // Copy the thread invocation into the byte array.
    new (&activeLists[coreId][index].threadInvocation)
        Arachne::ThreadInvocation<decltype(task)>(task);
    activeLists[coreId][index].wakeup = true;

    return &activeLists[coreId][index];
}

/**
  * Spawn a new thread with a function and arguments. The total size of the
  * arguments cannot exceed 48 bytes, and arguments are taken by value, so any
  * reference must be wrapped with std::ref.
  *
  * \param __f
  *     The main function for the new thread.
  * \param __args
  *     The arguments for __f.
  * \return
  *     The return value is an identifier which can be passed to other
  *     functions as an identifier.
  */
template<typename _Callable, typename... _Args>
ThreadId createThread(_Callable&& __f, _Args&&... __args) {

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
