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

#ifndef ARACHNE_H_
#define ARACHNE_H_

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <atomic>
#include <deque>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

#include "Common.h"
#include "CoreArbiter/Semaphore.h"
#include "CorePolicy.h"
#include "Logger.h"
#include "PerfStats.h"
#include "PerfUtils/Cycles.h"
#include "PerfUtils/Util.h"

/**
 * Arachne is a user-level, cooperative thread management system written in
 * C++, designed to improve core utlization and maximize throughput in server
 * applications without impacting latency. It performs M:N scheduling over
 * kernel threads running exclusively on CPU cores.
 */
namespace Arachne {

// A macro to disallow the copy constructor and operator= functions
#ifndef DISALLOW_COPY_AND_ASSIGN
#define DISALLOW_COPY_AND_ASSIGN(TypeName) \
    TypeName(const TypeName&) = delete;    \
    TypeName& operator=(const TypeName&) = delete;
#endif

using PerfUtils::Cycles;

// Forward declarations
struct ThreadContext;
struct Core;
extern thread_local Core core;

// This is used in createThread.
extern std::atomic<uint32_t> numActiveCores;

extern volatile uint32_t minNumCores;
extern volatile uint32_t maxNumCores;

extern int stackSize;

// Used in inline functions.
extern FILE* errorStream;
void dispatch();

extern std::function<void()> initCore;

extern std::vector<ThreadContext**> allThreadContexts;

extern CorePolicy* corePolicy;

extern std::vector<::Semaphore*> coreIdleSemaphores;
/*
 * True means that the Core Load Estimator will not run; used only in unit
 * tests.
 */
extern bool disableLoadEstimation;

/**
 * \addtogroup api Arachne Public API
 * Most of the functions in this API, with the exception of Arachne::init(),
 * Arachne::shutDown(), Arachne::waitForTermination(), and
 * Arachne::createThread(), should only be called from within Arachne threads.
 * @{
 */
/**
 * This structure is used to identify an Arachne thread to methods of the
 * Arachne API.
 */
struct ThreadId {
    /// The storage where this thread's state is held.
    ThreadContext* context;
    /// Differentiates this Arachne thread from previous threads (now defunct)
    /// that used the same context.
    uint32_t generation;

    /// Construct a ThreadId.
    /// \param context
    ///    The location where the thread's metadata currently lives.
    /// \param generation
    ///    Used to differentiate this thread from others that lived at this
    ///    context in the past and future.
    ThreadId(ThreadContext* context, uint32_t generation)
        : context(context), generation(generation) {}

    ThreadId() : context(NULL), generation(0) {}

    /// The equality operator is generally used for comparing against
    /// Arachne::NullThread.
    bool operator==(const ThreadId& other) const {
        return context == other.context && generation == other.generation;
    }

    /// Negation of the function above.
    bool operator!=(const ThreadId& other) const { return !(*this == other); }

    bool operator!() const { return *this == ThreadId(); }
};

void init(int* argcp = NULL, const char** argv = NULL);
void shutDown();
void waitForTermination();
void yield();
void sleep(uint64_t ns);
void sleepForCycles(uint64_t cycles);

void idleCore(int coreId);
void unidleCore(int coreId);

bool removeAllThreadsFromCore(int coreId,
                              CorePolicy::CorePolicy::CoreList outputCores);
void makeSharedOnCore();

void setCorePolicy(CorePolicy* arachneCorePolicy);
CorePolicy* getCorePolicy();

void block();
void signal(ThreadId id);
void join(ThreadId id);
ThreadId getThreadId();

void setErrorStream(FILE* ptr);
void mainThreadInit();
void mainThreadDestroy();

/**
 * A resource which blocks the current thread until it is available.
 * This resources should not be acquired from non-Arachne threads.
 */
class SleepLock {
  public:
    /** Constructor and destructor for sleepLock. */
    SleepLock()
        : blockedThreads(),
          blockedThreadsLock("blockedthreadslock", false),
          owner(NULL) {}
    ~SleepLock() {}
    void lock();
    bool try_lock();
    void unlock();

  private:
    // Ordered collection of threads that are waiting on this lock. Threads
    // are processed from this list in FIFO order when a notifyOne() is called.
    std::deque<ThreadId> blockedThreads;

    // A SpinLock to protect the blockedThreads data structure.
    SpinLock blockedThreadsLock;

