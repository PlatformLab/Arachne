/* Copyright (c) 2015-2018 Stanford University
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
#include <thread>

#include "Logger.h"
#include "PerfStats.h"

namespace Arachne {

SpinLock PerfStats::mutex(false);
std::vector<PerfStats*> PerfStats::allCoreStats;
std::vector<std::unique_ptr<PerfStats> > PerfStats::allUniqueCoreStats;
thread_local std::unique_ptr<PerfStats> PerfStats::threadStats;

// Constructor initializes all elements to 0
PerfStats::PerfStats(int coreId) {
    memset(this, 0, sizeof(*this));
    this->coreId = coreId;
}

/**
 * This method must be called obtain the PerfStats structure belonging to a
 * core. Typically this method is invoked once each time a kernel thread
 * acquires a core. This method is thread-safe but only one thread will obtain a
 * non-NULL value if multiple threads invoke it with the same coreId. It is an
 * error to invoke this method with the same coreId from a second kernel thread
 * until the first one has called releaseStats with the same coreId.
 *
 * \return
 *      The PerfStats structure associated with the given core. It is a
 *      unique_ptr to make it easier to find bugs associated with reuse of
 *      PerfStats structures by different threads.
 * \param coreId
 *      Identifier for the core for which to obtain a PerfStats structure.
 */
std::unique_ptr<PerfStats>
PerfStats::getStats(int coreId) {
    std::lock_guard<SpinLock> lock(mutex);
    // Initialize at least the slots for each core if they were not previously
    // allocated.
    if (allCoreStats.empty()) {
        allCoreStats.resize(std::thread::hardware_concurrency(), NULL);
        allUniqueCoreStats.resize(std::thread::hardware_concurrency());
    }

    // Initialize the current core if it is not already initialized
    if (!allCoreStats[coreId]) {
        allCoreStats[coreId] = new PerfStats(coreId);
        allUniqueCoreStats[coreId] =
            std::unique_ptr<PerfStats>(allCoreStats[coreId]);
    }

    return std::move(allUniqueCoreStats[coreId]);
}

/**
 * This method can be called to release a PerfStats structure that was
 * previously granted by getStats().
 * It is a no-op if the coreId was not previously granted.
 *
 * \param perfStats
 *      PerfStats structure to release; previously obtained from getStats.
 */
void
PerfStats::releaseStats(std::unique_ptr<PerfStats> perfStats) {
    std::lock_guard<SpinLock> lock(mutex);

    // Take back ownership of the perfStats for this core.
    allUniqueCoreStats[perfStats->coreId] = std::move(perfStats);
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
PerfStats::collectStats(PerfStats* total, CorePolicy::CoreList coreList) {
    std::lock_guard<SpinLock> lock(mutex);
    memset(total, 0, sizeof(*total));
    total->collectionTime = Cycles::rdtsc();
    total->cyclesPerSecond = Cycles::perSecond();
    for (size_t i = 0; i < coreList.size(); i++) {
        if (static_cast<uint32_t>(coreList[i]) >= allCoreStats.size()) {
            ARACHNE_LOG(ERROR,
                        "PerfStats::collectStats called with coreId %d, while "
                        "allCoreStats.size() is %zu\n",
                        coreList[i], allCoreStats.size());
            abort();
        }
        PerfStats* stats = allCoreStats[coreList[i]];
        // Skip over stats which are not allocated.
        if (!stats) {
            continue;
        }
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
