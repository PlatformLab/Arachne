/* Copyright (c) 2017 Stanford University
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

#include "CArachneWrapper.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

enum TestReturn { TEST_PASS, TEST_FAIL };
typedef enum TestReturn TestReturn;

/*
 * A wrapper to run a command with system() function
 */
static void
testSystem(const char* cmd) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-result"
    system(cmd);
#pragma GCC diagnostic pop
}

/**
 * Functions below are used to test thread creation
 */
static void*
funcCreateTest(void* arg) {
    /*
     * We sleep 1ms here so that threads joining this thread
     * must contend for the join lock with high probability.
     */
    usleep(1000);
    *(unsigned*)arg = 0xDEADBEEF;
    return NULL;
}

static TestReturn
createThreadTest(void) {
    CArachneThreadId threadId;
    unsigned arg = 0;
    TestReturn retval = TEST_PASS;

    cArachneInit(NULL, NULL);
    int ret = cArachneCreateThread(&threadId, funcCreateTest, (void*)&arg);
    assert(ret == 0);

    cArachneJoin(&threadId);
    if (arg != 0xDEADBEEF) {
        retval = TEST_FAIL;
    }

    return retval;
}

/**
 * Structures are used to collect all test cases
 */
typedef TestReturn (*TEST_FUNC)(void);
struct testcase {
    const char* description;
    TEST_FUNC function;
};

struct testcase testcases[] = {{"createThread", createThreadTest},
                               {NULL, NULL}};

/*
 * Wrapper for main function arguments
 */
struct ArgList {
    int argc;
    char** argv;
};
typedef struct ArgList ArgList;

/**
 * Main entrance for all tests
 */
static void*
mainTestLoop(void* arg) {
    int exitcode = 0;
    int id = 0, numCases = 0;

    for (numCases = 0; testcases[numCases].description; numCases++) {
        /* Counting the number of cases*/
    }

    printf("Testing cases 1..%d\n", numCases);

    for (id = 0; id < numCases; id++) {
        fflush(stdout);
        TestReturn ret = testcases[id].function();
        if (ret == TEST_PASS) {
            fprintf(stdout, "[OK] %d - %s\n", id + 1,
                    testcases[id].description);
        } else {
            fprintf(stdout, "[ERROR] %d - %s\n", id + 1,
                    testcases[id].description);
            exitcode += 1;
        }
        fflush(stdout);
    }

    if (exitcode != 0) {
        fprintf(stderr, "Non-zero return code for %d / %d tests \n", exitcode,
                numCases);
    }
    cArachneShutDown();
    return NULL;
}

/*
 * Create main thread
 */
int
main(int argc, char** argv) {
    CArachneThreadId threadId;
    int retval = 0;
    ArgList mainArgs;

    mainArgs.argc = argc;
    mainArgs.argv = argv;

    testSystem("../CoreArbiter/bin/coreArbiterServer > /dev/null 2>&1 &");

    /* Wait at most 10 sec for Arachne to initialize */
    for (int i = 0; i < 1000; ++i) {
        retval = cArachneInit(&argc, (const char**)argv);
        if (retval == 0) {
            break;
        } else {
            usleep(10000);  // Wait 10 ms before retry
        }
    }

    if (retval == -1) {
        fprintf(stderr, "Failed to initialize Arachne!\n");
        return retval;
    }

    retval = cArachneCreateThread(&threadId, mainTestLoop, (void*)&mainArgs);
    if (retval == -1) {
        fprintf(stderr, "Failed to create main Arachne thread!\n");
        return retval;
    }

    cArachneWaitForTermination();

    testSystem("kill $(pidof coreArbiterServer)");
    return retval;
}
