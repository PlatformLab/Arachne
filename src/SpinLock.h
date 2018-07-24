/* Copyright (c) 2015-2018 Stanford University
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

#ifndef ARACHNE_SPINLOCK_H_
#define ARACHNE_SPINLOCK_H_

#include <atomic>

#include "Common.h"
#include "Logger.h"
#include "PerfUtils/Cycles.h"

namespace Arachne {

using PerfUtils::Cycles;

// Forward declarations to resolve various circular dependencies of separating
// this out.
struct ThreadContext;
void yield();
extern thread_local Core core;

/**
 * A resource that can be acquired by only one thread at a time. Threads which
 * fail to acquire the resource will continue to actively attempt to acquire it
 * rather than sleeping.
 */
class SpinLock {
  public:
    // Constructor
    // \param name
    //     The name of this SpinLock, used for logging when there is a
    //     potential deadlock. If given, the name must be a string literal.
    // \param shouldYield
    //     True means that threads attempting to acquire this SpinLock will
    //     allow other threads to run. For short critical sections that hold
    //     onto a core throughout, it will minimize latency to set this to
    //     false. Otherwise, it is more core efficient to set this to true.
    explicit SpinLock(const char* name, bool shouldYield = true)
        : locked(false), name(name), shouldYield(shouldYield) {}
    explicit SpinLock(bool shouldYield = true)
        : locked(false), name("unnamed"), shouldYield(shouldYield) {}
    ~SpinLock() {}

    /** Repeatedly try to acquire this resource until success. */
    inline void lock() {
        uint64_t startOfContention = 0;
        while (locked.exchange(true, std::memory_order_acquire) != false) {
            if (startOfContention == 0) {
                startOfContention = Cycles::rdtsc();
            } else {
                uint64_t now = Cycles::rdtsc();
                if (Cycles::toSeconds(now - startOfContention) > 1.0) {
                    ARACHNE_LOG(
                        WARNING,
                        "%s SpinLock locked for one second; deadlock?\n", name);
                    startOfContention = now;
                }
            }
            if (shouldYield)
                yield();
        }
        owner = core.loadedContext;
    }

    /**
     * Attempt to acquire this resource once.
     * \return
     *    Whether or not the acquisition succeeded.  inline bool
     */
    inline bool try_lock() {
        if (!locked.exchange(true, std::memory_order_acquire)) {
            owner = core.loadedContext;
            return true;
        }
        return false;
    }

    /** Release resource. */
    inline void unlock() { locked.store(false, std::memory_order_release); }

    /** Set the label used for deadlock warning. */
    inline void setName(const char* name) { this->name = name; }

  private:
    // Implements the lock: false means free, true means locked
    std::atomic<bool> locked;

    // Used to identify the owning context for this lock.
    ThreadContext* owner;

    // Descriptive name for this SpinLock. Used to identify the purpose of
    // the lock, what it protects, where it exists in the codebase, etc.
    //
    // Used to identify the lock when reporting a potential deadlock.
    const char* name;

    // Controls whether the acquiring thread should yield control of the core
    // each time it fails to acquire this SpinLock.
    //
    // Should only be set to false for internal Arachne use.
    bool shouldYield;
};
}  // namespace Arachne
#endif
