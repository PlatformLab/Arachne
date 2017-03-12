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

#define private public
#include "Arachne.h"

namespace Arachne {

struct ArachneTest : public ::testing::Test {
    virtual void SetUp()
    {
        Arachne::numCores = 3;
        Arachne::maxNumCores = 3;
        Arachne::init();
    }

    virtual void TearDown()
    {
        shutDown();
        waitForTermination();
    }
};

// Shared data structures used in multiple tests.
char outputBuffer[1024];
std::atomic<int> completionCounter;
static Arachne::SleepLock sleepLock;

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
template <typename L>
static void
lockTaker(L *mutex) {
    flag = 1;
    mutex->lock();
    EXPECT_EQ(loadedContext, mutex->owner);
    mutex->unlock();
    flag = 0;
}

TEST_F(ArachneTest, SpinLock_lockUnlock) {
    EXPECT_EQ(NULL, loadedContext);
    flag = 0;
    mutex.lock();
    createThreadOnCore(0, lockTaker<SpinLock>, &mutex);
    limitedTimeWait([]() -> bool {return flag;});
    EXPECT_EQ(1, flag);
    usleep(1);
    EXPECT_EQ(1, flag);
    EXPECT_EQ(NULL, mutex.owner);
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
    sleep(1E9 + 5000);
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

// This is needed because SleepLocks cannot be taken by non-Arachne threads.
void sleepLockTest() {
    flag = 0;
    sleepLock.lock();
    Arachne::ThreadId tid = createThreadOnCore(0, lockTaker<SleepLock>, &sleepLock);
    Arachne::sleep(1000);
    limitedTimeWait([]() -> bool {return flag;});
    EXPECT_EQ(1, flag);
    EXPECT_EQ(Arachne::BLOCKED, tid.context->wakeupTimeInCycles);
    Arachne::sleep(1000);
    EXPECT_EQ(1, flag);
    EXPECT_EQ(Arachne::loadedContext, sleepLock.owner);
    sleepLock.unlock();
    limitedTimeWait([]() -> bool {return !flag;});
    EXPECT_EQ(0, flag);
    flag = 2;
}

TEST_F(ArachneTest, SleepLock) {
    EXPECT_EQ(NULL, loadedContext);
    Arachne::createThreadOnCore(1, sleepLockTest);
    limitedTimeWait([]() -> bool {return flag == 2;});
}

void sleepOnLock(int id) {
    std::lock_guard<SleepLock> guard(sleepLock);
    char tempBuffer[100];
    memset(tempBuffer, 0, 100);
    sprintf(tempBuffer, "T %d takes lock.\n", id);
    strcat(outputBuffer, tempBuffer);
    completionCounter++;
}
void lockHolder() {
    std::lock_guard<SleepLock> guard(sleepLock);
    yield();
    completionCounter++;
}

void silentLocker() {
    std::lock_guard<SleepLock> guard(sleepLock);
    completionCounter++;
}

TEST_F(ArachneTest, SleepLock_fairness) {
    memset(outputBuffer, 0, 1024);
    completionCounter = 0;
    createThreadOnCore(0, lockHolder);
    for (int i = 0; i < 20; i ++) {
        createThreadOnCore(0, sleepOnLock, i);
        // Interference on another core should not change the order
        createThreadOnCore(1, silentLocker);
    }

    limitedTimeWait([]()->bool {
            return completionCounter == 41;});

    char tempBuffer[100];
    char inputBuffer[1024];
    memset(inputBuffer, 0, 1024);
    for (int i = 0; i < 20; i ++) {
        memset(tempBuffer, 0, 100);
        sprintf(tempBuffer, "T %d takes lock.\n", i);
        strcat(inputBuffer, tempBuffer);
    }
    EXPECT_STREQ(inputBuffer, outputBuffer);
}

void sleepLockTryLockTest() {
    sleepLock.lock();
    EXPECT_FALSE(sleepLock.try_lock());
    sleepLock.unlock();
    EXPECT_TRUE(sleepLock.try_lock());
    sleepLock.unlock();
}
TEST_F(ArachneTest, SleepLock_tryLock) {
    createThread(sleepLockTryLockTest);
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
    EXPECT_EQ(0U, Arachne::occupiedAndCount[0]->load().numOccupied);
    EXPECT_EQ(0U, Arachne::occupiedAndCount[0]->load().occupied);
    createThreadOnCore(0, clearFlag);

    // This test may be a little fragile since it depends on the internal
    // structure of std::function
    EXPECT_EQ(reinterpret_cast<uint64_t>(clearFlag),
            *(reinterpret_cast<uint64_t*>(
                    &Arachne::allThreadContexts[0][0]->threadInvocation) + 1));
    EXPECT_EQ(1U, Arachne::occupiedAndCount[0]->load().numOccupied);
    EXPECT_EQ(1U, Arachne::occupiedAndCount[0]->load().occupied);
    threadCreationIndicator = 1;

    // Wait for thread to exit
    limitedTimeWait([]()-> bool {
            return Arachne::occupiedAndCount[0]->load().numOccupied != 1;});
    EXPECT_EQ(0U, Arachne::occupiedAndCount[0]->load().numOccupied);
    EXPECT_EQ(0U, Arachne::occupiedAndCount[0]->load().occupied);
}

TEST_F(ArachneTest, createThread_withArgs) {
    createThreadOnCore(0, setFlagForCreation, 2);
    EXPECT_EQ(1U, Arachne::occupiedAndCount[0]->load().numOccupied);
    EXPECT_EQ(1U, Arachne::occupiedAndCount[0]->load().occupied);
    EXPECT_EQ(0, threadCreationIndicator);
    threadCreationIndicator = 1;
    limitedTimeWait([]()->bool { return threadCreationIndicator != 1; });
    EXPECT_EQ(2, threadCreationIndicator);
    threadCreationIndicator = 0;
}

TEST_F(ArachneTest, createThread_findCorrectSlot) {
    // Seed the occupiedAndCount with some values first
    *occupiedAndCount[0] = {0b1011, 3};
    EXPECT_EQ(3U, Arachne::occupiedAndCount[0]->load().numOccupied);
    EXPECT_EQ(0b1011U, Arachne::occupiedAndCount[0]->load().occupied);

    createThreadOnCore(0, setFlagForCreation, 2);
    EXPECT_EQ(4U, Arachne::occupiedAndCount[0]->load().numOccupied);
    EXPECT_EQ(0b1111U, Arachne::occupiedAndCount[0]->load().occupied);
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
        EXPECT_NE(Arachne::NullThread, createThreadOnCore(0, clearFlag));
    EXPECT_EQ(Arachne::NullThread, createThreadOnCore(0, clearFlag));

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
    EXPECT_EQ(1U, Arachne::occupiedAndCount[1]->load().numOccupied);
    EXPECT_EQ(1U, Arachne::occupiedAndCount[1]->load().occupied);
    threadCreationIndicator = 1;

    limitedTimeWait(
            []()->bool { return occupiedAndCount[1]->load().numOccupied == 0;});

    mockRandomValues.push_back(0);
    mockRandomValues.push_back(1);
    *occupiedAndCount[1] = {0b1011, 3};
    createThread(clearFlag);
    EXPECT_EQ(1U, Arachne::occupiedAndCount[0]->load().numOccupied);
    EXPECT_EQ(1U, Arachne::occupiedAndCount[0]->load().occupied);
    threadCreationIndicator = 1;
    *occupiedAndCount[1] = {0, 0};
}

void* cacheAlignAlloc(size_t size);

TEST_F(ArachneTest, cacheAlignAlloc) {
    void* ptr = cacheAlignAlloc(7);
    EXPECT_EQ(0U, reinterpret_cast<uint64_t>(ptr) & (CACHE_LINE_SIZE - 1));
    free(ptr);
    ptr = cacheAlignAlloc(63);
    EXPECT_EQ(0U, reinterpret_cast<uint64_t>(ptr) & (CACHE_LINE_SIZE - 1));
    free(ptr);
}

extern std::vector<void*> kernelThreadStacks;

// Helper method for schedulerMainLoop
void
checkSchedulerState() {
    EXPECT_EQ(BLOCKED, loadedContext->wakeupTimeInCycles);
    EXPECT_EQ(1U, localOccupiedAndCount->load().numOccupied);
    EXPECT_EQ(1U, localOccupiedAndCount->load().occupied);
}

TEST_F(ArachneTest, schedulerMainLoop) {
    createThreadOnCore(0, checkSchedulerState);
    limitedTimeWait([]()->bool {
            return occupiedAndCount[0]->load().numOccupied == 0; });

    EXPECT_EQ(UNOCCUPIED, allThreadContexts[0][0]->wakeupTimeInCycles);
    EXPECT_EQ(2U, allThreadContexts[0][0]->generation);
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
    init();
    keepYielding = true;
    createThreadOnCore(0, yielder);

    flag = 0;
    createThreadOnCore(0, setFlag);
    limitedTimeWait([]()->bool {
            return Arachne::occupiedAndCount[0]->load().numOccupied <= 1;});
    EXPECT_EQ(1, flag);
    flag = 0;
    keepYielding = false;
}

TEST_F(ArachneTest, yield_allThreadsRan) {
    Arachne::numCores = 2;
    Arachne::init();
    keepYielding = true;
    flag = 0;

    createThreadOnCore(0, bitSetter, 0);
    createThreadOnCore(0, bitSetter, 1);
    createThreadOnCore(0, bitSetter, 2);
    limitedTimeWait([]()->bool {return flag == 7;});
    keepYielding = false;
}

using PerfUtils::Cycles;

void
sleeper() {
    uint64_t before = Cycles::rdtsc();
    Arachne::sleep(1000);
    uint64_t delta = Cycles::toNanoseconds(Cycles::rdtsc() - before);
    EXPECT_LE(1000UL, delta);
}

void
simplesleeper() {
    Arachne::sleep(10000);
    flag = 1;
    limitedTimeWait([]()->bool { return !flag; });
}

TEST_F(ArachneTest, sleep_minimumDelay) {
    numCores = 2;
    init();
    createThreadOnCore(0, sleeper);
}

TEST_F(ArachneTest, sleep_wakeupTimeSetAndCleared) {
    Arachne::numCores = 2;
    Arachne::init();
    flag = 0;
    createThreadOnCore(0, simplesleeper);
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
    Arachne::ThreadId id = createThreadOnCore(0, blocker);
    EXPECT_EQ(1U, Arachne::occupiedAndCount[0]->load().numOccupied);
    EXPECT_EQ(1U, Arachne::occupiedAndCount[0]->load().occupied);

    limitedTimeWait([]()->bool { return blockerHasStarted;});
    Arachne::signal(id);
    limitedTimeWait([]()->bool {
            return Arachne::occupiedAndCount[0]->load().numOccupied < 1;});
    EXPECT_EQ(0U, Arachne::occupiedAndCount[0]->load().occupied);
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
    EXPECT_EQ(0U, tempContext->wakeupTimeInCycles);
}

// This buffer does not need protection because the threads writing to it are
// deliberately scheduled onto the same core so only one will run at a time.

void
blockingThread() {
    strcat(outputBuffer, "Thread 1 blocking.");
    Arachne::block();
    strcat(outputBuffer, "Thread 1 unblocked.");
    completionCounter++;
}
void signalingThread(ThreadId toBeSignaled) {
    strcat(outputBuffer, "Thread 2 signaling.");
    signal(toBeSignaled);
    EXPECT_EQ(1LU, *publicPriorityMasks[0]);
    completionCounter++;
}
void normalThread() {
    strcat(outputBuffer, "Thread 3 running.");
    completionCounter++;
}

TEST_F(ArachneTest, block_priorities) {
    memset(outputBuffer, 0, 1024);
    completionCounter = 0;
    ThreadId blocking = createThreadOnCore(0, blockingThread);
    createThreadOnCore(0, signalingThread, blocking);
    createThreadOnCore(0, normalThread);
    limitedTimeWait([]()->bool {
            return completionCounter == 3;});
    EXPECT_STREQ("Thread 1 blocking.Thread 2 signaling.Thread 1 unblocked."
            "Thread 3 running.", outputBuffer);
}

static Arachne::ThreadId joineeId;

void
joinee() {
    EXPECT_LE(1U, Arachne::occupiedAndCount[0]->load().numOccupied);
}

void
joiner() {
    Arachne::join(joineeId);
    EXPECT_EQ(1U, Arachne::occupiedAndCount[0]->load().numOccupied);
}

void
joinee2() {
    Arachne::yield();
}

TEST_F(ArachneTest, join_afterTermination) {
    Arachne::numCores = 2;
    Arachne::init();

    // Since the joinee does not yield, we know that it terminated before the
    // joiner got a chance to run.
    joineeId = createThreadOnCore(0, joinee);
    createThreadOnCore(0, joiner);

    // Wait for threads to finish so that tests do not interfere with each
    // other.
    limitedTimeWait([]()->bool {
            return Arachne::occupiedAndCount[0]->load().numOccupied == 0;});
}

TEST_F(ArachneTest, join_DuringRun) {
    Arachne::numCores = 2;
    Arachne::init();
    joineeId = createThreadOnCore(0, joinee2);
    createThreadOnCore(0, joiner);
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
    Arachne::init(&argc, argv);
    EXPECT_EQ(3, argc);
    EXPECT_EQ(originalArgv, argv);
    EXPECT_EQ(3U, numCores);
    EXPECT_EQ(1024 * 1024, stackSize);
}

TEST_F(ArachneTest, parseOptions_longOptions) {
    // See comment in parseOptions_noOptions
    shutDown();
    waitForTermination();

    int argc = 7;
    const char* argv[] =
        {"ArachneTest", "--numCores", "5", "--stackSize", "4096",
            "--maxNumCores", "6"};
    Arachne::init(&argc, argv);
    EXPECT_EQ(1, argc);
    EXPECT_EQ(5U, numCores);
    EXPECT_EQ(stackSize, 4096);
    EXPECT_EQ(numCores, 5U);
    EXPECT_EQ(Arachne::maxNumCores, 6U);
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
    Arachne::init(&argc, argv);
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
    Arachne::init(&argc, argv);
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
    createThreadOnCore(0, waiter);
    createThreadOnCore(0, waiter);
    EXPECT_EQ(2U, Arachne::occupiedAndCount[0]->load().numOccupied);
    EXPECT_EQ(3U, Arachne::occupiedAndCount[0]->load().occupied);
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
        createThreadOnCore(0, waiter);
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
    createThreadOnCore(0, timedWaiter);
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

void doNothing() { }
extern volatile uint32_t numCoresPrecursor;

TEST_F(ArachneTest, incrementCoreCount) {
    void incrementCoreCount();
    shutDown();
    waitForTermination();
    maxNumCores = 4;
    Arachne::init();

    char *str;
    size_t size;
    FILE* newStream = open_memstream(&str, &size);
    setErrorStream(newStream);
    EXPECT_EQ(3U, numCoresPrecursor);
    EXPECT_EQ(NullThread, createThreadOnCore(3, doNothing));
    incrementCoreCount();
    EXPECT_EQ(4U, numCoresPrecursor);
    limitedTimeWait([]() -> bool { return numCores > 3;});
    EXPECT_NE(NullThread, createThreadOnCore(3, doNothing));
    fflush(newStream);
    EXPECT_EQ("Number of cores increasing from 3 to 4\n", std::string(str));
    free(str);
}
} // namespace Arachne
