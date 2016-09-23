#include "gtest/gtest.h" 
#include "Arachne.h"



void blocker() {
    Arachne::block();
}

TEST(BlockSignalTest, Signal) {
    Arachne::UserContext tempContext;
    tempContext.wakeup = false;
    Arachne::signal(&tempContext);
    EXPECT_EQ(true, tempContext.wakeup);
}

TEST(BlockSignalTest, AwakenBlockedThread) {
    Arachne::threadInit();
    Arachne::createThread(0, blocker);
    EXPECT_EQ(1, Arachne::occupiedAndCount[0].load().count);
    EXPECT_EQ(1, Arachne::occupiedAndCount[0].load().occupied);

    Arachne::signal(Arachne::activeLists[0]);
    while (Arachne::occupiedAndCount[0].load().count == 1);
    EXPECT_EQ(0, Arachne::occupiedAndCount[0].load().occupied);
}
