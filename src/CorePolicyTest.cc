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
#include "PerfUtils/Cycles.h"
#include "gtest/gtest.h"

#define private public
#define protected public
#include "Arachne.h"
#include "CorePolicy.h"
#include "CoreArbiter/CoreArbiterServer.h"
#include "CoreArbiter/CoreArbiterClient.h"
#include "CoreArbiter/MockSyscall.h"
#include "CoreArbiter/Logger.h"
#include "CoreArbiter/ArbiterClientShim.h"

namespace Arachne {

using CoreArbiter::CoreArbiterServer;
using CoreArbiter::CoreArbiterClient;
using CoreArbiter::MockSyscall;

extern bool useCoreArbiter;

extern std::atomic<uint32_t> numActiveCores;
extern volatile uint32_t minNumCores;
extern int* virtualCoreTable;

extern CoreArbiterClient* coreArbiter;

CorePolicy* corePolicyTest;

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
        coreArbiterServer = new CoreArbiterServer(
                std::string(TEST_SOCKET),
                std::string(TEST_MEM),
                {1,2,3,4,5,6,7}, false);
        coreArbiterServerThread = new std::thread([=] {
                coreArbiterServer->startArbitration();
                });

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

::testing::Environment* const testEnvironment = (useCoreArbiter) ?
    ::testing::AddGlobalTestEnvironment(new Environment) : NULL;

struct ArachneTest : public ::testing::Test {
    virtual void SetUp()
    {
        Arachne::minNumCores = 1;
        Arachne::maxNumCores = 3;
        Arachne::disableLoadEstimation = true;
        corePolicyTest = new CorePolicy();
        Arachne::init(corePolicyTest);
        // Artificially wake up all threads for testing purposes
        std::vector<uint32_t> coreRequest({3,0,0,0,0,0,0,0});
        coreArbiter->setRequestedCores(coreRequest);
        limitedTimeWait([]() -> bool { return numActiveCores == 3;});
    }

    virtual void TearDown()
    {
        // Unblock all cores so they can shut down and be joined.
        coreArbiter->setRequestedCores({Arachne::maxNumCores,0,0,0,0,0,0,0});

        shutDown();
        waitForTermination();
    }
};

// Helper function for tests with timing dependencies, so that we wait for a
// finite amount of time in the case of a bug causing an infinite loop.
static void limitedTimeWait(std::function<bool()> condition,
                int numIterations) {
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

TEST_F(ArachneTest, CorePolicy_constructor) {
    CorePolicy* corePolicy = new CorePolicy();
    for (unsigned i = 0; i < std::thread::hardware_concurrency(); i++) {
        EXPECT_EQ(corePolicy->threadClassCoreMap[corePolicy->defaultClass]->map[i], 0);
    }
    delete corePolicy;
}

TEST_F(ArachneTest, CorePolicy_addCore) {
    CorePolicy* corePolicy = new CorePolicy();
    corePolicy->addCore(5);
    EXPECT_EQ(corePolicy->threadClassCoreMap[corePolicy->defaultClass]->map[0], 5);
    corePolicy->addCore(4);
    corePolicy->addCore(7);
    EXPECT_EQ(corePolicy->threadClassCoreMap[corePolicy->defaultClass]->map[0], 5);
    EXPECT_EQ(corePolicy->threadClassCoreMap[corePolicy->defaultClass]->map[1], 4);
    EXPECT_EQ(corePolicy->threadClassCoreMap[corePolicy->defaultClass]->map[2], 7);
    EXPECT_EQ(corePolicy->threadClassCoreMap[corePolicy->defaultClass]->numFilled, 3U);
    delete corePolicy;
}

TEST_F(ArachneTest, CorePolicy_removeCore) {
    CorePolicy* corePolicy = new CorePolicy();
    corePolicy->addCore(5);
    corePolicy->addCore(4);
    corePolicy->addCore(7);
    corePolicy->removeCore(5);
    EXPECT_EQ(corePolicy->threadClassCoreMap[corePolicy->defaultClass]->numFilled, 2U);
    EXPECT_EQ(corePolicy->threadClassCoreMap[corePolicy->defaultClass]->map[0], 7);
    EXPECT_EQ(corePolicy->threadClassCoreMap[corePolicy->defaultClass]->map[1], 4);
    corePolicy->removeCore(4);
    EXPECT_EQ(corePolicy->threadClassCoreMap[corePolicy->defaultClass]->numFilled, 1U);
    EXPECT_EQ(corePolicy->threadClassCoreMap[corePolicy->defaultClass]->map[0], 7);
    corePolicy->removeCore(7);
    EXPECT_EQ(corePolicy->threadClassCoreMap[corePolicy->defaultClass]->numFilled, 0U);
    delete corePolicy;
}

TEST_F(ArachneTest, CorePolicy_getCoreList) {
    CorePolicy* corePolicy = new CorePolicy();
    CoreList* entry = corePolicy->getCoreList(corePolicy->defaultClass);
    corePolicy->addCore(5);
    EXPECT_EQ(entry->map[0], 5);
    corePolicy->addCore(4);
    corePolicy->addCore(7);
    EXPECT_EQ(entry->map[0], 5);
    EXPECT_EQ(entry->map[1], 4);
    EXPECT_EQ(entry->map[2], 7);
    EXPECT_EQ(entry->numFilled, 3U);
    delete corePolicy;
}

TEST_F(ArachneTest, CorePolicy_CoreBlocker) {
    CoreBlocker* coreBlocker = new CoreBlocker();
    EXPECT_NE(Arachne::NullThread,
            createThreadOnCore(corePolicyTest->defaultClass, 0, blockCore, coreBlocker, 0));
    limitedTimeWait([&coreBlocker]() -> bool {return coreBlocker->isSleepingArray[0];});
    EXPECT_TRUE(coreBlocker->isSleepingArray[0]);
    coreBlocker->unblockCore(0);
    limitedTimeWait([&coreBlocker]() -> bool {return !coreBlocker->isSleepingArray[0];});
    EXPECT_FALSE(coreBlocker->isSleepingArray[0]);
}

} // namespace Arachne
