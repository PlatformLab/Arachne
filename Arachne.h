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

// This structure holds the flags for protecting the enqueuing of a new Task to
// a particular core, as well as the function and arguments of that task.

// It is important for performance reasons that the two parts are in different
// cache lines, since the flag will be polled on by a particular core, and set
// only once loading is complete.
union alignas(64) TaskBox {
    // This wrapper enables us to copy out the fixed length array by value, we
    // can then use a reinterpret_cast to convert to a TaskBase* and invoke the
    // actual function.
    struct Task {
        char taskBuf[CACHE_LINE_SIZE];
    } task;

   // This function is used in the second step of invoking a function scheduled
   // by another core.
   // 0. Core polling loop detects that there is work enqueued.
   // 1. Caller copy out the function and arguments onto their local stack
   // 2. Callers free this structure by setting the loadState to EMPTY again.
   // 3. Caller invoke the function or yield, depending on priority.
   Task getTask() {
       return task;
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


    // This flag is a signal that this thread should run at the next opportunity.
    // It should be cleared immediately before a thread begins to run and
    // should be set by either remote cores as a signal or when a thread
    // yields.
    volatile bool wakeup;

    // Index of this UserContext in the vector of UserContexts for this core.
    // This gives us the correct bit to clear in the bit vector when we
    // complete a task.
    uint8_t index;

    // When a thread enters the sleep queue, it will keep its wakup time
    // here.
    uint64_t wakeUpTimeInCycles;

    TaskBox taskBox;
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
extern thread_local std::vector<UserContext*> *maybeRunnable;
extern std::vector<std::vector<UserContext*> > activeLists;

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
    new (&activeLists[coreId][index]->taskBox.task) Arachne::Task<decltype(task)>(task);

    // Set wakeup flag
    activeLists[coreId][index]->wakeup = true;

    return 0;
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