    // Used to identify the owning context for this lock. The lock is held iff
    // owner != NULL.
    ThreadContext* owner;
};

/**
 * This class enables one or more threads to block until a condition is true,
 * and then be awoken when the condition might be true.
 */
class ConditionVariable {
  public:
    ConditionVariable();
    ~ConditionVariable();
    void notifyOne();
    void notifyAll();
    template <typename LockType>
    void wait(LockType& lock);
    template <typename LockType>
    void waitFor(LockType& lock, uint64_t ns);

  private:
    // Ordered collection of threads that are waiting on this condition
    // variable. Threads are processed from this list in FIFO order when a
    // notifyOne() is called.
    std::deque<ThreadId> blockedThreads;
    DISALLOW_COPY_AND_ASSIGN(ConditionVariable);
};

/**
 * This class enables a thread to block until a resource is available.
 * It is safe to use in Arachne runtime code.
 */
class Semaphore {
  public:
    Semaphore();
    void reset();
    void notify();
    void wait();
    bool try_wait();

  private:
    SpinLock countProtector;
    ConditionVariable countWaiter;
    uint64_t count;  // Initialized as locked.
};

/**
 * This value represents the non-existence of a thread and can be returned by
 * any Arachne function that would normally return a ThreadId.
 *
 * One example is createThread when there are not enough resources to create a
 * new thread.
 */
const Arachne::ThreadId NullThread;
/**@}*/

////////////////////////////////////////////////////////////////////////////////
// The declarations in following section are private to the thread library.
////////////////////////////////////////////////////////////////////////////////

/**
 * We need to invoke a ThreadInvocation with unknown template types, which has
 * been stored in a character array, and this class enables us to do this.
 */
struct ThreadInvocationEnabler {
    /// This function allows us to invoke the templated subtype, without
    /// casting to a pointer of unknown type.
    virtual void runThread() = 0;
    virtual ~ThreadInvocationEnabler() {}
};

/**
 * This structure is used during thread creation to pass the function and
 * arguments for the new thread's top-level function from a creating thread to
 * the core that runs the new thread. It also ensures that the arguments will
 * fit in a single cache line, since they will be stored in a single cache
 * line.
 *
 * \tparam F
 *     The type of the return value of std::bind, which is a value type of
 *     unspecified class.
 *
 * This wrapper enables us to bypass the dynamic memory allocation that is
 * sometimes performed by std::function.
 */
template <typename F>
struct ThreadInvocation : public ThreadInvocationEnabler {
    /// The top-level function of the Arachne thread.
    F mainFunction;

    /// Construct a threadInvocation from the type that is returned by
    /// std::bind.
    explicit ThreadInvocation(F&& mainFunction)
        : mainFunction(std::move(mainFunction)) {
        static_assert(
            sizeof(ThreadInvocation<F>) <= CACHE_LINE_SIZE - 8,
            "Arachne requires the function and arguments for a thread to "
            "fit within one cache line.");
    }

    /// This is invoked exactly once for each Arachne thread to begin its
    /// execution.
    void runThread() { mainFunction(); }
};

/**
 * This class holds all the state for managing an Arachne thread.
 */
struct ThreadContext {
    /// Keep a reference to the original memory allocation for the stack used by
    /// this threadContext so that we can release the memory in shutDown.
    void* stack;

    /// This holds the value that rsp, the stack pointer register, will be set
    /// to when this thread is swapped in.
    void* sp;

    /// Used as part of ThreadIds to differentiate Arachne threads that use this
    /// ThreadContext; incremented whenever an Arachne thread finishes execution
    /// in this ThreadContext.
    uint32_t generation;

    /// This lock is used for synchronizing threads that attempt to join this
    /// thread.
    SpinLock joinLock;

    /// Threads attempting to join the thread that currently occupies this
    /// context shall wait on this CV.
    ConditionVariable joinCV;

    // Value of coreId before this ThreadContext is assigned to a core.
    static const uint8_t CORE_UNASSIGNED;

    /// Unique identifier for the core that this thread currently lives on.
    /// This will only change if a ThreadContext is migrated.
    /// Intended primarily for debugging.
    uint8_t coreId;

    /// Unique identifier for the core that this threadcontext was most
    /// recently initialized on. Initialization occurs as soon as a kernel
    /// thread returns from blockUntilCoreAvailable(). This helps us determine
    /// whether the thread was migrated, and where it was migrated from.
    uint8_t originalCoreId;

    /// Specified by applications to indicate general properties of this thread
    /// (e.g. latency-sensitive foreground thread vs throughput-sensitive
    /// background thread); used by CorePolicy.
    // It defaults to 0 for threads created without specifying a class.
    int threadClass = 0;

