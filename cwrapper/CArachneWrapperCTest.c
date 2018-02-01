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
 * Start a CoreArbiter server
 * Return 0 on success, return -1 on error.
 */
static int
startCoreArbiter(void) {
    char cmd[] = "../CoreArbiter/bin/coreArbiterServer > /dev/null 2>&1 &";
    char advisoryPath[] = "/tmp/coreArbiterAdvisoryLock";
    int retval = 0;
    char chr = 0;

    system(cmd);
    int readFd = open(advisoryPath, O_RDONLY);
    if (readFd < 0) {
        fprintf(stderr, "Failed to open advisoryPath %s: %s \n", advisoryPath,
                strerror(errno));
        return -1;
    }

    /* Check server started or not, wait at most 1 sec */
    ssize_t ret;
    for (int i = 0; i < 1000; ++i) {
        ret = read(readFd, &chr, 1);
        if (ret > 0) {
            break;
        } else {
            usleep(1000);
        }
    }

    if ((ret == 1) && (chr == 's')) {
        retval = 0;
    } else {
        if (ret < 0) {
            fprintf(stderr, "Failed to read advisoryPath %s: %s \n",
                    advisoryPath, strerror(errno));
        } else {
            fprintf(stderr, "Timed out waiting for server start \n");
        }
        retval = -1;
    }

    close(readFd);
    return retval;
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

    retval = startCoreArbiter();
    if (retval == -1) {
        fprintf(stderr, "Faild to start CoreArbiter server! \n");
        exit(1);
    }

    cArachneInit(&argc, (const char**)argv);
    retval = cArachneCreateThread(&threadId, mainTestLoop, (void*)&mainArgs);
    if (retval == -1) {
        fprintf(stderr, "Failed to create main Arachne thread!\n");
    } else {
        cArachneWaitForTermination();
    }

    system("kill $(pidof coreArbiterServer)");
    return retval;
}
