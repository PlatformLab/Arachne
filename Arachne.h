#ifndef ARACHNE_H
#define ARACHNE_H

#include <functional>
#include <vector>
#include <mutex>
#include <deque>
#include <atomic>
#include <assert.h>


#include "Common.h"

namespace  Arachne {

extern volatile unsigned numCores;

// This class allows us to invoke a function produced by std::bind after it has
// been copied to a fixed-size object, by using indirection through virtual
// dispatch.
struct TaskBase {
   virtual void runThread() = 0;
};

// This structure is used for cross-core thread creation.
// It is effectively a templated wrapper around the return value of std::bind,
// which is a value type of unspecified class.

// This wrapper enables us to bypass the dynamic memory allocation tendencies
// of std::function.
template <typename F> struct Task : public TaskBase {
    // The main function of the user thread.
    F workFunction;
    Task(F wf) : workFunction(wf) {
        static_assert(sizeof(Task<F>) <= CACHE_LINE_SIZE, 
                "Arachne does not support tasks larger than the size of a cache line.");
    }
   void runThread() {
       workFunction();
   }
};

/*
 * This class holds all the state for a running user thread.
 */
struct UserContext {
    // This is a pointer to the lowest valid memory address in the user thread stack.
    void* sp;
    
    // The top of the stack of the current workunit, used for both indicating
    // whether this is new or old work, and also for making it easier to
    // recycle the stacks.
    void* stack;

    // When a thread enters the sleep queue, it will keep its wakup time
    // here.
    uint64_t wakeUpTimeInCycles;

    // This flag is a signal that this thread should run at the next opportunity.
    // It should be cleared immediately before a thread begins to run and
    // should be set by either remote cores as a signal or when a thread
    // yields.
    volatile bool wakeup;

    // Index of this UserContext in the vector of UserContexts for this core.
    // This gives us the correct bit to clear in the bit vector when we
    // complete a task.
    uint8_t index;


    // Wrapping this array into a struct appears to mitigate the strict
    // aliasing warnings.
    struct alignas(sizeof(void*)) {
        char data[CACHE_LINE_SIZE];
    } task;
};

typedef UserContext* ThreadId;

const int stackSize = 1024 * 1024;
const int stackPoolSize = 1000;
const int maxThreadsPerCore = 56;

/**
  * The following data structures and functions are private to the thread library.
  */
void schedulerMainLoop();
void  savecontext(void **target);
void  swapcontext(void **saved, void **target);
void createNewRunnableThread();
extern thread_local int kernelThreadId;
extern thread_local UserContext *running;
extern thread_local UserContext* activeList;
extern std::vector<UserContext*> activeLists;

// This structure holds a bitmask of occupied flags and a count of busy slots.
struct MaskAndCount{
    unsigned long int occupied : 56;
    uint8_t count : 8;
};
extern std::atomic<MaskAndCount>  *occupiedAndCount;
extern thread_local std::atomic<MaskAndCount>  *localOccupiedAndCount;

/**
  * Create a user thread to run the function f with the given args on the provided core.
  * Pass in -1 as a core ID to use the current core.
  */
template<typename _Callable, typename... _Args>
    int createThread(int coreId, _Callable&& __f, _Args&&... __args) {
    if (coreId == -1) coreId = kernelThreadId;

    auto task = std::bind(std::forward<_Callable>(__f), std::forward<_Args>(__args)...);

    bool success;
    int index;
    do {
        // Attempt to enqueue the task to the specific core in this case.
        MaskAndCount slotMap = occupiedAndCount[coreId];
        MaskAndCount oldSlotMap = slotMap;

        // Search for a non-occupied slot and attempt to reserve the slot
        // TODO: Try variations on this and see if we can make it faster if we
        // measure it to be too slow.
        index = 0;
        while (slotMap.occupied & (1 << index))
            index++;
        
        if (index > 55) {
            printf("Reached maximum thread capacity for this core, should throttle but aborting for now");
            exit(1);
        }

        slotMap.occupied |= (1 << index);
        slotMap.count++;

        success = occupiedAndCount[coreId].compare_exchange_strong(oldSlotMap,
                slotMap);
    } while (!success);

    // Copy in the data
    new (&activeLists[coreId][index].task) Arachne::Task<decltype(task)>(task);

    // Set wakeup flag
    activeLists[coreId][index].index = index;
    activeLists[coreId][index].wakeup = true;

    return 0;
}

// A random number generator from the internets.
inline unsigned long xorshf96(void) {
    static unsigned long x=123456789, y=362436069, z=521288629;
    unsigned long t;
    x ^= x << 16;
    x ^= x >> 5;
    x ^= x << 1;

    t = x;
    x = y;
    y = z;
    z = t ^ x ^ y;

    return z;
}

/**
  * This is the thread creation function that is used in real systems, since it
  * does load balancing.
  */
template<typename _Callable, typename... _Args>
    int createThread(_Callable&& __f, _Args&&... __args) {

    // Find a core to enqueue to by picking two at random.
    int coreId;
    int choice1 = xorshf96() % numCores;
    int choice2 = xorshf96() % numCores;
    while (choice2 == choice1) choice2 = xorshf96() % numCores;

    if (occupiedAndCount[choice1].load().count < occupiedAndCount[choice2].load().count)
        coreId = choice1;
    else
        coreId = choice2;

    return createThread(coreId, __f, __args...);
}

void threadMainFunction(int id);
void threadInit();
void mainThreadJoinPool();
void yield();
void sleep(uint64_t ns);
ThreadId getThreadId();
void block();
void signal(ThreadId id);
void setBlockingState();


// The following data structures and functions are  technically part of the
// private API and should be in a different header. Move them in the CPP
// refactor.

void setcontext(void **context);
void swapcontext(void **saved, void **target);
void savecontext(void **target);
void checkSleepQueue();
}
#endif
