#include <setjmp.h>
#include <functional>


namespace  Arachne {

int createTask(std::function<void()> task);
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

