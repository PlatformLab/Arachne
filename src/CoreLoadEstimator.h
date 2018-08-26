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

#ifndef CORELOADESTIMATOR_H_
#define CORELOADESTIMATOR_H_

#include <stdint.h>
#include <string.h>
#include "CorePolicy.h"
#include "PerfStats.h"

namespace Arachne {

/**
 * Objects of this class offer recommendations about whether core count should
 * increase, decrease, or stay the same based on the current load factor and
 * utilization of cores.
 */
class CoreLoadEstimator {
  public:
    CoreLoadEstimator();
    ~CoreLoadEstimator();
    int estimate(CorePolicy::CoreList coreList);
    void clearHistory();
    void setLoadFactorThreshold(double loadFactorThreshold);
    void setMaxUtilization(double maxUtilization);

  private:
    /**
     * Strategy used by the coreLoadEstimator to estimate load.
     * Multiple choices exist to facilitate experimentation and comparison
     * between them across different workloads. They are set when the
     * corresponding threshold-setting method is invoked.
     */
    enum EstimationStrategy {
        /**
         * Decide the number of cores based on a combination of load factor and
         * utilization. Load factor is defined as the ratio of time spent going
         * through all contexts on a core weighted by the number of threads
         * executed to the total time the core was active.
         */
        LOAD_FACTOR = 1,
        /**
         * Decide the number of cores based purely on the utilization.
         */
        UTILIZATION = 2
    } estimationStrategy = LOAD_FACTOR;

    typedef std::lock_guard<SpinLock> Lock;
    /**
     * Protect the parameters below from concurrent modifications and use.
     */
    SpinLock lock;

    /*
     * We will attempt to increase the number of cores if the load factor
     * increases beyond this threshold.
     */
    double loadFactorThreshold = 1.5;

    /*
     * We will attempt to increase the number of cores if the utilization
     * increases above this level.
     */
    double maxUtilization = 0.8;

    /*
     * utilizationThresholds[i] is the active core fraction at the time the
     * number of cores was ramped up from i to i + 1.
     */
    std::vector<double> utilizationThresholds;

    /*
     * The difference in load, expressed as a utilization delta, between a
     * ramp-down threshold and the corresponding ramp-up threshold (i.e., we
     * wait to ramp down until the load gets a bit below the point at
     * which we ramped up).
     */
    double idleCoreFractionHysteresis = 0.09;

    /*
     * Core utilizations below this threshold are considered effectively 0.
     */
    double zeroCoreUtilizationThreshold = 0.01;

    /*
     * Do not ramp down if the fraction of occupied threadContext slots is
     * above this threshold. This exists because it does not make sense to
     * decrease the number of cores if there are large numbers of blocked
     * threads parked on the cores; such threads would have nowhere to migrate.
     */
    double slotOccupancyThreshold = 0.5;

    /**
     * Stats collected during the previous execution of estimate.
     */
    Arachne::PerfStats previousStats;
};

}  // namespace Arachne
#endif  // CORELOADESTIMATOR_H
