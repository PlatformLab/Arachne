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

#include <stdio.h>
#include <thread>
#include "PerfUtils/TimeTrace.h"
#include "PerfUtils/Util.h"
#include "PerfUtils/Cycles.h"
#include "CoreArbiter/CoreArbiterClient.h"

#include "Arachne.h"
#include "CorePolicy.h"
#include "PerfStats.h"
#include "CoreArbiter/ArbiterClientShim.h"

using PerfUtils::Cycles;

/*
 * We will attempt to increase the number of cores if the idle core
 * fraction (computed as idlePercentage * numSharedCores) is less than this
 * number.
 */
const double maxIdleCoreFraction = 0.1;

/*
 * We will attempt to increase the number of cores if the load factor
 * increases beyond this threshold.
 */
const double loadFactorThreshold = 1.0;

/*
 * Save the core fraction at which we ramped up based on load factor, so we
 * can decide whether to ramp down.  Allocated in bootstrapLoadEstimator.
 */
double *utilizationThresholds = NULL;

/*
 * The difference in load, expressed as a fraction of a core, between a
 * ramp-down threshold and the corresponding ramp-up threshold (i.e., we
 * wait to ramp down until the load gets a bit below the point at 
 * which we ramped up).
 */
const double idleCoreFractionHysteresis = 0.2;

/*
 * Do not ramp down if the percentage of occupied threadContext slots is
 * above this threshold.
 */
const double SLOT_OCCUPANCY_THRESHOLD = 0.5;

/*
 * The period in ns over which we measure before deciding to reduce the number
 * of cores we use.
 */
const uint64_t MEASUREMENT_PERIOD = 50 * 1000 * 1000;

/* Is the core load estimator running? */
bool loadEstimatorRunning = false;

void coreLoadEstimator(CorePolicy* corePolicy);

/*
 * Choose a core to deschedule.  Return its coreId.
 */
int CorePolicy::chooseRemovableCore() {
    CoreList* entry = threadClassCoreMap[defaultClass];
    uint8_t minLoaded =
        Arachne::occupiedAndCount[entry->map[0]]->load().numOccupied;
    int minCoreId = entry->map[0];
    for (uint32_t i = 1; i < threadClassCoreMap[defaultClass]->numFilled; i++) {
        uint32_t coreId = entry->map[i];
        if (Arachne::occupiedAndCount[coreId]->load().numOccupied < minLoaded) {
            minLoaded = Arachne::occupiedAndCount[coreId]->load().numOccupied;
            minCoreId = coreId;
        }
    }
    return minCoreId;
}

/*
 * Update threadClassCoreMap when a new core is added.  Callers must guarantee
 * that this function is not called multiple times simultaneously, or at the
 * same time as removeCore.
 *
 * This function will also bootstrap the coreLoadEstimator as soon as it has
 * a core.
 *
 * \param coreId
 *     The coreId of the new core.
 */
void CorePolicy::addCore(int coreId) {
  CoreList* entry = threadClassCoreMap[defaultClass];
  entry->map[entry->numFilled] = coreId;
  entry->numFilled++;
  if (!loadEstimatorRunning && !Arachne::disableLoadEstimation) {
    loadEstimatorRunning = true;
    runLoadEstimator();
  }
}

/*
 * Update threadClassCoreMap when a new core is removed.  Callers must
 * guarantee that this function is not called multiple times simultaneously,
 * or at the same time as addCore.
 *
 * \param coreId
 *     The coreId of the doomed core.
 */
void CorePolicy::removeCore(int coreId) {
  CoreList* entry = threadClassCoreMap[defaultClass];
  entry->numFilled--;
  int saveCoreId = entry->map[entry->numFilled];
  for (uint32_t i = 0; i < entry->numFilled; i++) {
    if (entry->map[i] == coreId) {
      entry->map[i] = saveCoreId;
      return;
    }
  }
}

/*
 * Return the CoreList for a particular threadClass.  This function is not
 * synchronized with addCore and removeCore and can return stale data.
 * Callers of this function must ensure that use of stale data is safe.
 *
 * \param threadClass
 *     The threadClass whose CoreList will be returned.
 */
