#ifndef ARACHNE_COMMON_H
#define ARACHNE_COMMON_H

namespace Arachne {
struct ThreadContext;
struct MaskAndCount;
/**
 * This class holds all the state associated with a particular core in Arachne.
 */
struct Core {
    /**
      * Alert the kernel thread that cleanup is complete and it should block for
      * ramp-down.
      */
    bool threadShouldYield;

    /**
      * This pointer allows fast access to the current kernel thread's
      * localThreadContexts without computing an offset from the global
      * allThreadContexts vector on each access.
      */
    ThreadContext** localThreadContexts;

    /**
      * Holds the identifier for the thread in which it is stored: allows each
      * kernel thread to identify itself. This should eventually become a
      * coreId, when we support multiple kernel threads per core to handle
      * blocking system calls.
      */
    int kernelThreadId = -1;

    /**
      * This is the context that a given kernel thread is currently executing.
      */
    ThreadContext *loadedContext;

    /**
      * See documentation for MaskAndCount.
      */
    std::atomic<MaskAndCount> *localOccupiedAndCount;

    /**
      * This represents each core's local copy of the high-priority mask. Each
      * call to dispatch() will first examine this bitmask. It will clear the
      * first set bit and switch to that context. If there are no set bits, it
      * will copy the current value of publicPriorityMasks for the current core
      * to here, and then atomically clear those bits using an atomic OR.
      *
      * When ramping down cores, this value (if nonzero) should be cleared,
      * since all non-terminated threads on this core will be migrated away
      * from this thread.
      */
    uint64_t privatePriorityMask;

    /**
      * This variable holds the index into the current kernel thread's
      * localThreadContexts that it will check first the next time it looks for
      * a thread to run. It is used to implement round-robin scheduling of
      * Arachne threads.
      */
    uint8_t nextCandidateIndex = 0;

    /**
     * This is the highest-indexed context known to be occupied by the dispatch
     * loop of a given core.
     */
    uint8_t highestOccupiedContext;
};
}

#endif
