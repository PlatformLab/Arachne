/* Copyright (c) 2017-2018 Stanford University
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

#include "DefaultCorePolicy.h"
#include <atomic>
#include "Arachne.h"

namespace Arachne {

// Forward declarations
void prepareForExclusiveUse(int coreId);
int findAndClaimUnusedCore(CorePolicy::CoreList* cores);
void setCoreCount(uint32_t desiredNumCores);
extern std::vector<uint64_t*> lastTotalCollectionTime;

// Constructor
//
// \param maxNumCores
//     The largest number of cores the application will ever require.
// \param estimateLoad
//     True means that this core estimator will estimate load and adjust the
//     number of cores.
DefaultCorePolicy::DefaultCorePolicy(int maxNumCores, bool estimateLoad)
    : maxNumCores(maxNumCores),
      loadEstimator(),
      lock("DefaultCorePolicy", false),
      sharedCores(maxNumCores),
      exclusiveCores(maxNumCores),
      coreAdjustmentShouldRun(estimateLoad),
      coreAdjustmentThreadStarted(false) {}

/**
 * See documentation in CorePolicy.
 */
void
DefaultCorePolicy::coreAvailable(int myCoreId) {
    Lock guard(lock);
    sharedCores.add(myCoreId);
    if (!coreAdjustmentThreadStarted && coreAdjustmentShouldRun) {
        if (Arachne::createThread(&DefaultCorePolicy::adjustCores, this) ==
            Arachne::NullThread) {
            ARACHNE_LOG(ERROR, "Failed to create thread to adjustCores!");
            abort();
        }
        coreAdjustmentThreadStarted = true;
    }
    loadEstimator.clearHistory();
}

/**
 * See documentation in CorePolicy.
 */
void
DefaultCorePolicy::coreUnavailable(int coreId) {
    Lock guard(lock);
    int index = sharedCores.find(coreId);
    if (index != -1) {
        sharedCores.remove(index);
        loadEstimator.clearHistory();
        return;
    }
    ARACHNE_LOG(ERROR,
                "Tried to remove core %d, unknown by CorePolicy or held "
                "exclusively by a thread.\n",
                coreId);
    abort();
}

/**
 * See documentation in CorePolicy.
 */
CorePolicy::CoreList
DefaultCorePolicy::getCores(int threadClass) {
    switch (threadClass) {
        case DEFAULT:
            return sharedCores;
        case EXCLUSIVE:
            int coreId = getExclusiveCore();
            if (coreId < 0)
                break;
            CorePolicy::CoreList retVal(1, /*mustFree=*/true);
            retVal.add(coreId);
            return retVal;
    }
    CorePolicy::CoreList retVal(0, /*mustFree=*/true);
    return retVal;
}

/**
 * After this function returns, load estimations that have already begun
 * will complete, but no future load estimations will occur.
 */
void
DefaultCorePolicy::disableLoadEstimation() {
    coreAdjustmentShouldRun.store(false);
}

/**
 * After this function returns, load estimation will resume normal operation.
 */
void
DefaultCorePolicy::enableLoadEstimation() {
    coreAdjustmentShouldRun.store(true);
}

CoreLoadEstimator*
DefaultCorePolicy::getEstimator() {
    return &loadEstimator;
}

/**
 * Find or allocate a core for exclusive use by a thread.
 * Existing threads may be migrated to make a core exclusive.
 */
int
DefaultCorePolicy::getExclusiveCore() {
    Lock guard(lock);
    // Attempt to pick up an exclusive core whose host thread has expired.
    int coreId = findAndClaimUnusedCore(&exclusiveCores);
    if (coreId == -1) {
        // Take the oldest core to be an exclusive core, so it is relinquished
        // last. No cores available, return failure.
        if (sharedCores.size() == 0) {
            return -1;
        }
        coreId = sharedCores[0];
        sharedCores.remove(0);
    }
    exclusiveCores.add(coreId);
    prepareForExclusiveUse(coreId);
    return coreId;
}

/**
 * This is the main function for a thread which periodically evaluates load and
 * determines whether to adjust cores between threadClasses, and/or increase or
 * decrease the total number of cores used by Arachne.
 */
void
DefaultCorePolicy::adjustCores() {
    while (true) {
        Arachne::sleep(measurementPeriod);
        if (!coreAdjustmentShouldRun.load()) {
            loadEstimator.clearHistory();
            continue;
        }
        Lock guard(lock);
        int estimate = loadEstimator.estimate(sharedCores);
        if (estimate == 0)
            continue;
        if (estimate == -1) {
            if (sharedCores.size() > 1)
                setCoreCount(Arachne::numActiveCores - 1);
            continue;
        }
        // Estimator believes we need more cores.
        // First, see if any exclusive cores are available for turning back to
        // shared. Note that this transition might race with an exclusive thread
        // creation that just received an exclusive core, but such a race is
        // safe as long as it results only in the failure of the exclusive
        // thread creation.
        int coreId = findAndClaimUnusedCore(&exclusiveCores);
        if (coreId != -1) {
            sharedCores.add(coreId);
            continue;
        }

        // Then try to incrementCoreCount the traditional way.
        setCoreCount(Arachne::numActiveCores + 1);
    }
}
}  // namespace Arachne
