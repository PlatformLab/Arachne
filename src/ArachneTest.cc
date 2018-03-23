/* Copyright (c) 2015-2018 Stanford University
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
#include "PerfUtils/Cycles.h"
#include "gtest/gtest.h"

#define private public
#include "Arachne.h"
#include "CoreArbiter/ArbiterClientShim.h"
#include "CoreArbiter/CoreArbiterClient.h"
#include "CoreArbiter/CoreArbiterServer.h"
#include "CoreArbiter/Logger.h"
#include "CoreArbiter/MockSyscall.h"
#include "DefaultCoreManager.h"

namespace Arachne {

// These macros are here because Arachne uses the CoreArbiter in its own unit
// tests, and these parameters are used for starting the CoreArbiter
// specifically for Arachne testing.
#define ARBITER_SOCKET "/tmp/CoreArbiter_ArachneTest/testsocket"
#define ARBITER_MEM "/tmp/CoreArbiter_ArachneTest/testmem"

using CoreArbiter::CoreArbiterClient;
using CoreArbiter::CoreArbiterServer;
using CoreArbiter::MockSyscall;

extern bool useCoreArbiter;
extern volatile bool coreChangeActive;

extern std::atomic<uint32_t> numActiveCores;
extern volatile uint32_t minNumCores;

extern std::string coreArbiterSocketPath;
extern CoreArbiterClient* coreArbiter;

static void limitedTimeWait(std::function<bool()> condition,
                            int numIterations = 1000);

struct Environment : public ::testing::Environment {
    CoreArbiterServer* coreArbiterServer;
    MockSyscall* sys;

    std::thread* coreArbiterServerThread;
    // Override this to define how to set up the environment.
    virtual void SetUp() {
        // Initalize core arbiter server
        CoreArbiter::Logger::setLogLevel(CoreArbiter::WARNING);
        sys = new MockSyscall();
        sys->callGeteuid = false;
        sys->geteuidResult = 0;
        CoreArbiterServer::testingSkipCpusetAllocation = true;

        CoreArbiterServer::sys = sys;
        coreArbiterServer = new CoreArbiterServer(std::string(ARBITER_SOCKET),
                                                  std::string(ARBITER_MEM),
                                                  {1, 2, 3, 4, 5, 6, 7}, false);
        coreArbiterServerThread =
            new std::thread([=] { coreArbiterServer->startArbitration(); });
    }
    // Override this to define how to tear down the environment.
    virtual void TearDown() {
        coreArbiterServer->endArbitration();
        coreArbiterServerThread->join();
        delete coreArbiterServerThread;
        delete coreArbiterServer;
        delete sys;
    }
};

__attribute__((unused))::testing::Environment* const testEnvironment =
    (useCoreArbiter) ? ::testing::AddGlobalTestEnvironment(new Environment)
                     : NULL;

struct ArachneTest : public ::testing::Test {
    virtual void SetUp() {
        Arachne::minNumCores = 1;
        Arachne::maxNumCores = 3;
        Arachne::disableLoadEstimation = true;
        Arachne::coreArbiterSocketPath = ARBITER_SOCKET;
        Arachne::init();
        // Articially wake up all threads for testing purposes
        std::vector<uint32_t> coreRequest({3, 0, 0, 0, 0, 0, 0, 0});
        coreArbiter->setRequestedCores(coreRequest);
        limitedTimeWait([]() -> bool { return numActiveCores == 3; });
    }

    virtual void TearDown() {
        // Unblock all cores so they can shut down and be joined.
        coreArbiter->setRequestedCores(
            {Arachne::maxNumCores, 0, 0, 0, 0, 0, 0, 0});

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
static void
limitedTimeWait(std::function<bool()> condition, int numIterations) {
    for (int i = 0; i < numIterations; i++) {
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

bool
canThreadBeCreatedOnCore(int threadClass, CoreManager* coreManager,
                         int coreId) {
    CoreList* coreList = coreManager->getCores(threadClass);
    for (uint32_t i = 0; i < coreList->size(); i++) {
        if (coreList->get(i) == coreId) {
            coreList->free();
            return true;
        }
    }
    coreList->free();
    return false;
}

// Helper function for SpinLock tests
template <typename L>
static void
lockTaker(L* mutex) {
    flag = 1;
    mutex->lock();
    EXPECT_EQ(core.loadedContext, mutex->owner);
    mutex->unlock();
    flag = 0;
}

TEST_F(ArachneTest, SpinLock_lockUnlock) {
    EXPECT_EQ(NULL, core.loadedContext);
    flag = 0;
    mutex.lock();
    EXPECT_NE(Arachne::NullThread,
              createThreadOnCore(0, lockTaker<SpinLock>, &mutex));
    limitedTimeWait([]() -> bool { return flag; });
    EXPECT_EQ(1, flag);
    usleep(1);
    EXPECT_EQ(1, flag);
    EXPECT_EQ(NULL, mutex.owner);
    mutex.unlock();
    limitedTimeWait([]() -> bool { return !flag; });
    EXPECT_EQ(0, flag);
}

static void
lockContender(SpinLock& lock) {
    lock.lock();
    lock.unlock();
}

TEST_F(ArachneTest, SpinLock_printWarning) {
    Arachne::testInit();
    char* str;
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
void
sleepLockTest() {
    flag = 0;
    sleepLock.lock();
    Arachne::ThreadId tid =
        createThreadOnCore(0, lockTaker<SleepLock>, &sleepLock);
    Arachne::sleep(1000);
    limitedTimeWait([]() -> bool { return flag; });
    EXPECT_EQ(1, flag);
    EXPECT_EQ(Arachne::BLOCKED, tid.context->wakeupTimeInCycles);
    Arachne::sleep(1000);
    EXPECT_EQ(1, flag);
    EXPECT_EQ(Arachne::core.loadedContext, sleepLock.owner);
    sleepLock.unlock();
    limitedTimeWait([]() -> bool { return !flag; });
    EXPECT_EQ(0, flag);
    flag = 2;
}

TEST_F(ArachneTest, SleepLock) {
    EXPECT_EQ(NULL, core.loadedContext);
    Arachne::createThreadOnCore(1, sleepLockTest);
    limitedTimeWait([]() -> bool { return flag == 2; });
}

void
sleepOnLock(int id) {
    std::lock_guard<SleepLock> guard(sleepLock);
    char tempBuffer[100];
    memset(tempBuffer, 0, 100);
    snprintf(tempBuffer, sizeof(tempBuffer), "T %d takes lock.\n", id);
    strcat(outputBuffer, tempBuffer);
    completionCounter++;
}
void
lockHolder() {
    std::lock_guard<SleepLock> guard(sleepLock);
    while (!completionCounter)
        yield();
}

void
silentLocker() {
    std::lock_guard<SleepLock> guard(sleepLock);
    completionCounter++;
}

TEST_F(ArachneTest, SleepLock_fairness) {
    memset(outputBuffer, 0, 1024);
    completionCounter = 0;
    createThreadOnCore(0, lockHolder);
    for (int i = 0; i < 20; i++) {
        ThreadId tid = createThreadOnCore(0, sleepOnLock, i);
        // Wait until this thread is actually running.
        limitedTimeWait([tid]() -> bool {
            return tid.context->wakeupTimeInCycles == BLOCKED;
        });

        // Interference on another core should not change the order
        createThreadOnCore(1, silentLocker);
    }
    // Allow the lockHolder to awaken and release the lock.
    completionCounter++;

    limitedTimeWait([]() -> bool { return completionCounter == 41; });

    char tempBuffer[100];
    char inputBuffer[1024];
    memset(inputBuffer, 0, 1024);
    for (int i = 0; i < 20; i++) {
        memset(tempBuffer, 0, 100);
        snprintf(tempBuffer, sizeof(tempBuffer), "T %d takes lock.\n", i);
        strcat(inputBuffer, tempBuffer);
    }
    EXPECT_STREQ(inputBuffer, outputBuffer);
}

void
sleepLockTryLockTest() {
    sleepLock.lock();
    EXPECT_FALSE(sleepLock.try_lock());
    sleepLock.unlock();
    EXPECT_TRUE(sleepLock.try_lock());
    sleepLock.unlock();
}
TEST_F(ArachneTest, SleepLock_tryLock) { createThread(sleepLockTryLockTest); }

// Helper functions for thread creation tests.
static volatile int threadCreationIndicator = 0;

void
clearFlag() {
    limitedTimeWait([]() -> bool { return threadCreationIndicator; });
    threadCreationIndicator = 0;
}

void
setFlagForCreation(int a) {
    limitedTimeWait([]() -> bool { return threadCreationIndicator; });
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
                    &Arachne::allThreadContexts[0][0]->threadInvocation) +
                1));
    EXPECT_EQ(1U, Arachne::occupiedAndCount[0]->load().numOccupied);
    EXPECT_EQ(1U, Arachne::occupiedAndCount[0]->load().occupied);
    threadCreationIndicator = 1;

    // Wait for thread to exit
    limitedTimeWait([]() -> bool {
        return Arachne::occupiedAndCount[0]->load().numOccupied != 1;
    });
    EXPECT_EQ(0U, Arachne::occupiedAndCount[0]->load().numOccupied);
    EXPECT_EQ(0U, Arachne::occupiedAndCount[0]->load().occupied);
}

TEST_F(ArachneTest, createThread_withArgs) {
    createThreadOnCore(0, setFlagForCreation, 2);
    EXPECT_EQ(1U, Arachne::occupiedAndCount[0]->load().numOccupied);
    EXPECT_EQ(1U, Arachne::occupiedAndCount[0]->load().occupied);
    EXPECT_EQ(0, threadCreationIndicator);
    threadCreationIndicator = 1;
    limitedTimeWait([]() -> bool { return threadCreationIndicator != 1; });
    EXPECT_EQ(2, threadCreationIndicator);
    threadCreationIndicator = 0;
}

TEST_F(ArachneTest, createThread_findCorrectSlot) {
    // Seed the occupiedAndCount with some values first
    // Note that this test only passes when the second or first slot is
    // unoccupied, because Arachne assumes that threads will be created in
    // order as an optimization; this implies higher slots will not be examined
    // until there is a runnable thread in lower slots.
    *occupiedAndCount[0] = {0b1101, 3};
    EXPECT_EQ(3U, Arachne::occupiedAndCount[0]->load().numOccupied);
    EXPECT_EQ(0b1101U, Arachne::occupiedAndCount[0]->load().occupied);

    threadCreationIndicator = 0;
    createThreadOnCore(0, setFlagForCreation, 2);
    EXPECT_EQ(4U, Arachne::occupiedAndCount[0]->load().numOccupied);
    EXPECT_EQ(0b1111U, Arachne::occupiedAndCount[0]->load().occupied);
    EXPECT_EQ(0, threadCreationIndicator);
    threadCreationIndicator = 1;
    limitedTimeWait([]() -> bool { return threadCreationIndicator != 1; });
    EXPECT_EQ(2, threadCreationIndicator);
    threadCreationIndicator = 0;

    limitedTimeWait(
        []() -> bool { return occupiedAndCount[0]->load().numOccupied != 4; });

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
    DefaultCoreManager* coreManager =
        reinterpret_cast<DefaultCoreManager*>(getCoreManagerForTest());
    mockRandomValues.push_back(0);
    mockRandomValues.push_back(0);
    mockRandomValues.push_back(1);
    createThread(clearFlag);
    int core0 = coreManager->getCores(0)->get(0);
    int core1 = coreManager->getCores(0)->get(1);
    EXPECT_EQ(1U, Arachne::occupiedAndCount[core1]->load().numOccupied);
    EXPECT_EQ(1U, Arachne::occupiedAndCount[core1]->load().occupied);
    threadCreationIndicator = 1;

    limitedTimeWait([core1]() -> bool {
        return occupiedAndCount[core1]->load().numOccupied == 0;
    });
    *occupiedAndCount[core1] = {0b1011, 3};

    mockRandomValues.push_back(0);
    mockRandomValues.push_back(1);
    createThread(clearFlag);
    EXPECT_EQ(1U, Arachne::occupiedAndCount[core0]->load().numOccupied);
    EXPECT_EQ(1U, Arachne::occupiedAndCount[core0]->load().occupied);
    threadCreationIndicator = 1;
    *occupiedAndCount[core1] = {0, 0};
}

TEST_F(ArachneTest, alignedAlloc) {
    void* ptr = alignedAlloc(7);
    EXPECT_EQ(0U, reinterpret_cast<uint64_t>(ptr) & (CACHE_LINE_SIZE - 1));
    free(ptr);
    ptr = alignedAlloc(63);
    EXPECT_EQ(0U, reinterpret_cast<uint64_t>(ptr) & (CACHE_LINE_SIZE - 1));
    free(ptr);
}

extern std::vector<void*> kernelThreadStacks;

// Helper method for schedulerMainLoop
void
checkSchedulerState() {
    EXPECT_EQ(BLOCKED, core.loadedContext->wakeupTimeInCycles);
    EXPECT_EQ(1U, core.localOccupiedAndCount->load().numOccupied);
    EXPECT_EQ(1U, core.localOccupiedAndCount->load().occupied);
}

TEST_F(ArachneTest, schedulerMainLoop) {
    createThreadOnCore(0, checkSchedulerState);
    limitedTimeWait(
        []() -> bool { return occupiedAndCount[0]->load().numOccupied == 0; });

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
    minNumCores = 2;
    Arachne::init();
    keepYielding = true;
    createThreadOnCore(0, yielder);

    flag = 0;
    createThreadOnCore(0, setFlag);
    limitedTimeWait([]() -> bool {
        return Arachne::occupiedAndCount[0]->load().numOccupied <= 1;
    });
    EXPECT_EQ(1, flag);
    flag = 0;
    keepYielding = false;
}

TEST_F(ArachneTest, yield_allThreadsRan) {
    Arachne::minNumCores = 2;
    Arachne::init();
    keepYielding = true;
    flag = 0;

    createThreadOnCore(0, bitSetter, 0);
    createThreadOnCore(0, bitSetter, 1);
    createThreadOnCore(0, bitSetter, 2);
    limitedTimeWait([]() -> bool { return flag == 7; });
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
    limitedTimeWait([]() -> bool { return !flag; });
}

TEST_F(ArachneTest, sleep_minimumDelay) {
    minNumCores = 2;
    init();
    createThreadOnCore(0, sleeper);
}

TEST_F(ArachneTest, sleep_wakeupTimeSetAndCleared) {
    Arachne::minNumCores = 2;
    Arachne::init();
    flag = 0;
    createThreadOnCore(0, simplesleeper);
    limitedTimeWait([]() -> bool { return flag; });
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

    limitedTimeWait([]() -> bool { return blockerHasStarted; });
    Arachne::signal(id);
    limitedTimeWait([]() -> bool {
        return Arachne::occupiedAndCount[0]->load().numOccupied < 1;
    });
    EXPECT_EQ(0U, Arachne::occupiedAndCount[0]->load().occupied);
}

TEST_F(ArachneTest, signal) {
    // We use a malloc here because we have deleted the constructor for
    // ThreadContext.
    ThreadContext tempContext(0, 0);
    tempContext.generation = 0;
    tempContext.wakeupTimeInCycles = BLOCKED;
    tempContext.coreId = 0;
    tempContext.idInCore = 0;
    Arachne::signal(ThreadId(&tempContext, 0));
    EXPECT_EQ(0U, tempContext.wakeupTimeInCycles);
    publicPriorityMasks[0] = 0;
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
void
signalingThread(ThreadId toBeSignaled) {
    strcat(outputBuffer, "Thread 2 signaling.");
    signal(toBeSignaled);
    EXPECT_EQ(1LU, *publicPriorityMasks[0]);
    completionCounter++;
}
void
normalThread() {
    strcat(outputBuffer, "Thread 3 running.");
    completionCounter++;
}

TEST_F(ArachneTest, block_priorities) {
    memset(outputBuffer, 0, 1024);
    completionCounter = 0;
    ThreadId blocking = createThreadOnCore(0, blockingThread);
    createThreadOnCore(0, signalingThread, blocking);
    createThreadOnCore(0, normalThread);
    limitedTimeWait([]() -> bool { return completionCounter == 3; });
    EXPECT_STREQ(
        "Thread 1 blocking.Thread 2 signaling.Thread 1 unblocked."
        "Thread 3 running.",
        outputBuffer);
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
    shutDown();
    waitForTermination();

    Arachne::minNumCores = 2;
    Arachne::init();

    // Since the joinee does not yield, we know that it terminated before the
    // joiner got a chance to run.
    joineeId = createThreadOnCore(0, joinee);
    createThreadOnCore(0, joiner);

    // Wait for threads to finish so that tests do not interfere with each
    // other.
    limitedTimeWait([]() -> bool {
        return Arachne::occupiedAndCount[0]->load().numOccupied == 0;
    });
}

TEST_F(ArachneTest, join_DuringRun) {
    shutDown();
    waitForTermination();

    Arachne::minNumCores = 2;
    Arachne::init();
    joineeId = createThreadOnCore(0, joinee2);
    createThreadOnCore(0, joiner);
    limitedTimeWait([]() -> bool {
        return Arachne::occupiedAndCount[0]->load().numOccupied == 0;
    });
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
    EXPECT_EQ(1U, minNumCores);
    EXPECT_EQ(1024 * 1024, stackSize);
}

TEST_F(ArachneTest, parseOptions_longOptions) {
    // See comment in parseOptions_noOptions
    shutDown();
    waitForTermination();

    int originalStackSize = stackSize;
    int argc = 9;
    const char* argv[] = {"ArachneTest",
                          "--minNumCores",
                          "5",
                          "--stackSize",
                          "4096",
                          "--maxNumCores",
                          "6",
                          "--enableArbiter",
                          "0"};
    Arachne::init(&argc, argv);
    EXPECT_EQ(1, argc);
    EXPECT_EQ(useCoreArbiter, false);
    EXPECT_EQ(5U, minNumCores);
    EXPECT_EQ(stackSize, 4096);
    EXPECT_EQ(minNumCores, 5U);
    EXPECT_EQ(Arachne::maxNumCores, 6U);
    shutDown();
    waitForTermination();
    stackSize = originalStackSize;
    Arachne::init();
}

TEST_F(ArachneTest, parseOptions_mixedOptions) {
    // See comment in parseOptions_noOptions
    shutDown();
    waitForTermination();

    int originalStackSize = stackSize;
    int argc = 7;
    const char* originalArgv[] = {"ArachneTest", "--appOptionB", "2",
                                  "--stackSize", "8192",         "--appOptionA",
                                  "Argument"};
    const char** argv = originalArgv;
    Arachne::init(&argc, argv);
    EXPECT_EQ(5, argc);
    EXPECT_EQ(stackSize, 8192);
    EXPECT_EQ("--appOptionB", argv[1]);
    EXPECT_EQ("--appOptionA", argv[3]);
    // Restore the stackSize. This races with cores trying to initialize
    // stacks, since the stack memory that was allocated is smaller than the
    // original stack size. We would like to allow thread creations before we
    // finish initializing stacks, since those operations are orthogonal.
    // Therefore, we have to first deinitialize the library, update the stack
    // size, and then reinitialize the library.
    shutDown();
    waitForTermination();
    stackSize = originalStackSize;
    Arachne::init();
}

TEST_F(ArachneTest, parseOptions_mixedOptions_noArbiter) {
    // See comment in parseOptions_noOptions
    shutDown();
    waitForTermination();

    int originalStackSize = stackSize;
    int argc = 9;
    const char* originalArgv[] = {"ArachneTest",
                                  "--appOptionB",
                                  "2",
                                  "--stackSize",
                                  "8192",
                                  "--appOptionA",
                                  "Argument",
                                  "--enableArbiter",
                                  "0"};
    const char** argv = originalArgv;
    Arachne::init(&argc, argv);
    EXPECT_EQ(5, argc);
    EXPECT_EQ(useCoreArbiter, false);
    EXPECT_EQ(stackSize, 8192);
    EXPECT_EQ("--appOptionB", argv[1]);
    EXPECT_EQ("--appOptionA", argv[3]);
    // Restore the stackSize. This races with cores trying to initialize
    // stacks, since the stack memory that was allocated is smaller than the
    // original stack size. We would like to allow thread creations before we
    // finish initializing stacks, since those operations are orthogonal.
    // Therefore, we have to first deinitialize the library, update the stack
    // size, and then reinitialize the library.
    shutDown();
    waitForTermination();
    stackSize = originalStackSize;
    Arachne::init();
}

TEST_F(ArachneTest, parseOptions_noArgumentOptions) {
    // See comment in parseOptions_noOptions
    shutDown();
    waitForTermination();
    Arachne::disableLoadEstimation = false;
    int argc = 4;
    const char* originalArgv[] = {"ArachneTest", "--disableLoadEstimation",
                                  "--minNumCores", "5"};
    const char** argv = originalArgv;
    Arachne::init(&argc, argv);
    EXPECT_EQ(1, argc);
    EXPECT_EQ(5U, Arachne::minNumCores);
    EXPECT_EQ(5U, Arachne::maxNumCores);
    EXPECT_EQ(true, disableLoadEstimation);
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
    limitedTimeWait([]() -> bool { return numWaitedOn != 2; });
    // We test for GE here because it is possible that one of the two threads
    // ran after numWaitedOn = 2 was set, which means it would not wait at all.
    EXPECT_GE(1, numWaitedOn);
    mutex.lock();
    cv.notifyOne();
    mutex.unlock();
    limitedTimeWait([]() -> bool { return numWaitedOn != 1; });
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
    limitedTimeWait([]() -> bool {
        return Arachne::occupiedAndCount[0]->load().numOccupied <= 5;
    });
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
    limitedTimeWait([]() -> bool { return numWaitedOn != 1; });
    EXPECT_EQ(0, numWaitedOn);
}

TEST_F(ArachneTest, setErrorStream) {
    char* str;
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
    shutDown();
    waitForTermination();
    maxNumCores = 4;
    Arachne::init();
    DefaultCoreManager* coreManager =
        reinterpret_cast<DefaultCoreManager*>(getCoreManagerForTest());
    // Articially wake up one less than maximum threads.
    std::vector<uint32_t> coreRequest({3, 0, 0, 0, 0, 0, 0, 0});
    coreArbiter->setRequestedCores(coreRequest);
    limitedTimeWait([]() -> bool { return numActiveCores == 3; });
    char* str;
    size_t size;
    FILE* newStream = open_memstream(&str, &size);
    setErrorStream(newStream);
    EXPECT_EQ(coreManager->sharedCores.size(), 3U);
    incrementCoreCount();
    limitedTimeWait([]() -> bool { return numActiveCores > 3; });
    EXPECT_TRUE(
        canThreadBeCreatedOnCore(0, coreManager, coreManager->sharedCores[3]));
    fflush(newStream);
    EXPECT_EQ("Attempting to increase number of cores 3 --> 4\n",
              std::string(str));
    free(str);
}
//
TEST_F(ArachneTest, decrementCoreCount) {
    void decrementCoreCount();
    char* str;
    size_t size;
    FILE* newStream = open_memstream(&str, &size);
    setErrorStream(newStream);
    DefaultCoreManager* coreManager =
        reinterpret_cast<DefaultCoreManager*>(getCoreManagerForTest());
    EXPECT_TRUE(
        canThreadBeCreatedOnCore(0, coreManager, coreManager->sharedCores[2]));
    decrementCoreCount();
    limitedTimeWait(
        []() -> bool { return numActiveCores < 3 && !coreChangeActive; });
    EXPECT_EQ(coreManager->sharedCores.size(), 2U);
    decrementCoreCount();
    limitedTimeWait(
        []() -> bool { return numActiveCores < 2 && !coreChangeActive; });
    EXPECT_EQ(coreManager->sharedCores.size(), 1U);
    fflush(newStream);
    EXPECT_EQ(
        "Attempting to decrease number of cores 3 --> 2\n"
        "Attempting to decrease number of cores 2 --> 1\n",
        std::string(str));
    free(str);
}

void
doNothing() {}

// This thread sits on a core exclusively and exits when shouldExit is set.
std::atomic<int> shouldExit(0);
void
exclusiveThread() {
    while (!shouldExit.load())
        ;
}
// Since the functions are paired, this also serves as the test for
// makeSharedOnCore.
TEST_F(ArachneTest, createExclusiveThread) {
    DefaultCoreManager* coreManager =
        reinterpret_cast<DefaultCoreManager*>(getCoreManagerForTest());
    createThreadWithClass(DefaultCoreManager::EXCLUSIVE, exclusiveThread);
    limitedTimeWait([&coreManager]() -> bool {
        return Arachne::occupiedAndCount[coreManager->exclusiveCores[0]]
                   ->load()
                   .numOccupied == 56;
    });

    // Check that the core is no longer available in the default scheduling
    // class.
    EXPECT_FALSE(canThreadBeCreatedOnCore(0, coreManager,
                                          coreManager->exclusiveCores[0]));
    shouldExit.store(1);
}

// Since idleCore and unidleCore are paired, they are tested together.
TEST_F(ArachneTest, idleAndUnidle) {
    idleCore(0);
    idleCore(2);
    limitedTimeWait([]() -> bool { return Arachne::isIdledArray[2]; });
    EXPECT_TRUE(Arachne::isIdledArray[0]);
    EXPECT_FALSE(Arachne::isIdledArray[1]);
    EXPECT_TRUE(Arachne::isIdledArray[2]);
    unidleCore(0);
    unidleCore(2);
    limitedTimeWait([]() -> bool { return !Arachne::isIdledArray[2]; });
    EXPECT_FALSE(Arachne::isIdledArray[0]);
    EXPECT_FALSE(Arachne::isIdledArray[1]);
    EXPECT_FALSE(Arachne::isIdledArray[2]);
}

TEST_F(ArachneTest, nestedDispatchDetector) {
    {
        NestedDispatchDetector detector1;
        EXPECT_TRUE(NestedDispatchDetector::dispatchRunning);
    }
    EXPECT_FALSE(NestedDispatchDetector::dispatchRunning);
}

}  // namespace Arachne
