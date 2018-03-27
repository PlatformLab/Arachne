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
    DefaultCoreManager(int minNumCores, int maxNumCores,
                       bool estimateLoad = true);
    virtual void coreAvailable(int myCoreId);
    virtual void coreUnavailable(int coreId);
    virtual CoreManager::CoreList getCores(int threadClass);
    void disableLoadEstimation();
    CoreLoadEstimator* getEstimator();

    /**
     * Limit the thread classes that the default core manager supports.
     */
    enum ThreadClass { DEFAULT = 0, EXCLUSIVE = 1 };

  private:
    int getExclusiveCore();
    void adjustCores();
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
    CoreManager::CoreList sharedCores;

    /**
     * Cores that are currently hosting exclusive threads.
     */
    CoreManager::CoreList exclusiveCores;

    /**
     * The core adjustment thread will run as long as this flag is set.
     */
    std::atomic<bool> coreAdjustmentShouldRun;

    /**
     * The following flag indicates whether the core adjustment thread has
     * already been started.  It should only be read and written with the mutex
     * held.
     */
    bool coreAdjustmentThreadStarted;

    /*
     * The period in ns over which we measure before deciding to increase or
     * reduce the number of cores we use.
     */
    uint64_t measurementPeriod = 50 * 1000 * 1000;
};
}  // namespace Arachne
#endif  // DEFAULTCOREMANAGER_H_
