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

#include "PerfUtils/Cycles.h"
#include "PerfUtils/Util.h"
#include "Logger.h"
#include "PerfStats.h"
#include "Common.h"
#include "Arachne.h"

class CorePolicy {
  public:
    /** Constructor and destructor for CorePolicy. */
    CorePolicy() {}
    ~CorePolicy(){}
    void bootstrapLoadEstimator(bool disableLoadEstimation);

};

#endif // COREPOLICY_H_