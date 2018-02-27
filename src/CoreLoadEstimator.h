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

#ifndef CORELOADESTIMATOR_H_
#define CORELOADESTIMATOR_H_

#include <stdint.h>
#include <string.h>
#include "PerfStats.h"

namespace Arachne {

/**
 * Objects of this class offer recommendations about whether core count should
 * increase, decrease, or stay the same based on the current load factor and
 * utilization of cores.
 */
class CoreLoadEstimator {
  public:
    explicit CoreLoadEstimator(int maxNumCores);
    ~CoreLoadEstimator();
    int estimate(int currentNumCores);
    void reset();

  private:
    /*
     * We will attempt to increase the number of cores if the idle core
     * fraction (computed as idlePercentage * numSharedCores) is less than this
     * number.
     */
    double maxIdleCoreFraction = 0.1;

    /*
     * We will attempt to increase the number of cores if the load factor
     * increases beyond this threshold.
     */
    double loadFactorThreshold = 1.0;

    /*
     * utilizationThresholds[i] is the active core fraction at the time the
     * number of cores was ramped up from i to i + 1.  Allocated in
     * bootstrapLoadEstimator.
     */
    double* utilizationThresholds = NULL;

    /*
     * The difference in load, expressed as a fraction of a core, between a
     * ramp-down threshold and the corresponding ramp-up threshold (i.e., we
     * wait to ramp down until the load gets a bit below the point at
     * which we ramped up).
     */
    double idleCoreFractionHysteresis = 0.2;

    /*
     * Do not ramp down if the percentage of occupied threadContext slots is
     * above this threshold.
     */
    double slotOccupancyThreshold = 0.5;

    /*
     * Store the maximum cores the application is willing to use so that we
     * never recommend increasing the number of cores beyond this number.
     */
    int maxNumCores;

    /**
     * Stats collected during the previous execution of estimate.
     */
    Arachne::PerfStats previousStats;
};

}  // namespace Arachne
#endif  // DEFAULTCOREMANAGER_H_
