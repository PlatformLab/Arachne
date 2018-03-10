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
#include "PerfUtils/TimeTrace.h"

namespace Arachne {

using PerfUtils::TimeTrace;

void removeThreadsFromCore(CoreList* outputCores);
void releaseCore(CoreList* outputCores);
void decrementCoreCount();
void incrementCoreCount();
extern std::vector<uint64_t*> lastTotalCollectionTime;

DefaultCoreManager::DefaultCoreManager(int minNumCores, int maxNumCores,
                                       bool estimateLoad)
    : minNumCores(minNumCores),
      maxNumCores(maxNumCores),
      loadEstimator(maxNumCores),
      lock(false),
      sharedCores(maxNumCores),
      exclusiveCores(maxNumCores),
      coreAdjustmentShouldRun(estimateLoad),
      coreAdjustmentThreadStarted(false) {}

/**
 * Add the given core to the pool of threads for general scheduling.
 */
void
DefaultCoreManager::coreAvailable(int myCoreId) {
    Lock guard(lock);
    sharedCores.add(myCoreId);
    if (!coreAdjustmentThreadStarted && coreAdjustmentShouldRun) {
        if (Arachne::createThread(&DefaultCoreManager::adjustCores, this) ==
            Arachne::NullThread) {
            ARACHNE_LOG(ERROR, "Failed to create thread to adjustCores!");
            exit(1);
        }
        coreAdjustmentThreadStarted = true;
    }
}

/**
 * Record the loss of a core and return its coreId. Returns -1 if there are no
 * cores available for descheduling.
 */
int
DefaultCoreManager::coreUnavailable() {
    Lock guard(lock);
    if (sharedCores.size() == 0) {
        return -1;
    }
    int freeCoreId = sharedCores[sharedCores.size() - 1];
    sharedCores.remove(sharedCores.size() - 1);
    return freeCoreId;
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
 * Provide a set of cores for Arachne to migrate threads to when cleaning up a
 * core for return to the CoreArbiter.
 */
CoreList*
DefaultCoreManager::getMigrationTargets() {
    return &sharedCores;
}

/**
 * After this function returns, load estimations that have already begun
 * will complete, but no future load estimations will occur.
 */
void
DefaultCoreManager::disableLoadEstimation() {
    coreAdjustmentShouldRun.store(false);
}

CoreLoadEstimator*
DefaultCoreManager::getEstimator() {
    return &loadEstimator;
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

/**
 * This is the main function for a thread which periodically evaluates load and
 * determines whether to adjust cores between threadClasses, and/or increase or
 * decrease the total number of cores used by Arachne.
 */
void
DefaultCoreManager::adjustCores() {
    while (coreAdjustmentShouldRun.load()) {
        TimeTrace::record("Core manager about to sleep");
        Arachne::sleep(measurementPeriod);
        TimeTrace::record("Core manager starts adjusting cores");
        Lock guard(lock);
        int estimate = loadEstimator.estimate(sharedCores.size());
        if (estimate == 0)
            continue;
        if (estimate == -1) {
            if (sharedCores.size() > 1)
                decrementCoreCount();
            continue;
        }
        // Estimator believes we need more cores
        // First, see if any exclusive cores are available for turning back to
        // shared. Note that this transition might race with an exclusive thread
        // creation that just received an exclusive core, but such a race is
        // safe as long as it results only in the failure of the exclusive
        // thread creation.
        for (uint32_t i = 0; i < exclusiveCores.size(); i++) {
            int coreId = exclusiveCores[i];
            MaskAndCount slotMap = *occupiedAndCount[coreId];
            if (slotMap.numOccupied == maxThreadsPerCore - 1) {
                // Attempt to reclaim this core with a CAS. Only move back
                // to sharedCores if we succeed.
                MaskAndCount oldSlotMap = slotMap;
                slotMap.numOccupied = maxThreadsPerCore;
                if (occupiedAndCount[coreId]->compare_exchange_strong(
                        oldSlotMap, slotMap)) {
                    exclusiveCores.remove(i);
                    *lastTotalCollectionTime[coreId] = 0;
                    *occupiedAndCount[coreId] = {0, 0};
                    sharedCores.add(coreId);
                    continue;
                }
            }
        }
        // Then try to incrementCoreCount the traditional way.
        incrementCoreCount();
    }
}
}  // namespace Arachne
