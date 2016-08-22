#ifndef ARACHNE_SPINLOCK_H
#define ARACHNE_SPINLOCK_H
#include <atomic>
#include "Common.h"

namespace Arachne {

/**
 * A simple SpinLock without any statistics.
 */
class SpinLock {
  public:
    SpinLock() : state(false) {}
    ~SpinLock(){}
    void lock() {
       while(state.exchange(true, std::memory_order_acquire) != false);
    }
    
    // If the original value was false, then we successfully acquired the lock.
    // Otherwise we failed.
    bool try_lock() {
         return !state.exchange(true, std::memory_order_acquire);
    }
    void unlock() {
        state.store(false, std::memory_order_release);
    }

  private:
    // Implements the lock: false means free, true means locked
    std::atomic<bool> state;
    
    // Pad this data structure out to a cache line size to mitigate false sharing.
    char cachePad[CACHE_LINE_SIZE-sizeof(state)];
} __attribute__ ((aligned(CACHE_LINE_SIZE)));
} 
#endif