    /// Unique identifier for this thread among those on the same core.
    /// Used to index into various core-specific arrays.
    /// This will only change if a ThreadContext is migrated.
    uint8_t idInCore;

    /// \var threadInvocation
    /// Storage for the ThreadInvocation object that contains the function and
    /// arguments for a new thread.
    /// We wrap the char buffer in a struct to enable aligning to a cache line
    /// boundary, which eliminates false sharing of cache lines.

    /// \cond SuppressDoxygen
    struct alignas(CACHE_LINE_SIZE) {
        char data[CACHE_LINE_SIZE - 8];
        /// This variable holds the minimum value of the cycle counter for which
        /// this thread can run.
        /// 0 is a signal that this thread should run at the next opportunity.
        /// ~0 is used as an infinitely large time: a sleeping thread will not
        /// awaken as long as wakeupTimeInCycles has this value.
        /// This variable lives here to share a cache line with the thread
        /// invocation, thereby removing a cache miss for thread creation.
        volatile uint64_t wakeupTimeInCycles;
    }
    /// \endcond
    threadInvocation;

    /**
     * This is the value for wakeupTimeInCycles when a live thread is blocked.
     */
    static const uint64_t BLOCKED;

    /**
     * This is the value for wakeupTimeInCycles when a ThreadContext is not
     * hosting a thread.
     */
    static const uint64_t UNOCCUPIED;

    /// This reference is for convenience and always points at
    /// threadInvocation->wakeupTimeInCycles.
    volatile uint64_t& wakeupTimeInCycles;

    void initializeStack();
    ThreadContext() = delete;
    ThreadContext(ThreadContext&) = delete;

