#include <setjmp.h>
#include <functional>


namespace  Arachne {

// Preserve closure style as well, for those who do not want to read disgusting templates.
int createThread(std::function<void()> task, int coreId = -1);

template<typename _Callable, typename... _Args>
    int createThread(int coreId, _Callable&& __f, _Args&&... __args) {
    return createThread(std::bind(std::forward<_Callable>(__f), std::forward<_Args>(__args)...), coreId);
}


void threadMainFunction(int id);
void threadInit();
void mainThreadJoinPool();
void yield();


// The following data structures and functions are  technically part of the
// private API and should be in a different header. Move them in the CPP
// refactor.

void setcontext(void **context);
void swapcontext(void **saved, void **target);
void savecontext(void **target);
}

