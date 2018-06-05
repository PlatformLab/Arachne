/* Copyright (c) 2017-2018 Stanford University
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
#include "CoreArbiter/ArbiterClientShim.h"
#include "CoreArbiter/CoreArbiterClient.h"
#include "CoreArbiter/CoreArbiterServer.h"
#include "CoreArbiter/Logger.h"
#include "CoreArbiter/MockSyscall.h"
#include "arachne_wrapper.h"

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

extern std::atomic<uint32_t> numActiveCores;
extern volatile uint32_t minNumCores;
extern int* virtualCoreTable;

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
        ::arachne_init(NULL, NULL);
        // Artificially wake up all threads for testing purposes
        std::vector<uint32_t> coreRequest({3, 0, 0, 0, 0, 0, 0, 0});
        coreArbiter->setRequestedCores(coreRequest);
        limitedTimeWait([]() -> bool { return numActiveCores == 3; });
    }

    virtual void TearDown() {
        // Unblock all cores so they can shut down and be joined.
        coreArbiter->setRequestedCores(
            {Arachne::maxNumCores, 0, 0, 0, 0, 0, 0, 0});

        ::arachne_shutdown();
        ::arachne_wait_termination();
    }
};

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

// Functions below are used to test thread creation
static void*
funcCreateTest(void* arg) {
    // We sleep 1ms here so that threads joining this thread
    // must contend for the join lock with high probability.
    usleep(1000);
    *reinterpret_cast<unsigned*>(arg) = 0xDEADBEEF;
    return NULL;
}

TEST_F(ArachneTest, CWrapper_createThread) {
    arachne_thread_id threadId;
    unsigned arg = 0;
    int ret = ::arachne_thread_create(&threadId, funcCreateTest,
                                      reinterpret_cast<void*>(&arg));
    EXPECT_EQ(0, ret);

    ::arachne_thread_join(&threadId);
    EXPECT_EQ(0xDEADBEEF, arg);
}

}  // namespace Arachne
