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

#include <string.h>
#include <algorithm>

#include "PerfStats.h"

namespace Arachne {

SpinLock PerfStats::mutex(false);
std::vector<PerfStats*> PerfStats::registeredStats;
thread_local PerfStats PerfStats::threadStats(true);

// This constructor will automatically register a PerfStats structure
// if `true` is passed.
PerfStats::PerfStats(bool shouldRegister) {
    if (shouldRegister) registerStats(this);
}

/**
 * This method must be called to make a PerfStats structure "known" so that
 * its contents will be considered by collectStats. Typically this method
 * is invoked once for the thread-local structure associated with each
 * thread. This method is idempotent and thread-safe, so it is safe to
 * invoke it multiple times for the same PerfStats.
 *
 * \param stats
 *      PerfStats structure to remember for usage by collectStats. If this
 *      is the first time this structure has been registered, all of its
 *      counters will be initialized.
 */
void
PerfStats::registerStats(PerfStats* stats) {
    std::lock_guard<SpinLock> lock(mutex);

    // First see if this structure is already registered; if so,
    // there is nothing for us to do.
    for (PerfStats* registered : registeredStats) {
        if (registered == stats) {
            return;
        }
    }

    // This is a new structure; add it to our list, and reset its contents.
    memset(stats, 0, sizeof(*stats));
    registeredStats.push_back(stats);
}

/**
 * This method can be called to deregister a PerfStats structure that has been
 * registered using registerStats. It is a no-op if the stat is already
 * deregistered.
 *
 * \param stats
 *      PerfStats structure to drop from usage by collectStats.
 */
void
PerfStats::deregisterStats(PerfStats* stats) {
    std::lock_guard<SpinLock> lock(mutex);
    registeredStats.erase(
        std::remove(registeredStats.begin(), registeredStats.end(), stats),
        registeredStats.end());
}

/**
 * This method aggregates performance information from all of the
 * PerfStats structures that have been registered via the registerStats
 * method.
 *
 * Note: this function doesn't calculate or fill memory statistics.
 *       See definition of memory stat fields (eg. logMaxBytes) for details.
 *
 * \param[out] total
 *      Filled in with the sum of all statistics from all registered
 *      PerfStat structures; any existing contents are overwritten.
 */
void
PerfStats::collectStats(PerfStats* total) {
    std::lock_guard<SpinLock> lock(mutex);
    memset(total, 0, sizeof(*total));
    total->collectionTime = Cycles::rdtsc();
    total->cyclesPerSecond = Cycles::perSecond();
    for (PerfStats* stats : registeredStats) {
        // Note: the order of the statements below should match the
        // declaration order in PerfStats.h.
        total->idleCycles += stats->idleCycles;
        total->totalCycles += stats->totalCycles;
        total->weightedLoadedCycles += stats->weightedLoadedCycles;
        total->numThreadsCreated += stats->numThreadsCreated;
        total->numThreadsFinished += stats->numThreadsFinished;
        total->numCoreIncrements += stats->numCoreIncrements;
        total->numCoreDecrements += stats->numCoreDecrements;
        total->numContendedCreations += stats->numContendedCreations;
    }
}
}  // namespace Arachne
