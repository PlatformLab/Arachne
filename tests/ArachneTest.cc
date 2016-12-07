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

#include <thread>
#include "Cycles.h"
#include "gtest/gtest.h"
#include "Arachne.h"

namespace Arachne {

struct ArachneTest : public ::testing::Test {
    virtual void SetUp()
    {
        Arachne::numCores = 3;
        Arachne::threadInit();
    }

    virtual void TearDown()
    {
        shutDown();
        waitForTermination();
    }
};

// Helper function for tests with timing dependencies, so that we wait for a
// finite amount of time in the case of a bug causing an infinite loop.
static void limitedTimeWait(std::function<bool()> condition) {
    for (int i = 0; i < 1000; i++) {
        if (condition()) {
            break;
        }
        usleep(1000);
    }
    // We use assert here because an infinite loop will result in TearDown
    // not being able to complete, so we might as well terminate the tests here.
    ASSERT_TRUE(condition());
}

static Arachne::SpinLock mutex;
static Arachne::ConditionVariable cv;
static volatile int numWaitedOn;
static volatile int flag;

// Helper function for SpinLock tests
static void
lockTaker() {
    flag = 1;
    mutex.lock();
    mutex.unlock();
    flag = 0;
}

TEST_F(ArachneTest, SpinLock_lockUnlock) {
    flag = 0;
    mutex.lock();
    createThread(0, lockTaker);
    limitedTimeWait([]() -> bool {return flag;});
    EXPECT_EQ(1, flag);
    usleep(1);
    EXPECT_EQ(1, flag);
    mutex.unlock();
    limitedTimeWait([]() -> bool {return !flag;});
    EXPECT_EQ(0, flag);
}

static void
lockContender(SpinLock& lock) {
    lock.lock();
    lock.unlock();
}

TEST_F(ArachneTest, SpinLock_printWarning) {
    Arachne::testInit();
    char *str;
    size_t size;
    FILE* newStream = open_memstream(&str, &size);
    setErrorStream(newStream);


    SpinLock lock("SpinLockTest");
    lock.lock();
    Arachne::ThreadId contender = createThread(lockContender, std::ref(lock));
    sleep(1E9 + 2000);
    lock.unlock();
    join(contender);
    Arachne::testDestroy();

    fflush(newStream);
    setErrorStream(stderr);
    EXPECT_EQ("SpinLockTest SpinLock locked for one second; deadlock?\n",
            std::string(str));
    free(str);
}

TEST_F(ArachneTest, SpinLock_tryLock) {
    mutex.lock();
    EXPECT_FALSE(mutex.try_lock());
    mutex.unlock();
    EXPECT_TRUE(mutex.try_lock());
    mutex.unlock();
}

// Helper functions for thread creation tests.
static volatile int threadCreationIndicator = 0;

void
clearFlag() {
    limitedTimeWait([]()-> bool {
            return threadCreationIndicator;});
    threadCreationIndicator = 0;
}

void
setFlagForCreation(int a) {
    limitedTimeWait([]()-> bool {
            return threadCreationIndicator;});
    threadCreationIndicator = a;
}

TEST_F(ArachneTest, createThread_noArgs) {
    EXPECT_EQ(0, Arachne::occupiedAndCount[0]->load().numOccupied);
    EXPECT_EQ(0, Arachne::occupiedAndCount[0]->load().occupied);
    createThread(0, clearFlag);

    // This test may be a little fragile since it depends on the internal
    // structure of std::function
    EXPECT_EQ(reinterpret_cast<uint64_t>(clearFlag),
            *(reinterpret_cast<uint64_t*>(
                    &Arachne::allThreadContexts[0][0]->threadInvocation) + 1));
    EXPECT_EQ(1, Arachne::occupiedAndCount[0]->load().numOccupied);
    EXPECT_EQ(1, Arachne::occupiedAndCount[0]->load().occupied);
    threadCreationIndicator = 1;

    // Wait for thread to exit
    limitedTimeWait([]()-> bool {
            return Arachne::occupiedAndCount[0]->load().numOccupied != 1;});
    EXPECT_EQ(0, Arachne::occupiedAndCount[0]->load().numOccupied);
    EXPECT_EQ(0, Arachne::occupiedAndCount[0]->load().occupied);
}

TEST_F(ArachneTest, createThread_withArgs) {
    createThread(0, setFlagForCreation, 2);
    EXPECT_EQ(1, Arachne::occupiedAndCount[0]->load().numOccupied);
    EXPECT_EQ(1, Arachne::occupiedAndCount[0]->load().occupied);
    EXPECT_EQ(0, threadCreationIndicator);
    threadCreationIndicator = 1;
    limitedTimeWait([]()->bool { return threadCreationIndicator != 1; });
    EXPECT_EQ(2, threadCreationIndicator);
    threadCreationIndicator = 0;
}

TEST_F(ArachneTest, createThread_findCorrectSlot) {
    // Seed the occupiedAndCount with some values first
    *occupiedAndCount[0] = {0b1011, 3};
    EXPECT_EQ(3, Arachne::occupiedAndCount[0]->load().numOccupied);
    EXPECT_EQ(0b1011, Arachne::occupiedAndCount[0]->load().occupied);

    createThread(0, setFlagForCreation, 2);
    EXPECT_EQ(4, Arachne::occupiedAndCount[0]->load().numOccupied);
    EXPECT_EQ(0b1111, Arachne::occupiedAndCount[0]->load().occupied);
    EXPECT_EQ(0, threadCreationIndicator);
    threadCreationIndicator = 1;
    limitedTimeWait([]()->bool { return threadCreationIndicator != 1; });
    EXPECT_EQ(2, threadCreationIndicator);
    threadCreationIndicator = 0;

    limitedTimeWait([]()->bool {
            return occupiedAndCount[0]->load().numOccupied != 4; });

    // Clear out the seeded occupiedAndCount
    *occupiedAndCount[0] = {0, 0};
}

TEST_F(ArachneTest, createThread_maxThreadsExceeded) {
    for (int i = 0; i < Arachne::maxThreadsPerCore; i++)
        EXPECT_NE(Arachne::NullThread, createThread(0, clearFlag));
    EXPECT_EQ(Arachne::NullThread, createThread(0, clearFlag));

    // Clean up the threads
    while (Arachne::occupiedAndCount[0]->load().numOccupied > 0)
        threadCreationIndicator = 1;
    threadCreationIndicator = 0;
}

TEST_F(ArachneTest, createThread_pickLeastLoaded) {
    mockRandomValues.push_back(0);
    mockRandomValues.push_back(0);
    mockRandomValues.push_back(1);
    createThread(clearFlag);
    EXPECT_EQ(1, Arachne::occupiedAndCount[1]->load().numOccupied);
    EXPECT_EQ(1, Arachne::occupiedAndCount[1]->load().occupied);
    threadCreationIndicator = 1;

    limitedTimeWait(
            []()->bool { return occupiedAndCount[1]->load().numOccupied == 0;});

    mockRandomValues.push_back(0);
    mockRandomValues.push_back(1);
    *occupiedAndCount[1] = {0b1011, 3};
    createThread(clearFlag);
    EXPECT_EQ(1, Arachne::occupiedAndCount[0]->load().numOccupied);
    EXPECT_EQ(1, Arachne::occupiedAndCount[0]->load().occupied);
    threadCreationIndicator = 1;
    *occupiedAndCount[1] = {0, 0};
}

void* cacheAlignAlloc(size_t size);

TEST_F(ArachneTest, cacheAlignAlloc) {
    void* ptr = cacheAlignAlloc(7);
    EXPECT_EQ(0, reinterpret_cast<uint64_t>(ptr) & (CACHE_LINE_SIZE - 1));
    free(ptr);
    ptr = cacheAlignAlloc(63);
    EXPECT_EQ(0, reinterpret_cast<uint64_t>(ptr) & (CACHE_LINE_SIZE - 1));
    free(ptr);
}

extern std::vector<void*> kernelThreadStacks;

// These file-scope variables are used to test swapcontext.
static const size_t testStackSize = 256;
static char stack[testStackSize];
static void* stackPointer;
static void *oldStackPointer;

static bool swapContextSuccess = 0;

void
swapContextHelper() {
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
void
checkSchedulerState() {
    EXPECT_EQ(BLOCKED, loadedContext->wakeupTimeInCycles);
    EXPECT_EQ(1, localOccupiedAndCount->load().numOccupied);
    EXPECT_EQ(1, localOccupiedAndCount->load().occupied);
}

TEST_F(ArachneTest, schedulerMainLoop) {
    createThread(0, checkSchedulerState);
    limitedTimeWait([]()->bool {
            return occupiedAndCount[0]->load().numOccupied == 0; });

    //TODO(hq6): Update this test to check for the new sentinel value on a dead
    //thread instead.
    EXPECT_EQ(UNOCCUPIED, allThreadContexts[0][0]->wakeupTimeInCycles);
    EXPECT_EQ(2, allThreadContexts[0][0]->generation);
}

static volatile int keepYielding;

static void
yielder() {
    while (keepYielding)
        Arachne::yield();
}

static void
setFlag() {
    flag = 1;
}

static void
bitSetter(int index) {
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
    limitedTimeWait([]()->bool {
            return Arachne::occupiedAndCount[0]->load().numOccupied <= 1;});
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
    limitedTimeWait([]()->bool {return flag == 7;});
    keepYielding = false;
}

using PerfUtils::Cycles;

void
sleeper() {
    uint64_t before = Cycles::rdtsc();
    Arachne::sleep(1000);
    uint64_t delta = Cycles::toNanoseconds(Cycles::rdtsc() - before);
    EXPECT_LE(1000, delta);
}

void
simplesleeper() {
    Arachne::sleep(10000);
    flag = 1;
    limitedTimeWait([]()->bool { return !flag; });
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
    limitedTimeWait([]()->bool { return flag; });
    EXPECT_EQ(BLOCKED, Arachne::allThreadContexts[0][0]->wakeupTimeInCycles);
    flag = 0;
}

volatile bool blockerHasStarted;

void
blocker() {
    blockerHasStarted = true;
    Arachne::block();
}

TEST_F(ArachneTest, block_basics) {
    Arachne::ThreadId id = createThread(0, blocker);
    EXPECT_EQ(1, Arachne::occupiedAndCount[0]->load().numOccupied);
    EXPECT_EQ(1, Arachne::occupiedAndCount[0]->load().occupied);

    limitedTimeWait([]()->bool { return blockerHasStarted;});
    Arachne::signal(id);
    limitedTimeWait([]()->bool {
            return Arachne::occupiedAndCount[0]->load().numOccupied < 1;});
    EXPECT_EQ(0, Arachne::occupiedAndCount[0]->load().occupied);
}

TEST_F(ArachneTest, signal) {
    // We use a malloc here because we have deleted the constructor for
    // ThreadContext.
    ThreadContext *tempContext =
        reinterpret_cast<ThreadContext*>(
                malloc(sizeof(Arachne::ThreadContext)));
    tempContext->generation = 0;
    tempContext->wakeupTimeInCycles = BLOCKED;
    Arachne::signal(ThreadId(tempContext, 0));
    EXPECT_EQ(0, tempContext->wakeupTimeInCycles);
}

static Arachne::ThreadId joineeId;

void
joinee() {
    EXPECT_LE(1, Arachne::occupiedAndCount[0]->load().numOccupied);
}

void
joiner() {
    Arachne::join(joineeId);
    EXPECT_EQ(1, Arachne::occupiedAndCount[0]->load().numOccupied);
}

void
joinee2() {
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
    limitedTimeWait([]()->bool {
            return Arachne::occupiedAndCount[0]->load().numOccupied == 0;});
}

TEST_F(ArachneTest, join_DuringRun) {
    Arachne::numCores = 2;
    Arachne::threadInit();
    joineeId = createThread(0, joinee2);
    createThread(0, joiner);
    limitedTimeWait([]() ->bool {
            return Arachne::occupiedAndCount[0]->load().numOccupied == 0;});
}

extern int stackSize;
TEST_F(ArachneTest, parseOptions_noOptions) {
    // Since Google Test requires all tests by the same name to either use or
    // not use the fixture, we must de-initialize so that we can initialize
    // again to test argument parsing.
    shutDown();
    waitForTermination();

    int argc = 3;
    const char* originalArgv[] = {"ArachneTest", "foo", "bar"};
    const char** argv = originalArgv;
    Arachne::threadInit(&argc, argv);
    EXPECT_EQ(3, argc);
    EXPECT_EQ(originalArgv, argv);
    EXPECT_EQ(3, numCores);
    EXPECT_EQ(1024 * 1024, stackSize);
}

TEST_F(ArachneTest, parseOptions_longOptions) {
    // See comment in parseOptions_noOptions
    shutDown();
    waitForTermination();

    int argc = 5;
    const char* argv[] =
        {"ArachneTest", "--numCores", "5", "--stackSize", "4096"};
    Arachne::threadInit(&argc, argv);
    EXPECT_EQ(1, argc);
    EXPECT_EQ(5, numCores);
    EXPECT_EQ(stackSize, 4096);
    EXPECT_EQ(numCores, 5);
}

TEST_F(ArachneTest, parseOptions_mixedOptions) {
    // See comment in parseOptions_noOptions
    shutDown();
    waitForTermination();

    int argc = 7;
    const char* originalArgv[] =
        {"ArachneTest", "--appOptionB", "2", "--stackSize", "2048",
            "--appOptionA", "Argument"};
    const char** argv = originalArgv;
    Arachne::threadInit(&argc, argv);
    EXPECT_EQ(5, argc);
    EXPECT_EQ(stackSize, 2048);
    EXPECT_EQ("--appOptionB", argv[1]);
    EXPECT_EQ("--appOptionA", argv[3]);
}

TEST_F(ArachneTest, parseOptions_appOptionsOnly) {
    // See comment in parseOptions_noOptions
    shutDown();
    waitForTermination();

    int argc = 3;
    const char* argv[] = {"ArachneTest", "--appOptionA", "Argument"};
    Arachne::threadInit(&argc, argv);
    EXPECT_EQ(3, argc);
}

// Helper function for condition variable tests.
static void
waiter() {
    mutex.lock();
    while (!numWaitedOn)
        cv.wait(mutex);
    numWaitedOn--;
    mutex.unlock();
}

TEST_F(ArachneTest, ConditionVariable_notifyOne) {
    numWaitedOn = 0;
    createThread(0, waiter);
    createThread(0, waiter);
    EXPECT_EQ(2, Arachne::occupiedAndCount[0]->load().numOccupied);
    EXPECT_EQ(3, Arachne::occupiedAndCount[0]->load().occupied);
    numWaitedOn = 2;
    mutex.lock();
    cv.notifyOne();
    mutex.unlock();
    limitedTimeWait([]() -> bool {return numWaitedOn != 2;});
    // We test for GE here because it is possible that one of the two threads
    // ran after numWaitedOn = 2 was set, which means it would not wait at all.
    EXPECT_GE(1, numWaitedOn);
    mutex.lock();
    cv.notifyOne();
    mutex.unlock();
    limitedTimeWait([]() -> bool {return numWaitedOn != 1;});
    EXPECT_EQ(0, numWaitedOn);
}

TEST_F(ArachneTest, ConditionVariable_notifyAll) {
    mutex.lock();
    numWaitedOn = 0;
    for (int i = 0; i < 10; i++)
        createThread(0, waiter);
    numWaitedOn = 5;
    cv.notifyAll();
    mutex.unlock();
    limitedTimeWait([]()-> bool {
            return Arachne::occupiedAndCount[0]->load().numOccupied <= 5;});
    mutex.lock();
    EXPECT_EQ(0, numWaitedOn);
    numWaitedOn = 5;
    cv.notifyAll();
    mutex.unlock();
}

static void
timedWaiter() {
    mutex.lock();
    cv.waitFor(mutex, 80000);
    numWaitedOn--;
}

TEST_F(ArachneTest, ConditionVariable_waitFor) {
    numWaitedOn = 1;
    createThread(0, timedWaiter);
    limitedTimeWait([]() -> bool {return numWaitedOn != 1;});
    EXPECT_EQ(0, numWaitedOn);
}

TEST_F(ArachneTest, setErrorStream) {
    char *str;
    size_t size;
    FILE* newStream = open_memstream(&str, &size);
    setErrorStream(newStream);
    fprintf(errorStream, "FooBar");
    fflush(newStream);
    setErrorStream(stderr);
    EXPECT_EQ("FooBar", std::string(str));
    free(str);
}

TEST_F(ArachneTest, incrementCoreCount) {
    void incrementCoreCount();

    EXPECT_EQ(3, occupiedAndCount.size());
    EXPECT_EQ(3, allThreadContexts.size());
    incrementCoreCount();
    limitedTimeWait([]() -> bool { return numCores > 3;});
    EXPECT_EQ(4, occupiedAndCount.size());
    EXPECT_EQ(4, allThreadContexts.size());
}
} // namespace Arachne
