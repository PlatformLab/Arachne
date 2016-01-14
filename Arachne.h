#include <setjmp.h>
#include <functional>


namespace  Arachne {

void createTask(std::function<void()> task);
void threadMainFunction(int id);
void threadInit();
void mainThreadJoinPool();
void yield();


// The following data structures and functions are  technically part of the
// private API and should be in a different header. Move them in the CPP
// refactor.

struct UserContext {
    void* esp;
    void* pc;
};

void setcontext(UserContext *context);
void swapcontext(UserContext *saved, UserContext *target);

}

