#include "gtest/gtest.h"
#include "Arachne.h"


// This test verifies that only one thread can join at a time
TEST(JoinTest, DoubleJoin) {
    Arachne::UserContext tempContext;
    tempContext.waiter = reinterpret_cast<Arachne::UserContext*>(1);;
    EXPECT_EQ(false, Arachne::join(&tempContext));
}

volatile Arachne::UserContext* joineeId;
volatile Arachne::UserContext* joinerId;
void joinee() {
   joineeId = Arachne::getThreadId(); 
}

void joiner() {
    joinerId = Arachne::getThreadId();
    while (!joineeId);
    EXPECT_EQ(true, Arachne::join(const_cast<Arachne::UserContext*>(joineeId)));
}

TEST(JoinTest, JoinAfterTermination) {
    Arachne::numCores = 2;
    Arachne::threadInit();

    // Since the joinee does not yield, we know that it terminated before the
    // jointer got a chance to run.
    Arachne::createThread(0, joinee);
    Arachne::createThread(0, joiner);
}

void joinee2() {
   joineeId = Arachne::getThreadId(); 
   Arachne::yield();
   EXPECT_EQ(joinerId, Arachne::running->waiter);
}

TEST(JoinTest, JoinDuringRun) {
    Arachne::numCores = 2;
    Arachne::threadInit();

    Arachne::createThread(0, joinee2);
    Arachne::createThread(0, joiner);
}
