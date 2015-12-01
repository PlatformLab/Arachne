#include <atomic>

namespace Arachne {

/**
 * A simple SpinLock without any statistics.
 */
class SpinLock {
  public:
    SpinLock() : state(false) {}
    ~SpinLock(){}
    void lock() {
       while(state.exchange(true, std::memory_order_acq_rel) != false);
    }
    
    // If the original value was false, then we successfully acquired the lock.
    // Otherwise we failed.
    bool try_lock() {
         return !state.exchange(true, std::memory_order_acq_rel);
    }
    void unlock() {
        state.store(false, std::memory_order_acq_rel);
    }

  private:
    /// Implements the lock: false means free, true means locked
    std::atomic<bool> state;
};
}
