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

#ifndef ARACHNE_SEMAPHORE_H
#define ARACHNE_SEMAPHORE_H

#include <mutex>
#include <condition_variable>

/**
  * This class enables a kernel thread to block in the kernel until a resource
  * is available. It is a modified version of
  * https://stackoverflow.com/a/4793662/391161.
  */
class Semaphore
{
  private:
    // Protect the internal resource count.
    std::mutex mutex;

    // Blocked threads park on this condition variable until this resource is
    // available.
    std::condition_variable condition;

    // Quantity of resources available for consumption.
    uint64_t count = 0;

  public:
    // Restore the resource count to its original state.
    void reset() {
        std::unique_lock<decltype(mutex)> lock(mutex);
        count = 0;
    }

    // Increase the resource count.
    void notify() {
        std::unique_lock<decltype(mutex)> lock(mutex);
        ++count;
        condition.notify_one();
    }

    // Block until this resource is available.
    void wait() {
        std::unique_lock<decltype(mutex)> lock(mutex);
        while (!count) // Handle spurious wake-ups.
            condition.wait(lock);
        --count;
    }

    // Attempt to acquire this resource once.
    // \return
    //    Whether or not the acquisition succeeded.  inline bool
    bool try_wait() {
        std::unique_lock<decltype(mutex)> lock(mutex);
        if (count) {
            --count;
            return true;
        }
        return false;
    }
};

#endif