    explicit ThreadContext(uint8_t idInCore);
};

/**
 * This is the number of bytes needed on the stack to store the callee-saved
 * registers that are defined by the current processor and operating system's
 * calling convention.
 */
const size_t SPACE_FOR_SAVED_REGISTERS = 48;

/**
 * This value is placed at the lowest allocated address of the stack to detect
 * stack overflows.
 */
const uint64_t STACK_CANARY = 0xDEADBAAD;

/**
 * Amount of time in nanoseconds to wait for extant threads to finish before
 * commencing migration.
 */
const uint64_t COMPLETION_WAIT_TIME = 100000;

void schedulerMainLoop();
void swapcontext(void** saved, void** target);
void threadMain();

/// This structure tracks the live threads on a single core.
struct MaskAndCount {
    /// Each bit corresponds to a particular ThreadContext which has the
    /// idInCore corresponding to its index.
    /// 0 means this context is available for a new thread.
    /// 1 means this context is in use by a live thread.
    uint64_t occupied : 56;
    /// The number of 1 bits in occupied.
    uint64_t numOccupied : 8;
    /**
     * Initial value of numOccupied for cores that are exclusive to a thread.
     * This value is sufficiently high that when other threads exit and
     * decrement numOccupied, creation will continue to be blocked on the target
     * core.
     */
    static const uint8_t EXCLUSIVE;
};

extern std::vector<std::atomic<MaskAndCount>*> occupiedAndCount;

extern std::vector<std::atomic<uint64_t>*> allHighPriorityThreads;

#ifdef ARACHNE_TEST
extern std::deque<uint64_t> mockRandomValues;
#endif

/**
 * A random number generator from the Internet that returns 64-bit integers.
 * It is used for selecting candidate cores to create threads on.
 */
inline uint64_t
random(void) {
#ifdef ARACHNE_TEST
    if (!mockRandomValues.empty()) {
        uint64_t returnValue = mockRandomValues.front();
        mockRandomValues.pop_front();
        return returnValue;
    }
#endif

    // This function came from the following site.
    // http://stackoverflow.com/a/1640399/391161
    //
    // It was chosen because it was advertised to be fast, but this fact has
    // not yet been verified or disproved through experiments.
    static thread_local uint64_t x = 123456789, y = 362436069, z = 521288629;
    uint64_t t;
    x ^= x << 16;
    x ^= x >> 5;
    x ^= x << 1;

    t = x;
    x = y;
    y = z;
    z = t ^ x ^ y;

    return z;
}

// Select a reasonably unloaded core from coreList using randomness with
// refinement. This function is defined here to facilitate testing; defining it
// in the CC file causes the compiler to generate an independent version of the
// random() function above.
static int __attribute__((unused))
chooseCore(const CorePolicy::CoreList& coreList) {
    uint32_t index1 = static_cast<uint32_t>(random()) % coreList.size();
    uint32_t index2 = static_cast<uint32_t>(random()) % coreList.size();
    while (index2 == index1 && coreList.size() > 1)
        index2 = static_cast<uint32_t>(random()) % coreList.size();

    int choice1 = coreList.get(index1);
    int choice2 = coreList.get(index2);

    if (occupiedAndCount[choice1]->load().numOccupied <
        occupiedAndCount[choice2]->load().numOccupied)
        return choice1;
    return choice2;
}

/**
 * Spawn a thread with main function f invoked with the given args on the
 * kernel thread with id = coreId
 * This function should usually only be invoked directly in tests, since it
 * does not perform load balancing.
 *
 * \param coreId
 *     The id for the kernel thread to put the new Arachne thread on.
 * \param __f
 *     The main function for the new thread.
 * \param __args
 *     The arguments for __f.
 * \return
 *     The return value is an identifier for the newly created thread. If
 *     there are insufficient resources for creating a new thread, then
 *     NullThread will be returned.
 */
template <typename _Callable, typename... _Args>
ThreadId
createThreadOnCore(uint32_t coreId, _Callable&& __f, _Args&&... __args) {
    auto task =
        std::bind(std::forward<_Callable>(__f), std::forward<_Args>(__args)...);

    ThreadContext* threadContext;
    bool success;
    uint32_t index;
    int failureCount = 0;
    do {
        // Each iteration through this loop makes one attempt to enqueue the
        // task to the specified core. Multiple iterations are required only if
        // there is contention for the core's state variables.
        MaskAndCount slotMap = *occupiedAndCount[coreId];
        MaskAndCount oldSlotMap = slotMap;

        if (slotMap.numOccupied >= maxThreadsPerCore) {
            ARACHNE_LOG(VERBOSE,
                        "createThread failure, coreId = %u, "
                        "numOccupied = %ld\n",
                        coreId, slotMap.numOccupied);
            return NullThread;
        }

        // Search for a non-occupied slot and attempt to reserve the slot
        index = ffsll(~slotMap.occupied);
        if (!index) {
            ARACHNE_LOG(WARNING,
                        "createThread failed after passing numOccupied"
                        " check, coreId = %u,"
                        " numOccupied = %ld\n",
                        coreId, slotMap.numOccupied);
            return NullThread;
        }

        // ffsll returns a 1-based index.
        index--;

        slotMap.occupied =
            (slotMap.occupied | (1L << index)) & 0x00FFFFFFFFFFFFFF;
        slotMap.numOccupied++;
        threadContext = allThreadContexts[coreId][index];
        success = occupiedAndCount[coreId]->compare_exchange_strong(oldSlotMap,
                                                                    slotMap);
        if (!success) {
            failureCount++;
        }
    } while (!success);

    // Copy the thread invocation into the byte array.
    new (&threadContext->threadInvocation.data)
        Arachne::ThreadInvocation<decltype(task)>(std::move(task));

    // Read the generation number *before* waking up the thread, to avoid a
    // race where the thread finishes executing so fast that we read the next
    // generation number instead of the current one.
    // It's not clear why reading from allThreadContexts[coreId][index] is
    // faster than reading it from threadContext, but it seems to save ~10 ns
    // in the microbenchmark. One speculation is that we can get better ILP by
    // not using the same variable for both.
    uint32_t generation = allThreadContexts[coreId][index]->generation;
    threadContext->wakeupTimeInCycles = 0;

    PerfStats::threadStats->numThreadsCreated++;
    if (failureCount)
        PerfStats::threadStats->numContendedCreations++;

    return ThreadId(threadContext, generation);
}

int chooseCore(const CorePolicy::CoreList& coreList);

////////////////////////////////////////////////////////////////////////////////
// The ends the private section of the thread library.
////////////////////////////////////////////////////////////////////////////////

/**
 * Spawn a new thread with the given threadClass, function and arguments.
 *
 * \param threadClass
 *     The class of the thread being created; its meaning is determined by the
 *     currently running CorePolicy.
 * \param __f
 *     The main function for the new thread.
 * \param __args
 *     The arguments for __f. The total size of the arguments cannot exceed 48
 *     bytes, and arguments are taken by value, so any reference must be
 *     wrapped with std::ref.
 * \return
 *     The return value is an identifier for the newly created thread. If
 *     there are insufficient resources for creating a new thread, then
 *     NullThread will be returned.
 *
 * \ingroup api
 */
template <typename _Callable, typename... _Args>
ThreadId
createThreadWithClass(int threadClass, _Callable&& __f, _Args&&... __args) {
    // Find a core to enqueue to by picking two at random and choosing
    // the one with the fewest Arachne threads.
    CorePolicy::CoreList coreList = corePolicy->getCores(threadClass);
    if (coreList.size() == 0)
        return Arachne::NullThread;
    uint32_t kId = static_cast<uint32_t>(chooseCore(coreList));
    auto threadId = createThreadOnCore(kId, __f, __args...);
    if (threadId != NullThread) {
        threadId.context->threadClass = threadClass;
    }
    return threadId;
}

/**
 * Spawn a new thread with a function and arguments.
 *
 * \param __f
 *     The main function for the new thread.
 * \param __args
 *     The arguments for __f. The total size of the arguments cannot exceed 48
 *     bytes, and arguments are taken by value, so any reference must be
 *     wrapped with std::ref.
 * \return
 *     The return value is an identifier for the newly created thread. If
 *     there are insufficient resources for creating a new thread, then
 *     NullThread will be returned.
 *
 * \ingroup api
 */
template <typename _Callable, typename... _Args>
ThreadId
createThread(_Callable&& __f, _Args&&... __args) {
    return createThreadWithClass(0, __f, __args...);
}

/**
 * Block the current thread until the condition variable is notified.
 *
 * \param lock
 *     The mutex associated with this condition variable; must be held by
 *     caller before calling wait. This function releases the mutex before
 *     blocking, and re-acquires it before returning to the user.
 */
template <typename LockType>
void
ConditionVariable::wait(LockType& lock) {
#if TIME_TRACE
    TimeTrace::record("Wait on Core %d", core.id);
#endif
    blockedThreads.push_back(
        ThreadId(core.loadedContext, core.loadedContext->generation));
    lock.unlock();
    dispatch();
#if TIME_TRACE
    TimeTrace::record("About to acquire lock after waking up");
#endif
    lock.lock();
}

/**
 * Block the current thread until the condition variable is notified or at
 * least ns nanoseconds has passed.
 *
 * \param lock
 *     The mutex associated with this condition variable; must be held by
 *     caller before calling wait. This function releases the mutex before
 *     blocking, and re-acquires it before returning to the user.
 * \param ns
 *     The time in nanoseconds this thread should wait before returning in the
 *     absence of a signal.
 */
template <typename LockType>
void
ConditionVariable::waitFor(LockType& lock, uint64_t ns) {
    core.loadedContext->wakeupTimeInCycles =
        Cycles::rdtsc() + Cycles::fromNanoseconds(ns);
    blockedThreads.push_back(
        ThreadId(core.loadedContext, core.loadedContext->generation));
    lock.unlock();
    dispatch();
    lock.lock();
}

/**
 * This class updates idleCycles and totalCycles in PerfStats to keep track of
 * idle and total time.
 */
struct IdleTimeTracker {
    /**
     * Cycle counter of the last time updatePerfStats() was called.
     */
    static thread_local uint64_t lastTotalCollectionTime;

