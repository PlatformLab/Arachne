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

#define MAX_THREAD_CLASSES 64

typedef uint32_t threadClass_t;

/** 
  * An individual entry in the threadClassCoreMap. Each entry corresponds to a
  * particular thread class.  For a particular entry corresponding to a 
  * particular thread class, the set of all cores map[i] for i < numFilled
  * is the set of cores on which threads of that class can run.
  */
struct CoreList {
  uint32_t numFilled;
  int* map;
};

/*
 * Arachne uses the CorePolicy class to make decisions about what threads
 * to schedule on what cores.  All threads have a particular thread class,
 * which is mapped to a set of cores on which threads of that class can run
 * by the threadClassCoreMap, accessed through getCoreList.  All other
 * class methods maintain the threadClassCoreMap as cores are added or removed.
 */
class CorePolicy {
  public:
    /** Constructor and destructor for CorePolicy. */
    CorePolicy() {
        threadClassCoreMap = (CoreList**) 
          malloc(MAX_THREAD_CLASSES * sizeof(CoreList*));
        for (int i = 0; i < MAX_THREAD_CLASSES; i++) {
            threadClassCoreMap[i] = (CoreList*) 
              malloc(sizeof(CoreList));
            threadClassCoreMap[i]->map = (int*) 
              calloc(std::thread::hardware_concurrency(), sizeof(int));
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
    virtual void bootstrapLoadEstimator();
    virtual int chooseRemovableCore();
    virtual void addCore(int coreId);
    virtual void removeCore(int coreId);
    CoreList* getCoreList(threadClass_t threadClass);

    /* 
     * The base thread class, shared across all core policies and used as a 
     * default within Arachne when necessary.  Other core policies can add
     * more thread classes as needed
     */
    threadClass_t defaultClass = 0;
    
  protected:
    /**
      * A map from thread classes to cores on which threads of those classes
      * can run.  If threadClassCoreMap[i]->map[j] = c for some
      * j < threadClassCoreMap[i]->numFilled then Arachne can create a thread of
      * class i on the core with coreId c.
      */
    CoreList** threadClassCoreMap;

};

#endif // COREPOLICY_H_