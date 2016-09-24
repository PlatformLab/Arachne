#include "gtest/gtest.h" 
#include "Arachne.h"
#include "Condition.h"


static Arachne::SpinLock mutex;
static Arachne::condition_variable cv;
volatile int awaited;

static void waiter() {
    mutex.lock();
    while (!awaited)
        cv.wait(mutex);
    awaited--;
    mutex.unlock();
}
TEST(ConditionTest, NotifyOne) {
    Arachne::numCores = 2;
    Arachne::threadInit();
    awaited = 0; 
    Arachne::createThread(0, waiter);
    Arachne::createThread(0, waiter);
    EXPECT_EQ(2, Arachne::occupiedAndCount[0].load().count);
    EXPECT_EQ(3, Arachne::occupiedAndCount[0].load().occupied);
    awaited = 2;
    cv.notify_one();
    while (awaited == 2);
    EXPECT_EQ(1, awaited);
    cv.notify_one();
    while (awaited == 1);
    EXPECT_EQ(0, awaited);
}

TEST(ConditionTest, NotifyAll) {
    Arachne::numCores = 2;
    Arachne::threadInit();
    awaited = 0; 
	for (int i = 0; i < 10; i++)
		Arachne::createThread(0, waiter);
    awaited = 5;
    cv.notify_all();
    while(Arachne::occupiedAndCount[0].load().count > 5);
    EXPECT_EQ(0, awaited);
    awaited = 5;
    cv.notify_all();
}
