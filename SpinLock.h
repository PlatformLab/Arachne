#include <atomic>

namespace Arachne {

    const int CACHE_LINE_SIZE = 64;
/**
 * A simple SpinLock without any statistics.
 */
class SpinLock {
  public:
    SpinLock() : state(false) {}
    ~SpinLock(){}
    void lock() {
       while(state.exchange(true) != false);
    }
    
    // If the original value was false, then we successfully acquired the lock.
    // Otherwise we failed.
    bool try_lock() {
         return !state.exchange(true);
    }
    void unlock() {
        state.store(false);
    }

  private:
    // Implements the lock: false means free, true means locked
    std::atomic<bool> state;
    
    // Pad this data structure out to a cache line size to mitigate false sharing.
    char cachePad[CACHE_LINE_SIZE-sizeof(state)];
};
}
