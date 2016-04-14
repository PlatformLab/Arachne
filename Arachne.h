#include <setjmp.h>
#include <functional>
#include "CacheTrace.h"


namespace  Arachne {

// Preserve closure style as well, for those who do not want to read disgusting templates.
int createThread(std::function<void()> task, int coreId = -1);

template<typename _Callable, typename... _Args>
    int createThread(int coreId, _Callable&& __f, _Args&&... __args) {
    auto bound = std::bind(std::forward<_Callable>(__f), std::forward<_Args>(__args)...);
    PerfUtils::CacheTrace::getGlobalInstance()->record("Just bound the arguments", PerfUtils::Util::serialReadPmc(1));
    return createThread(bound, coreId);
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

