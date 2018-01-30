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

#ifndef COREPOLICY_H_
#define COREPOLICY_H_

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include "Common.h"
#include "Logger.h"
#include "PerfStats.h"
#include "PerfUtils/Cycles.h"
#include "PerfUtils/Util.h"
#include "SpinLock.h"

/*
 * The maximum number of thread classes.  All thread classes must be less than
 * this number, else behavior is undefined.
 */
#define MAX_THREAD_CLASSES 64

/*
 * All Arachne threads have a ThreadClass, which the CorePolicy uses to
 * determine on which cores they can run.
 */
typedef uint32_t ThreadClass;

/*
 * An unordered list of cores, defined as {map[i] for i < numFilled}.
 */
struct CoreList {
    /* The number of cores in the list */
    uint32_t numFilled;
    /*
     * An array containing the list.  Must be dynamically allocated to a size
     * equal to the number of cores on the machine.
     */
    int* map;
};

/*
 * This class allows applications to control the allocation and use of cores
 * in Arachne. It decides which Arachne threads get to run on which
 * cores.  It also decides how many cores Arachne should have.  It does
 * load estimation to decide when to request cores or release held cores
 * and, if a core is released, chooses which core to lose.
 */
class CorePolicy {
  public:
    /*
     * Constructor and destructor for CorePolicy.
     */
    CorePolicy() : corePolicyMutex("corePolicyMutex", false) {
        threadClassCoreMap = reinterpret_cast<CoreList**>(
            malloc(MAX_THREAD_CLASSES * sizeof(CoreList*)));
        for (int i = 0; i < MAX_THREAD_CLASSES; i++) {
            threadClassCoreMap[i] =
                reinterpret_cast<CoreList*>(malloc(sizeof(CoreList)));
            threadClassCoreMap[i]->map = reinterpret_cast<int*>(
                calloc(std::thread::hardware_concurrency(), sizeof(int)));
            threadClassCoreMap[i]->numFilled = 0;
        }
    }
    virtual ~CorePolicy() {
        for (int i = 0; i < MAX_THREAD_CLASSES; i++) {
            free(threadClassCoreMap[i]->map);
            free(threadClassCoreMap[i]);
        }
        free(threadClassCoreMap);
    }
    virtual int chooseRemovableCore();
    virtual void addCore(int coreId);
    virtual void removeCore(int coreId);
    CoreList* getRunnableCores(ThreadClass threadClass);

    /*
     * The default thread class, which all core policies must support.
     * It is used as a default within Arachne and benchmarks when necessary.
     * Other core policies can add more thread classes as needed.
     */
    static const ThreadClass defaultClass = 0;

  protected:
    virtual void coreLoadEstimator();
    /*
     * A map from thread classes to cores on which threads of those classes
     * can run.  threadClassCoreMap[i] is a CoreList of the cores on which
     * threads of class i can run.
     *
     * Resizing of the threadClassCoreMap is not safe, so the
     * threadClassCoreMap is entirely allocated once at startup and
     * must be large enough to meet future needs.
     */
    CoreList** threadClassCoreMap;
    /*
     * A lock that protects the threadClassCoreMap.  Held whenever
     * threadClassCoreMap is updated.
     */
    Arachne::SpinLock corePolicyMutex;
};

#endif  // COREPOLICY_H_