CoreList* CorePolicy::getCoreList(ThreadClass threadClass) {
  return threadClassCoreMap[threadClass];
}

/*
 * Bootstrap the core load estimator thread.  Called from addCore.
 */
void CorePolicy::runLoadEstimator() {
    Arachne::createThread(defaultClass, coreLoadEstimator, this);
}

/*
 * Periodically wake up and observe the current load in Arachne to determine
 * whether it is necessary to increase or reduce the number of cores used by
 * Arachne.  Runs as the top-level method in an Arachne thread.
 *
 * \param corePolicy
 *     Pointer to the CorePolicy which created this thread.
 */
void coreLoadEstimator(CorePolicy* corePolicy) {
    utilizationThresholds = new double[Arachne::maxNumCores];
    Arachne::PerfStats previousStats;
    Arachne::PerfStats::collectStats(&previousStats);

    // Each loop cycle makes measurements and uses them to evaluate whether
    // to increment the core count, decrement it, or do nothing.
    for (Arachne::PerfStats currentStats; ; previousStats = currentStats) {
        Arachne::sleep(MEASUREMENT_PERIOD);
        Arachne::PerfStats::collectStats(&currentStats);

        // Take a snapshot of currently active cores before performing
        // estimation to avoid races between estimation and the fulfillment of
        // a previous core request.
        uint32_t curActiveCores =
          corePolicy->getCoreList(corePolicy->defaultClass)->numFilled;

        // Evaluate idle time precentage multiplied by number of cores to
        // determine whether we need to decrease the number of cores.
        uint64_t idleCycles =
            currentStats.idleCycles - previousStats.idleCycles;
        uint64_t totalCycles =
            currentStats.totalCycles - previousStats.totalCycles;
        uint64_t utilizedCycles = totalCycles - idleCycles;
        uint64_t totalMeasurementCycles =
            Cycles::fromNanoseconds(MEASUREMENT_PERIOD);
        double totalUtilizedCores =
            static_cast<double>(utilizedCycles) /
            static_cast<double>(totalMeasurementCycles);

        // Estimate load to determine whether we need to increment the number
        // of cores.
        uint64_t weightedLoadedCycles =
            currentStats.weightedLoadedCycles -
            previousStats.weightedLoadedCycles;
        double averageLoadFactor =
            static_cast<double>(weightedLoadedCycles) /
            static_cast<double>(totalCycles);
        if (curActiveCores < Arachne::maxNumCores &&
                averageLoadFactor > loadFactorThreshold) {
            // Record our current totalUtilizedCores, so we will only ramp down
            // if utilization would drop below this level.
            utilizationThresholds[curActiveCores] = totalUtilizedCores;
            Arachne::incrementCoreCount();
            continue;
        }

        // We should not ramp down if we have high occupancy of slots.
        double averageNumSlotsUsed = static_cast<double>(
                currentStats.numThreadsCreated -
                currentStats.numThreadsFinished) /
                curActiveCores / Arachne::maxThreadsPerCore;

        // Scale down if the idle time after scale down is greater than the
        // time at which we scaled up, plus a hysteresis threshold.
        if (totalUtilizedCores < utilizationThresholds[curActiveCores - 1]
                - idleCoreFractionHysteresis &&
                averageNumSlotsUsed < SLOT_OCCUPANCY_THRESHOLD) {
            Arachne::decrementCoreCount();
        }
    }
}

/*
 * Block a core in the kernel.
 *
 * \param coreId
 *     The coreId of the core that will block
 */
void CoreBlocker::blockCore(int coreId) {
    std::mutex cvMutex;
    std::unique_lock<std::mutex> lk(cvMutex);
    std::condition_variable* cv = cvArray[coreId];
    isSleepingArray[coreId] = true;
    cv->wait(lk);
    isSleepingArray[coreId] = false;
}

/*
 * Unblock a core that is currently blocking.
 *
 * \param coreId
 *     The coreId of the core that will be unblocked.
 */
void CoreBlocker::unblockCore(int coreId) {
    std::condition_variable* cv = cvArray[coreId];
    while (isSleepingArray[coreId])
        cv->notify_one();
}
