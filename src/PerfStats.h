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

#ifndef ARACHNE_PERFSTATS_H
#define ARACHNE_PERFSTATS_H

#include <vector>

#include "SpinLock.h"

namespace Arachne {
/**
 * An object of this class records various performance-related information.
 * Each kernel thread has a private instance of this object, which
 * eliminates cache conflicts when updating statistics and makes the class
 * thread-safe.  In addition, an object of this class is returned by
 * collectStats, which aggregates the statistics from all of the individual
 * threads.
 *
 * If you add a new metric, be sure to update all of the relevant methods
 * in PerfStats.cc. For example, search for all of the places where
 * "collectionTime" appears and add appropriate lines for the new metric.
 */
struct PerfStats {
    /// Time (in cycles) when the statistics were gathered (only
    /// present in aggregate statistics, not in thread-local instances).
    uint64_t collectionTime;

    /// Conversion factor from collectionTime to seconds (only present
    /// in aggregate statistics, not in thread-local instances).
    double cyclesPerSecond;

    // Number of cycles spent in the dispatch() loop.
    uint64_t idleCycles;

    // Total number of cycles actively executing on a core, including both
    // useful work and idle time.
    uint64_t totalCycles;

    // Number of threads run in one pass through a dispatch cycle
    // multiplied by the number of cycles that dispatch cycle took.
    uint64_t weightedLoadedCycles;

    // Number of times this core created a thread.
    uint64_t numThreadsCreated;

    // Number of times a thread finished running on this core.
    uint64_t numThreadsFinished;

    // Number of times the number of cores increased.
    uint64_t numCoreIncrements;

    // Number of times the number of cores decreased.
    uint64_t numCoreDecrements;

    // Number of createThread calls which had to be retried at least once
    // because of a CAS failure resulting from a conflict on the occupied
    // bitmask.
    uint64_t numContendedCreations;

    /// Used to protect the registeredStats vector.
    static SpinLock mutex;

    /// Keeps track of all the PerfStat structures that have been passed
    /// to registerStats (e.g. the different thread-local structures for
    /// each thread). This allows us to find all of the structures to
    /// aggregate their statistics in collectStats.
    static std::vector<PerfStats*> registeredStats;

    /// The following thread-local variable is used to access the
    /// statistics for the current thread.
    static thread_local PerfStats threadStats;

    explicit PerfStats(bool shouldRegister = false);
    static void registerStats(PerfStats* stats);
    static void deregisterStats(PerfStats* stats);
    static void collectStats(PerfStats* total);
};
}  // namespace Arachne

#endif  // ARACHNE_PERFSTATS_H