    /**
     * Cycle counter at the top of the last call to Arachne::dispatch() on this
     * core, regardless of which user thread made the call.
     */
    static thread_local uint64_t dispatchStartCycles;

    /**
     * Cycle counter for the beginning of the last iteration through the
     * dispatch loop.
     */
    static thread_local uint64_t lastDispatchIterationStart;

    /**
     * The number of threads ran in the last loop through all contexts.
     */
    static thread_local uint8_t numThreadsRan;

    IdleTimeTracker();
    void updatePerfStats();
    ~IdleTimeTracker();
};

/**
 * This class maintains a per-core bit which indicates whether the
 * core is currently inside the main dispatch loop, and aborts the application
 * with a backtrace if there is a second call to dispatch() from within
 * dispatch().
 */
class NestedDispatchDetector {
  public:
    NestedDispatchDetector();
    ~NestedDispatchDetector();
    static void clearDispatchFlag();

  private:
    /**
     * This per-core flag is set when entering the dispatch loop and cleared
     * when exiting it.
     */
    static thread_local bool dispatchRunning;
};

}  // namespace Arachne

// Force instantiation for debugging with operator[]
template class std::vector<Arachne::ThreadContext**>;
template class std::vector<std::atomic<Arachne::MaskAndCount>*>;
template class std::vector<std::atomic<uint64_t>*>;
template class std::vector<Arachne::PerfStats*>;

#endif  // ARACHNE_H_
