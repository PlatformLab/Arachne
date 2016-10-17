/* Copyright (c) 2015-2016 Stanford University
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR(S) DISCLAIM ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL AUTHORS BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "gtest/gtest.h"
#include "Arachne.h"
#include "Cycles.h"

namespace Arachne {

struct ArachneTest : public ::testing::Test {
    virtual void SetUp()
    {
        Arachne::numCores = 2;
        Arachne::threadInit();
    }

    virtual void TearDown()
    {
        Arachne::threadDestroy();
    }
};

static Arachne::SpinLock mutex;
static Arachne::ConditionVariable cv;
static volatile int numWaitedOn;
static volatile int flag;

// Helper function for SpinLock tests
static void lockTaker() {
    flag = 1;
    mutex.lock();
    mutex.unlock();
    flag = 0;
}

TEST_F(ArachneTest, spinLock_exclusion) {
    flag = 0;
    mutex.lock();
    createThread(0, lockTaker);
    while (!flag);
    EXPECT_EQ(1, flag);
    mutex.unlock();
    while (flag);
    EXPECT_EQ(0, flag);
}

TEST_F(ArachneTest, spinLock_tryLock) {
    mutex.lock();
    EXPECT_FALSE(mutex.try_lock());
    mutex.unlock();
    EXPECT_TRUE(mutex.try_lock());
    mutex.unlock();
}

// Helper function for condition variable tests.
static void waiter() {
    mutex.lock();
    while (!numWaitedOn)
        cv.wait(mutex);
    numWaitedOn--;
    mutex.unlock();
}

TEST_F(ArachneTest, conditionVariable_notifyOne) {
    numWaitedOn = 0;
    createThread(0, waiter);
    createThread(0, waiter);
    EXPECT_EQ(2, Arachne::occupiedAndCount[0].load().numOccupied);
    EXPECT_EQ(3, Arachne::occupiedAndCount[0].load().occupied);
    numWaitedOn = 2;
    mutex.lock();
    cv.notifyOne();
    mutex.unlock();
    while (numWaitedOn == 2);
    // We test for GE here because it is possible that one of the two threads
    // ran after numWaitedOn = 2 was set, which means it would not wait at all.
    EXPECT_GE(1, numWaitedOn);
    mutex.lock();
    cv.notifyOne();
    mutex.unlock();
    while (numWaitedOn == 1);
    EXPECT_EQ(0, numWaitedOn);
}

TEST_F(ArachneTest, conditionVariable_notifyAll) {
    mutex.lock();
    numWaitedOn = 0;
    for (int i = 0; i < 10; i++)
        createThread(0, waiter);
    numWaitedOn = 5;
    cv.notifyAll();
    mutex.unlock();
    while (Arachne::occupiedAndCount[0].load().numOccupied > 5);
    mutex.lock();
    EXPECT_EQ(0, numWaitedOn);
    numWaitedOn = 5;
    cv.notifyAll();
    mutex.unlock();
}

// Helper functions for thread creation tests.
static volatile int threadCreationIndicator = 0;

void clearFlag() {
    while (!threadCreationIndicator);
    threadCreationIndicator = 0;
}

void setFlagForCreation(int a) {
    while (!threadCreationIndicator);
    threadCreationIndicator = a;
}

TEST_F(ArachneTest, createThread_noArgs) {
    // By assigning the number of cores, we can run this test independently of
    // the number of cores on the machine.
    Arachne::numCores = 2;

    Arachne::threadInit();
    EXPECT_EQ(0, Arachne::occupiedAndCount[0].load().numOccupied);
    EXPECT_EQ(0, Arachne::occupiedAndCount[0].load().occupied);
    createThread(0, clearFlag);

    // This test may be a little fragile since it depends on the internal
    // structure of std::function
    EXPECT_EQ(reinterpret_cast<uint64_t>(clearFlag),
            *(reinterpret_cast<uint64_t*>(
                    &Arachne::activeLists[0]->threadInvocation) + 1));
    EXPECT_EQ(1, Arachne::occupiedAndCount[0].load().numOccupied);
    EXPECT_EQ(1, Arachne::occupiedAndCount[0].load().occupied);
    threadCreationIndicator = 1;

    // Wait for thread to exit
    while (Arachne::occupiedAndCount[0].load().numOccupied == 1);
    EXPECT_EQ(0, Arachne::occupiedAndCount[0].load().numOccupied);
    EXPECT_EQ(0, Arachne::occupiedAndCount[0].load().occupied);
}

TEST_F(ArachneTest, createThread_withArgs) {
    // By assigning the number of cores, we can run this test independently of
    // the number of cores on the machine.
    Arachne::numCores = 2;
    Arachne::threadInit();
    createThread(0, setFlagForCreation, 2);
    EXPECT_EQ(1, Arachne::occupiedAndCount[0].load().numOccupied);
    EXPECT_EQ(1, Arachne::occupiedAndCount[0].load().occupied);
    EXPECT_EQ(0, threadCreationIndicator);
    threadCreationIndicator = 1;
    while (threadCreationIndicator == 1);
    EXPECT_EQ(2, threadCreationIndicator);
    threadCreationIndicator = 0;
}

TEST_F(ArachneTest, createThread_maxThreadsExceeded) {
    // By assigning the number of cores, we can run this test independently of
    // the number of cores on the machine.
    Arachne::numCores = 2;
    Arachne::threadInit();
    for (int i = 0; i < Arachne::maxThreadsPerCore; i++)
        EXPECT_NE(Arachne::NullThread, createThread(0, clearFlag));
    EXPECT_EQ(Arachne::NullThread, createThread(0, clearFlag));

    // Clean up the threads
    while (Arachne::occupiedAndCount[0].load().numOccupied > 0)
        threadCreationIndicator = 1;
    threadCreationIndicator = 0;
}

void* cacheAlignAlloc(size_t size);

TEST_F(ArachneTest, cacheAlignAlloc) {
    // Multiple of alignment size
    void* ptr = cacheAlignAlloc(CACHE_LINE_SIZE);
    EXPECT_EQ(0, reinterpret_cast<uint64_t>(ptr) & (CACHE_LINE_SIZE - 1));
    free(ptr);
    ptr = cacheAlignAlloc(63);
    EXPECT_EQ(0, reinterpret_cast<uint64_t>(ptr) & (CACHE_LINE_SIZE - 1));
    free(ptr);

}

extern std::vector<void*> kernelThreadStacks;

void threadMainHelper() {
    swapcontext(&kernelThreadStacks[kernelThreadId], &runningContext->sp);
}

TEST_F(ArachneTest, threadMain) {
    createThread(1, threadMainHelper);
    threadMain(1);
    EXPECT_EQ(activeList, activeLists[kernelThreadId]);
    EXPECT_EQ(localOccupiedAndCount, occupiedAndCount + kernelThreadId);
    EXPECT_EQ(1, localOccupiedAndCount->load().numOccupied);
    // Manually clean up the state that the schedulerMainLoop would do.
    *localOccupiedAndCount = {0, 0};
}

static const size_t testStackSize = 256;
static char stack[testStackSize];
static void* stackPointer;
static void *oldStackPointer;

static bool swapContextSuccess;

void swapContextHelper() {
    swapContextSuccess = 1;
    Arachne::swapcontext(&oldStackPointer, &stackPointer);
}

TEST_F(ArachneTest, swapContext) {
    swapContextSuccess = 0;
    stackPointer = stack + testStackSize;
    *reinterpret_cast<void**>(stackPointer) =
        reinterpret_cast<void*>(swapContextHelper);
    EXPECT_EQ(256, reinterpret_cast<char*>(stackPointer) -
            reinterpret_cast<char*>(stack));
    stackPointer = reinterpret_cast<char*>(stackPointer) -
        Arachne::SpaceForSavedRegisters;
    EXPECT_EQ(208, reinterpret_cast<char*>(stackPointer) -
            reinterpret_cast<char*>(stack));
    Arachne::swapcontext(&stackPointer, &oldStackPointer);
    EXPECT_EQ(1, swapContextSuccess);
}

// Helper method for schedulerMainLoop
void checkSchedulerState() {
    EXPECT_EQ(~0L, runningContext->wakeupTimeInCycles);
    EXPECT_EQ(1, localOccupiedAndCount->load().numOccupied);
    EXPECT_EQ(1, localOccupiedAndCount->load().occupied);
}

TEST_F(ArachneTest, schedulerMainLoop) {
    createThread(0, checkSchedulerState);
}

static volatile int keepYielding;

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

TEST_F(ArachneTest, yield_secondThreadGotControl) {
    numCores = 2;
    threadInit();
    keepYielding = true;
    createThread(0, yielder);

    flag = 0;
    createThread(0, setFlag);
    while (Arachne::occupiedAndCount[0].load().numOccupied > 1);
    EXPECT_EQ(1, flag);
    flag = 0;
    keepYielding = false;
}

TEST_F(ArachneTest, yield_allThreadsRan) {
    Arachne::numCores = 2;
    Arachne::threadInit();
    keepYielding = true;
    flag = 0;

    createThread(0, bitSetter, 0);
    createThread(0, bitSetter, 1);
    createThread(0, bitSetter, 2);
    usleep(100);

    keepYielding = false;
    EXPECT_EQ(7, flag);
}

using PerfUtils::Cycles;

void sleeper() {
    uint64_t before = Cycles::rdtsc();
    Arachne::sleep(1000);
    uint64_t delta = Cycles::toNanoseconds(Cycles::rdtsc() - before);
    EXPECT_LE(1000, delta);
}

void simplesleeper() {
    Arachne::sleep(10000);
    flag = 1;
    while (flag);
}

TEST_F(ArachneTest, sleep_minimumDelay) {
    numCores = 2;
    threadInit();
    createThread(0, sleeper);
}

TEST_F(ArachneTest, sleep_wakeupTimeSetAndCleared) {
    Arachne::numCores = 2;
    Arachne::threadInit();
    flag = 0;
    createThread(0, simplesleeper);
    while (!flag);
    EXPECT_EQ(~0L, Arachne::activeLists[0]->wakeupTimeInCycles);
    flag = 0;
}

volatile bool blockerHasStarted;

void blocker() {
    blockerHasStarted = true;
    Arachne::block();
}

TEST_F(ArachneTest, blockSignal) {
    Arachne::ThreadId id = createThread(0, blocker);
    EXPECT_EQ(1, Arachne::occupiedAndCount[0].load().numOccupied);
    EXPECT_EQ(1, Arachne::occupiedAndCount[0].load().occupied);

    while (!blockerHasStarted);
    Arachne::signal(id);
    while (Arachne::occupiedAndCount[0].load().numOccupied == 1);
    EXPECT_EQ(0, Arachne::occupiedAndCount[0].load().occupied);
}

TEST_F(ArachneTest, signal) {
    // We use a malloc here because we have deleted the constructor for
    // ThreadContext.
    ThreadContext *tempContext =
        reinterpret_cast<ThreadContext*>(
                malloc(sizeof(Arachne::ThreadContext)));
    tempContext->generation = 0;
    tempContext->wakeupTimeInCycles = ~0L;
    Arachne::signal(ThreadId(tempContext, 0));
    EXPECT_EQ(0, tempContext->wakeupTimeInCycles);
}

static Arachne::ThreadId joineeId;

void joinee() {
    EXPECT_LE(1, Arachne::occupiedAndCount[0].load().numOccupied);
}

void joiner() {
    Arachne::join(joineeId);
    EXPECT_EQ(1, Arachne::occupiedAndCount[0].load().numOccupied);
}

void joinee2() {
    Arachne::yield();
}

TEST_F(ArachneTest, join_afterTermination) {
    Arachne::numCores = 2;
    Arachne::threadInit();

    // Since the joinee does not yield, we know that it terminated before the
    // joiner got a chance to run.
    joineeId = createThread(0, joinee);
    createThread(0, joiner);

    // Wait for threads to finish so that tests do not interfere with each
    // other.
    while (Arachne::occupiedAndCount[0].load().numOccupied > 0);
}

TEST_F(ArachneTest, join_DuringRun) {
    Arachne::numCores = 2;
    Arachne::threadInit();
    joineeId = createThread(0, joinee2);
    createThread(0, joiner);
    while (Arachne::occupiedAndCount[0].load().numOccupied > 0);
}

} // namespace Arachne
