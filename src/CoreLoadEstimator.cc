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
#include "CoreLoadEstimator.h"

#include <thread>

namespace Arachne {

CoreLoadEstimator::CoreLoadEstimator()
    : lock("CoreLoadEstimator", false),
      utilizationThresholds(std::thread::hardware_concurrency()) {}
CoreLoadEstimator::~CoreLoadEstimator() {}

/**
 * Returns -1,0,1 to suggest whether the core count should decrease,
 * stay the same, or increase respectively.
 *
 * \param coreList
 *    The list of cores over which to perform the estimation.
 */
int
CoreLoadEstimator::estimate(CorePolicy::CoreList coreList) {
    Lock guard(lock);
    int curActiveCores = coreList.size();
    // Use collectionTime as a proxy to tell whether PerfStats have been
    // previously recorded.
    if (previousStats.collectionTime == 0) {
        Arachne::PerfStats::collectStats(&previousStats, coreList);
        return 0;
    }
    Arachne::PerfStats currentStats;
    Arachne::PerfStats::collectStats(&currentStats, coreList);

    // Evaluate idle time precentage multiplied by number of cores to
    // determine whether we need to decrease the number of cores.
    uint64_t idleCycles = currentStats.idleCycles - previousStats.idleCycles;
    uint64_t totalCycles = currentStats.totalCycles - previousStats.totalCycles;
    uint64_t utilizedCycles = totalCycles - idleCycles;
    uint64_t totalMeasurementCycles =
        currentStats.collectionTime - previousStats.collectionTime;
    double totalUtilizedCores = static_cast<double>(utilizedCycles) /
                                static_cast<double>(totalMeasurementCycles);

    // Estimate load to determine whether we need to increment the number
    // of cores.
    uint64_t weightedLoadedCycles =
        currentStats.weightedLoadedCycles - previousStats.weightedLoadedCycles;
    double averageLoadFactor = static_cast<double>(weightedLoadedCycles) /
                               static_cast<double>(totalCycles);
    previousStats = currentStats;

    if (estimationStrategy == LOAD_FACTOR) {
        // Scale down if the core utilization after scale down is greater than
        // the core utilization at which we scaled up, plus a hysteresis
        // threshold.
        double localThreshold =
            utilizationThresholds[curActiveCores - 1] -
            idleCoreFractionHysteresis * (curActiveCores - 1);

        ARACHNE_LOG(DEBUG,
                    "curActiveCores = %d, totalUtilizedCores = %lf, "
                    "localThreshold = %lf, averageloadFactor = %lf, "
                    "loadFactorThreshold = %lf\n",
                    curActiveCores, totalUtilizedCores, localThreshold,
                    averageLoadFactor, loadFactorThreshold);

        if (static_cast<size_t>(curActiveCores) <
                utilizationThresholds.size() &&
            averageLoadFactor > loadFactorThreshold) {
            // Record our current totalUtilizedCores, so we will only ramp down
            // if utilization would drop below this level.
            utilizationThresholds[curActiveCores] = totalUtilizedCores;
            ARACHNE_LOG(NOTICE,
                        "Recommending increase core count: curActiveCores = "
                        "%d, totalUtilizedCores = %lf, localThreshold = %lf, "
                        "averageloadFactor = %lf, loadFactorThreshold = %lf\n",
                        curActiveCores, totalUtilizedCores, localThreshold,
                        averageLoadFactor, loadFactorThreshold);
            return 1;
        }
        localThreshold = std::max(zeroCoreUtilizationThreshold, localThreshold);
        if ((totalUtilizedCores < localThreshold)) {
            ARACHNE_LOG(NOTICE,
                        "Recommending decrease core count: curActiveCores = "
                        "%d, totalUtilizedCores = %lf, localThreshold = %lf, "
                        "averageloadFactor = %lf, loadFactorThreshold = %lf\n",
                        curActiveCores, totalUtilizedCores, localThreshold,
                        averageLoadFactor, loadFactorThreshold);
            return -1;
        }
        return 0;
    } else if (estimationStrategy == UTILIZATION) {
        ARACHNE_LOG(DEBUG,
                    "curActiveCores = %d, totalUtilizedCores = %lf, "
                    "maxUtilization = %lf\n",
                    curActiveCores, totalUtilizedCores, maxUtilization);

        if (totalUtilizedCores > maxUtilization * curActiveCores) {
            ARACHNE_LOG(NOTICE,
                        "Recommending increase core count: curActiveCores = "
                        "%d, totalUtilizedCores = %lf, maxUtilization = %lf\n",
                        curActiveCores, totalUtilizedCores, maxUtilization);
            return 1;
        }
        if (totalUtilizedCores < maxUtilization * (curActiveCores - 1) -
                                     idleCoreFractionHysteresis) {
            ARACHNE_LOG(NOTICE,
                        "Recommending decrease core count: curActiveCores = "
                        "%d, totalUtilizedCores = %lf, maxUtilization = %lf\n",
                        curActiveCores, totalUtilizedCores, maxUtilization);
            return -1;
        }
        return 0;
    }
    // We have an unknown estimation strategy, so we do nothing.
    ARACHNE_LOG(ERROR,
                "CoreLoadEstimator has an invalid estimationStrategy; cowardly "
                "refusing to recommend change.");
    return 0;
}

/**
 * This function causes the estimator to behave as if running for the first
 * time, with no prior history.
 */
void
CoreLoadEstimator::clearHistory() {
    previousStats.collectionTime = 0;
}

/**
 * Invoking this function will set the load factor threshold and also
 * change the load estimation strategy to use load factor.
 */
void
CoreLoadEstimator::setLoadFactorThreshold(double loadFactorThreshold) {
    Lock guard(lock);
    this->loadFactorThreshold = loadFactorThreshold;
    this->estimationStrategy = LOAD_FACTOR;
}

/**
 * Invoking this function will set the max utilization threshold and also
 * change the load estimation strategy to use utilization.
 */
void
CoreLoadEstimator::setMaxUtilization(double maxUtilization) {
    Lock guard(lock);
    this->maxUtilization = maxUtilization;
    this->estimationStrategy = UTILIZATION;
}
}  // namespace Arachne
