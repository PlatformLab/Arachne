#include <setjmp.h>
#include <functional>


namespace  Arachne {
struct WorkUnit {
    std::function<void()> workFunction;
    bool finished;
    // The stack of the current workunit, used for both indicating whether this
    // is new or old work, and also for making it easier to recycle the stacks.
    void* stack;
    // Used for storing the context when yielding
    jmp_buf env;
};

void createTask(std::function<void()> task);
void threadMainFunction(int id);
void threadInit();
void threadWrapper(WorkUnit work);
void mainThreadJoinPool();

}

