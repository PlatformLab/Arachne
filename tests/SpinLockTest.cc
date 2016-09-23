#include "gtest/gtest.h" 
#include "Arachne.h"
#include "SpinLock.h"

static volatile int flag;
static Arachne::SpinLock mutex;

static void lockTaker() {
    flag = 1;
    mutex.lock();
    mutex.unlock();
    flag = 0;
}

TEST(SpinLockTest, Exclusion) {
    Arachne::numCores = 2;
    Arachne::threadInit();
    flag = 0;
    mutex.lock();
    Arachne::createThread(0, lockTaker);
    while (!flag);
    EXPECT_EQ(1, flag);
    mutex.unlock();
    while (flag);
    EXPECT_EQ(0, flag);
}

TEST(SpinLockTest, TryLock) {
    mutex.lock();
    EXPECT_FALSE(mutex.try_lock());
    mutex.unlock();
    EXPECT_TRUE(mutex.try_lock());
    mutex.unlock();
}
