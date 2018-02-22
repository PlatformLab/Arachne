/* Copyright (c) 2017 Stanford University
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

#include "DefaultCoreManager.h"
#include <atomic>
#include "Arachne.h"

namespace Arachne {

void removeThreadsFromCore(CoreList* outputCores);

DefaultCoreManager::DefaultCoreManager(int minNumCores, int maxNumCores)
    : minNumCores(minNumCores),
      maxNumCores(maxNumCores),
      loadEstimator(),
      lock(false),
      sharedCores(maxNumCores),
      exclusiveCores(maxNumCores) {}

/**
 * Add the given core to the pool of threads for general scheduling.
 */
void
DefaultCoreManager::coreAvailable(int myCoreId) {
    Lock guard(lock);
    sharedCores.add(myCoreId);
}

/**
 * Prevent further creations on and remove all threads from the most
 * recently acquired core, and schedule releaseCore() on it. Log an error
 * and abort the process if loss of a core results in no cores available
 * for general scheduling.
 */
void
DefaultCoreManager::coreUnavailable() {
    Lock guard(lock);
    // TODO(hq6): Implement this function
}

/**
 * Invoked by Arachne::createThread to get cores available for scheduling of
 * short-lived tasks. Returns NULL if an invalid threadClass is passed in.
 */
CoreList*
DefaultCoreManager::getCores(int threadClass) {
    switch (threadClass) {
        case DEFAULT:
            return &sharedCores;
        case EXCLUSIVE:
            int coreId = getExclusiveCore();
            if (coreId < 0)
                break;
            CoreList* retVal = new CoreList(1, /*mustFree=*/true);
            retVal->add(coreId);
            return retVal;
    }
    return NULL;
}

/**
 * After this function returns, no load estimations that have already begun
 * will complete, but no future load estimations will occur.
 */
void
DefaultCoreManager::disableLoadEstimation() {
    // TODO(hq6): Implement this function
}

/**
 * Find or allocate a core for exclusive use by a thread.
 */
int
DefaultCoreManager::getExclusiveCore() {
    Lock guard(lock);
    // Look for an existing idle exclusive core
    for (uint32_t i = 0; i < exclusiveCores.size(); i++) {
        if (occupiedAndCount[exclusiveCores[i]]->load().occupied == 0) {
            // Enable scheduling on this core again
            *occupiedAndCount[exclusiveCores[i]] = {0, 0};
            return exclusiveCores[i];
        }
    }
    // Failed to find one; make one instead from the shared cores.
    // Take the oldest core to be an exclusive core, so it is relinquished last.
    // No cores available, return failure.
    if (sharedCores.size() == 0) {
        return -1;
    }
    int newExclusiveCore = sharedCores[0];
    exclusiveCores.add(newExclusiveCore);
    sharedCores.remove(0);

    ThreadId migrationThread = createThreadOnCore(
        newExclusiveCore, removeThreadsFromCore, &sharedCores);
    // The current thread is a non-Arachne thread.
    if (core.kernelThreadId == -1) {
        // Polling for completion is a short-term hack until we figure out a
        // good story for joining Arachne threads from non-Arachne threads.
        while (Arachne::occupiedAndCount[newExclusiveCore]->load().occupied)
            usleep(10);
    } else {
        Arachne::join(migrationThread);
    }

    // Prepare this core for scheduling exclusively.
    // By setting numOccupied to one less than the maximium number of threads
    // per core, we ensure that only one thread gets scheduled onto this core.
    *occupiedAndCount[newExclusiveCore] = {0, maxThreadsPerCore - 1};
    return newExclusiveCore;
}

}  // namespace Arachne
