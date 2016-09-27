#include "gtest/gtest.h"
#include "Arachne.h"
#include "Cycles.h"

using PerfUtils::Cycles;

void sleeper() {
    uint64_t before = Cycles::rdtsc();
    Arachne::sleep(1000);
    uint64_t delta = Cycles::toNanoseconds(Cycles::rdtsc() - before);
    EXPECT_LE(1000, delta);
}

static volatile int flag;
void simplesleeper() {
    Arachne::sleep(10000);
    flag = 1;
    while (flag);
}

TEST(SleepTest, MinimumDelay) {
    Arachne::numCores = 2;
    Arachne::threadInit();
    Arachne::createThread(0, sleeper);
}

TEST(SleepTest, WakeupTimeSetAndCleared) {
    Arachne::numCores = 2;
    Arachne::threadInit();
    flag = 0;
    Arachne::createThread(0, simplesleeper);
    while (!flag);
    EXPECT_EQ(0, Arachne::activeLists[0]->wakeupTimeInCycles);
    flag = 0;
}
