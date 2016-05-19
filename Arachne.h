#include <functional>
#include <vector>
#include <mutex>
#include <deque>
#include <atomic>
#include "SpinLock.h"


namespace  Arachne {

/*
 * This enum represents the state of a Task object.
 * EMPTY => This slot is available for enqueing.
 * FILLING => This slot is currently being filled.
 * FILLED => This slot is available for reading.
 */
enum TaskState {
    EMPTY = 0,
    FILLING,
    FILLED
};

/*
 * This base class for WorkUnit allows us to maintain pointers for WorkUnits
 * without knowing the templated type ahead of time.
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
};

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
struct alignas(64) TaskBox {
   union {
       std::atomic<TaskState> loadState{}; // Default initialized
       char pad[CACHE_LINE_SIZE];
   } state;

   // This wrapper enables us to copy out the fixed length array by value, we
   // can then use a reinterpret_cast to convert to a TaskBase* and invoke the
   // actual function.
   struct Task {
       char taskBuf[CACHE_LINE_SIZE];
   } task;

   // This function is used in the second step of invoking a function scheduled by another core.
   // 0. Core polling loop detects that there is work enqueued.
   // 1. Caller copy out the function and arguments onto their local stack
   // 2. Callers free this structure by setting the loadState to EMPTY again.
   // 3. Caller invoke the function or yield, depending on priority.
   Task getTask() {
       return task;
   }
};



const int stackSize = 1024 * 1024;
const int stackPoolSize = 1000;

void schedulerMainLoop();
void  savecontext(void **target);
void  swapcontext(void **saved, void **target);
extern thread_local int kernelThreadId;
extern SpinLock *workQueueLocks;
extern std::vector<std::deque<void*> > stackPool;
extern std::vector<std::deque<UserContext* > > workQueues;
extern TaskBox* taskBoxes;

/**
  * Create a user thread to run the function f with the given args on the provided core.
  * Pass in -1 as a core ID to use the current core.
  */
template<typename _Callable, typename... _Args>
    int createThread(int coreId, _Callable&& __f, _Args&&... __args) {
    if (coreId == -1) coreId = kernelThreadId;

    auto task = std::bind(std::forward<_Callable>(__f), std::forward<_Args>(__args)...);

    // Attempt to enqueue the task by first checking the status
    auto& taskBox = taskBoxes[coreId];
    auto expectedTaskState = EMPTY; // Because of compare_exchange_strong requires a reference
    if (!taskBox.state.loadState.compare_exchange_strong(expectedTaskState, FILLING)) {
        fprintf(stderr, "Fast path for thread creation was blocked, and slow " 
                "path is not yet implemented. Exiting...\n");
        exit(0);
    }

    new (&taskBox.task) Arachne::Task<decltype(task)>(task);

    // Notify the target thread that the taskBox has been loaded
    expectedTaskState = FILLING;
    if (!taskBox.state.loadState.compare_exchange_strong(expectedTaskState, FILLED)) {
        fprintf(stderr, "TaskBox is not in FILLING State after work has been " 
                "placed. This is beyond expectation! Exiting....\n");
        exit(0);
    }

    return 0;
}

void threadMainFunction(int id);
void threadInit();
void mainThreadJoinPool();
void yield();
void sleep(uint64_t ns);


// The following data structures and functions are  technically part of the
// private API and should be in a different header. Move them in the CPP
// refactor.

void setcontext(void **context);
void swapcontext(void **saved, void **target);
void savecontext(void **target);
void checkSleepQueue();
}
