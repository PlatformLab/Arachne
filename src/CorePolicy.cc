/* Copyright (c) 2015-2017 Stanford University
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

/**
  * Arachne will attempt to increase the number of cores if the idle core
  * fraction (computed as idlePercentage * numSharedCores) is less than this
  * number.
  */
double maxIdleCoreFraction = 0.1;

/**
  * Arachne will attempt to increase the number of cores if the load factor
  * increase beyond this threshold.
  */
double loadFactorThreshold = 1.0;

/**
  * Only here to satisfy compiling benchmark.
  */
double maxUtilization = 0.9;

/**
  * Save the core fraction at which we ramped up based on load factor, so we
  * can decide whether to ramp down.
  */
double *utilizationThresholds;

/**
  * The difference in load, expressed as a fraction of a core, between a
  * ramp-down threshold and the corresponding ramp-up threshold.
  */
double idleCoreFractionHysteresis = 0.2;

/**
  * The percentage of slots which are occupied that will prevent reducing the
  * number of cores.
  */
double SLOT_OCCUPANCY_THRESHOLD = 0.5;

/**
  * The period in ns over which we measure before deciding to reduce the number
  * of cores we use.
  */
const uint64_t MEASUREMENT_PERIOD = 50 * 1000 * 1000;


void CorePolicy::bootstrapLoadEstimator(bool disableLoadEstimation) {
    if (!disableLoadEstimation) {
        utilizationThresholds = new double[Arachne::maxNumCores];
        void coreLoadEstimator();
        Arachne::createThread(coreLoadEstimator);
    }
}

/**
  * Periodically wake up and observe the current load in Arachne to determine
  * whether it is necessary to increase or reduce the number of cores used by
  * Arachne.
  */
// void coreLoadEstimator() {
//     Arachne::PerfStats previousStats;
//     Arachne::PerfStats::collectStats(&previousStats);


//     for (Arachne::PerfStats currentStats; ; previousStats = currentStats) {
//         sleep(MEASUREMENT_PERIOD);
//         Arachne::PerfStats::collectStats(&currentStats);

//         // Take a snapshot of currently active cores before performing
//         // estimation to avoid races between estimation and the fulfillment of
//         // a previous core request.
//         uint32_t curActiveCores = Arachne::numActiveCores;

//         // Exclusive cores should contribute nothing to statistics relevant to
//         // core estimation during the period over which they are exclusive.
//         int numSharedCores = curActiveCores - Arachne::numExclusiveCores;

//         // Evalute idle time precentage multiplied by number of cores to
//         // determine whether we need to decrease the number of cores.
//         uint64_t idleCycles =
//             currentStats.idleCycles - previousStats.idleCycles;
//         uint64_t totalCycles =
//             currentStats.totalCycles - previousStats.totalCycles;
//         uint64_t utilizedCycles = totalCycles - idleCycles;
//         uint64_t totalMeasurementCycles =
//             Cycles::fromNanoseconds(MEASUREMENT_PERIOD);
//         double totalUtilizedCores =
//             static_cast<double>(utilizedCycles) /
//             static_cast<double>(totalMeasurementCycles);

//         // Estimate load to determine whether we need to increment the number
//         // of cores.
//         uint64_t weightedLoadedCycles =
//             currentStats.weightedLoadedCycles -
//             previousStats.weightedLoadedCycles;
//         double averageLoadFactor =
//             static_cast<double>(weightedLoadedCycles) /
//             static_cast<double>(totalCycles);
//         if (curActiveCores < Arachne::maxNumCores &&
//                 averageLoadFactor > loadFactorThreshold) {
//             // Record our current totalUtilizedCores, so we will only ramp down
//             // if utilization would drop below this level.
//             utilizationThresholds[numSharedCores] = totalUtilizedCores;
//             Arachne::incrementCoreCount();
//             continue;
//         }

//         // We should not ramp down if we have high occupancy of slots.
//         double averageNumSlotsUsed = static_cast<double>(
//                 currentStats.numThreadsCreated -
//                 currentStats.numThreadsFinished) /
//                 numSharedCores / Arachne::maxThreadsPerCore;

//         // Scale down if the idle time after scale down is greater than the
//         // time at which we scaled up, plus a hysteresis threshold.
//         if (totalUtilizedCores < utilizationThresholds[numSharedCores - 1]
//                 - idleCoreFractionHysteresis &&
//                 averageNumSlotsUsed < SLOT_OCCUPANCY_THRESHOLD) {
//             Arachne::decrementCoreCount();
//         }
//     }
// }