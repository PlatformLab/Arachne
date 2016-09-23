#include "gtest/gtest.h" 
#include "Arachne.h"



volatile int flag = 0;
void clearFlag() {
    while (!flag);
    flag = 0;
}

void setFlag(int a) {
   while (!flag);
   flag = a;
}

TEST(CreateThreadTest, NoArgs) {
    // By assigning the number of cores, we can run this test independently of the number of cores on the machine.
    Arachne::numCores = 2;

    Arachne::threadInit();
    EXPECT_EQ(0, Arachne::occupiedAndCount[0].load().count);
    EXPECT_EQ(0, Arachne::occupiedAndCount[0].load().occupied);
    Arachne::createThread(0, clearFlag);

    // This test may be a little fragile since it depends on the internal
    // structure of std::function
    EXPECT_EQ(reinterpret_cast<uint64_t>(clearFlag), *(reinterpret_cast<uint64_t*>(&Arachne::activeLists[0]->task) + 1));
    EXPECT_EQ(1, Arachne::occupiedAndCount[0].load().count);
    EXPECT_EQ(1, Arachne::occupiedAndCount[0].load().occupied);
    flag = 1;
    
    // Wait for thread to exit
    while (Arachne::occupiedAndCount[0].load().count == 1);
    EXPECT_EQ(0, Arachne::occupiedAndCount[0].load().count);
    EXPECT_EQ(0, Arachne::occupiedAndCount[0].load().occupied);
}

TEST(CreateThreadTest, WithArgs) {
    // By assigning the number of cores, we can run this test independently of the number of cores on the machine.
    Arachne::numCores = 2;
    Arachne::threadInit();
    Arachne::createThread(0, setFlag, 2);
    EXPECT_EQ(1, Arachne::occupiedAndCount[0].load().count);
    EXPECT_EQ(1, Arachne::occupiedAndCount[0].load().occupied);
    EXPECT_EQ(0, flag);
    flag = 1;
    while (flag == 1);
    EXPECT_EQ(2, flag);
    flag = 0;
}

TEST(CreateThreadTest, MaxThreadsExceeded) {
    // By assigning the number of cores, we can run this test independently of the number of cores on the machine.
    Arachne::numCores = 2;
    Arachne::threadInit();
    for (int i = 0; i < Arachne::maxThreadsPerCore; i++)
        EXPECT_EQ(0, Arachne::createThread(0, clearFlag));
//    EXPECT_EQ(-1, Arachne::createThread(0, clearFlag));

    // Clean up the threads
    while (Arachne::occupiedAndCount[0].load().count > 0)
        flag = 1;
    flag = 0;
}
