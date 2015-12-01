#include <setjmp.h>
#include <functional>


namespace  Arachne {

void createTask(std::function<void()> task);
void threadMainFunction(int id);
void threadInit();
void mainThreadJoinPool();

}

