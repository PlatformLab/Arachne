#include <setjmp.h>
#include <functional>


namespace  Arachne {

// Cannot do default arguments if we have varargs.
template<typename _Callable, typename... _Args>
    int createThread(int coreId, _Callable&& __f, _Args&&... __args);

// Preserve closure style as well, for those who do not want to read disgusting templates.
int createTask(std::function<void()> task, int coreId);

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

