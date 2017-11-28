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

#include "PerfUtils/Cycles.h"
#include "PerfUtils/Util.h"
#include "Logger.h"
#include "PerfStats.h"
#include "Common.h"

#define NUM_THREAD_CLASSES 1

typedef uint32_t threadClass_t;

/** Struct used to define the threadCoreMap in CorePolicy. **/
struct threadCoreMapEntry {
  uint32_t numFilled;
  int* map;
};

class CorePolicy {
  public:
    /** Constructor and destructor for CorePolicy. */
    CorePolicy() {
        threadCoreMap = (threadCoreMapEntry**) malloc(NUM_THREAD_CLASSES * sizeof(threadCoreMapEntry*));
        for (int i = 0; i < NUM_THREAD_CLASSES; i++) {
            threadCoreMap[i] = (threadCoreMapEntry*) malloc(sizeof(threadCoreMapEntry));
            threadCoreMap[i]->map = (int*) calloc(std::thread::hardware_concurrency(), sizeof(int));
            threadCoreMap[i]->numFilled = 0;
          }
    }
    ~CorePolicy() {
        for (int i = 0; i < NUM_THREAD_CLASSES; i++) {
            free(threadCoreMap[i]->map);
            free(threadCoreMap[i]);
          }
        free(threadCoreMap);
    }
    void bootstrapLoadEstimator(bool disableLoadEstimation);
    int chooseRemovableCore();
    void addCore(int coreId);
    void removeCore(int coreId);
    threadCoreMapEntry* getThreadCoreMapEntry(threadClass_t threadClass);

    threadClass_t baseClass = 0;
    
  protected:
    /**
      * A map from thread classes to cores on which threads of those classes
      * can run.  If threadCoreMap[i]->map[j] = c for some
      * j < threadCoreMap[i]->numFilled then Arachne can create a thread of
      * class i on the core with coreId c.
      */
    threadCoreMapEntry** threadCoreMap;

};

#endif // COREPOLICY_H_