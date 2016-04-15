#include <setjmp.h>
#include <functional>
#include <vector>
#include <mutex>
#include <deque>
#include "SpinLock.h"
#include "CacheTrace.h"


namespace  Arachne {

/*
 * This base class for WorkUnit allows us to maintain pointers for WorkUnits
 * without knowing the templated type ahead of time.
 */
struct WorkBase {
   // This function is defined in the subclass and allows us to prepare for 
   virtual void runThread() = 0;

   virtual ~WorkBase() {}

    // This is a pointer to the lowest valid memory address in the user thread stack.
    void* sp;

    // This flag is set on completion of a thread's main function. When set,
    // the thread dispatcher can reclaim the stack for this thread.
    bool finished; 
    
    // The top of the stack of the current workunit, used for both indicating
    // whether this is new or old work, and also for making it easier to
    // recycle the stacks.
    void* stack;

    // When a thread enters the sleep queue, it will keep its wakup time
    // here.
    uint64_t wakeUpTimeInCycles;
};

template <typename F> struct WorkUnit : WorkBase {
    void runThread() {
        workFunction();
    }

    // The main function of the user thread.
    F workFunction;

    WorkUnit(F wf) : workFunction(wf) {}

};

const int stackSize = 1024 * 1024;
const int stackPoolSize = 1000;

void threadWrapper();
void  savecontext(void **target);
void  swapcontext(void **saved, void **target);
extern thread_local int kernelThreadId;
extern SpinLock *workQueueLocks;
extern std::vector<std::deque<void*> > stackPool;
extern std::vector<std::deque<WorkBase* > > workQueues;

template <typename F> int createThread(F task, int coreId = -1);

template<typename _Callable, typename... _Args>
    int createThread(int coreId, _Callable&& __f, _Args&&... __args) {
    auto bound = std::bind(std::forward<_Callable>(__f), std::forward<_Args>(__args)...);
    PerfUtils::CacheTrace::getGlobalInstance()->record("Just bound the arguments", PerfUtils::Util::serialReadPmc(1));
    return createThread(bound, coreId);
}

/**
 * Create a WorkUnit for the given task, on the same queue as the current
 * function.
 */
template <typename F> int createThread(F task, int coreId) {
    if (coreId == -1) coreId = kernelThreadId;
    PerfUtils::CacheTrace::getGlobalInstance()->record("Before workQueueLock", PerfUtils::Util::serialReadPmc(1));
    std::lock_guard<SpinLock> guard(workQueueLocks[coreId]);
    PerfUtils::CacheTrace::getGlobalInstance()->record("Acquired workQueueLock", PerfUtils::Util::serialReadPmc(1));
    if (stackPool[coreId].empty()) return -1;
    PerfUtils::CacheTrace::getGlobalInstance()->record("Checked stackPool", PerfUtils::Util::serialReadPmc(1));

    WorkUnit<F> *work = new WorkUnit<F>(task); // TODO: Get rid of the new here.
    work->finished = false;
    PerfUtils::CacheTrace::getGlobalInstance()->record("Allocated WorkUnit and initialized", PerfUtils::Util::serialReadPmc(1));

    work->stack = stackPool[coreId].front();
    stackPool[coreId].pop_front();
    workQueues[coreId].push_back(work);
    PerfUtils::CacheTrace::getGlobalInstance()->record("Added work to workQueue", PerfUtils::Util::serialReadPmc(1));

    // Wrap the function to restore control when the user thread
    // terminates instead of yielding.
    work->sp = (char*) work->stack + stackSize - 64; 
    // set up the stack to pass the single argument in this case.
    *(void**) work->sp = (void*) threadWrapper;

    // Set up the initial stack with the registers from the current thread.
    savecontext(&work->sp);
    PerfUtils::CacheTrace::getGlobalInstance()->record("Finished saving context", PerfUtils::Util::serialReadPmc(1));
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

