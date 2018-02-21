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

#ifndef DEFAULTCOREMANAGER_H_
#define DEFAULTCOREMANAGER_H_

#include <mutex>
#include "CoreLoadEstimator.h"
#include "CoreManager.h"
#include "SpinLock.h"

namespace Arachne {

/**
 * Arachne's default CoreManager makes all cores available for general
 * scheduling unless the application invokes createExclusiveThread; each call
 * to createExclusiveThread will remove one core from the general scheduling
 * pool.
 */
class DefaultCoreManager : public CoreManager {
  public:
    DefaultCoreManager(int minNumCores, int maxNumCores);
    /**
     * Add the given core to the pool of threads for general scheduling.
     */
    virtual void coreAvailable(int myCoreId);

    /**
     * Prevent further creations on and remove all threads from the most
     * recently acquired core, and schedule releaseCore() on it. Log an error
     * and abort the process if loss of a core results in no cores available
     * for general scheduling.
     */
    virtual void coreUnavailable();

    /**
     * Invoked by Arachne to get cores available for scheduling a particular
     * threadClass.
     */
    virtual CoreList* getCores(int threadClass);

    /**
     * After this function returns, no load estimations that have already begun
     * will complete, but no future load estimations will occur.
     */
    void disableLoadEstimation();

    /**
     * Limit the thread classes that the default core manager supports.
     */
    enum ThreadClass { DEFAULT = 0, EXCLUSIVE = 1 };

  private:
    /**
     * Find or allocate a core for exclusive use by a thread.
     */
    int getExclusiveCore();
    /**
     * The minimum number of cores that the application needs to run
     * effectively.
     */
    const int minNumCores;
    /**
     * The maximum number of cores that Arachne will use.
     */
    const int maxNumCores;
    /**
     * Used to determine whether the system needs more cores or fewer cores.
     */
    CoreLoadEstimator loadEstimator;

    typedef std::lock_guard<SpinLock> Lock;

    /**
     * Protect the data structures below for writes.
     */
    SpinLock lock;

    /**
     * Cores that are available for general scheduling.
     */
    CoreList sharedCores;

    /**
     * Cores that are currently hosting exclusive threads.
     */
    CoreList exclusiveCores;
};
}  // namespace Arachne
#endif  // DEFAULTCOREMANAGER_H_
