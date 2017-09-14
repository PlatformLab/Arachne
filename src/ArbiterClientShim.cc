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
#include <sched.h>
#include "ArbiterClientShim.h"

namespace Arachne {

/**
  * Block until setRequestedCores with a higher number causes a notification on
  * this semaphor.
  */
core_t ArbiterClientShim::blockUntilCoreAvailable() {
    inactiveCores.wait();
    return sched_getcpu();
}

/**
  * Returns true iff currentRequestedCores <= currentCores.
  */
bool ArbiterClientShim::mustReleaseCore() {
    // Double-checked locking
    if (currentRequestedCores >= currentCores)
        return false;

    std::lock_guard<SpinLock> guard(shimLock);
    if (currentRequestedCores < currentCores) {
        currentCores--;
        return true;
    }
    return false;
}

/**
  * Adjust the number of requested cores, and unblock threads if there are more
  * requested cores than there used to be.
  */
void ArbiterClientShim::setRequestedCores(std::vector<uint32_t> numCores) {
    uint32_t sum = 0;
    for (uint32_t i: numCores)
        sum+=i;
    currentRequestedCores = sum;

    std::lock_guard<SpinLock> guard(shimLock);
    if (currentRequestedCores > currentCores) {
        uint64_t diff = currentRequestedCores - currentCores;
        for (uint64_t i = 0; i < diff; i++)
            inactiveCores.notify();
        currentCores.store(currentRequestedCores);
    }
}

// Since there is no server, this function is a no-op.
void ArbiterClientShim::unregisterThread() { }

}
