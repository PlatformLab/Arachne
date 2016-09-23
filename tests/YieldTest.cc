#include "gtest/gtest.h" 
#include "Arachne.h"


volatile int keepYielding;
volatile static int flag;
static void yielder() {
    while (keepYielding)
        Arachne::yield();
}

static void setFlag() {
    flag = 1;
}

static void bitSetter(int index) {
    while (keepYielding) {
        flag |= (1 << index);
        Arachne::yield();
    }
}

TEST(YieldTest, SecondThreadGotControl) {
   Arachne::numCores = 2;
   Arachne::threadInit();
   keepYielding = true; 
   Arachne::createThread(0, yielder);

   flag = 0;
   Arachne::createThread(0, setFlag);
   while (Arachne::occupiedAndCount[0].load().count > 1);
   EXPECT_EQ(1, flag);
   flag = 0;
   keepYielding = false;
}

TEST(YieldTest, AllThreadsRan) {
   Arachne::numCores = 2;
   Arachne::threadInit();
   keepYielding = true; 
   flag = 0;

   Arachne::createThread(0, bitSetter, 0);
   Arachne::createThread(0, bitSetter, 1);
   Arachne::createThread(0, bitSetter, 2);
   usleep(1);

   keepYielding = false;
   EXPECT_EQ(7, flag);
}
