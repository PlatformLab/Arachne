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


#ifndef COREPOLICY_H_
#define COREPOLICY_H_

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <functional>
#include <vector>
#include <mutex>
#include <deque>
#include <atomic>
#include <queue>
#include <string>
#include <thread>

typedef uint32_t threadClass_t;

#include "PerfUtils/Cycles.h"
#include "PerfUtils/Util.h"
#include "Logger.h"
#include "PerfStats.h"
#include "Common.h"
#include "Arachne.h"

#define NUM_THREAD_CLASSES 1

class CorePolicy {
  public:
    /** Constructor and destructor for CorePolicy. */
    CorePolicy() {
        threadCoreMap = (int**) malloc(NUM_THREAD_CLASSES * sizeof(int*));
        for (int i = 0; i < NUM_THREAD_CLASSES; i++)
            threadCoreMap[i] = (int*) calloc(std::thread::hardware_concurrency(), sizeof(int));
    }
    ~CorePolicy(){
        for (int i = 0; i < NUM_THREAD_CLASSES; i++)
            free(threadCoreMap[i]);
        free(threadCoreMap);
    }
    void bootstrapLoadEstimator(bool disableLoadEstimation);
    void addCore(int coreId, int numActiveCores);
    void removeCore(int coreId, int numActiveCores);

    threadClass_t baseClass = 0;
    
  protected:
    /**
      * A map from thread classes to cores on which threads of those classes
      * can run.  If threadCoreMap[i][j] = c for some j < numActiveCores then
      * Arachne can create a thread of class i on the core with coreId c.
      */
    int** threadCoreMap;



};

#endif // COREPOLICY_H_