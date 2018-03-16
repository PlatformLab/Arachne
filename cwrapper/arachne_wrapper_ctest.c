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

#include "arachne_wrapper.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

enum test_return { TEST_PASS, TEST_FAIL };
typedef enum test_return test_return;

/*
 * A wrapper to run a command with system() function
 */
static void
ctest_system(const char* cmd) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-result"
    system(cmd);
#pragma GCC diagnostic pop
}

/**
 * Functions below are used to test thread creation
 */
static void*
func_create_test(void* arg) {
    /*
     * We sleep 1ms here so that threads joining this thread
     * must contend for the join lock with high probability.
     */
    usleep(1000);
    *(unsigned*)arg = 0xDEADBEEF;
    return NULL;
}

static test_return
create_thread_test(void) {
    arachne_thread_id tid;
    unsigned arg = 0;
    test_return retval = TEST_PASS;

    arachne_init(NULL, NULL);
    int ret = arachne_thread_create(&tid, func_create_test, (void*)&arg);
    assert(ret == 0);

    arachne_thread_join(&tid);
    if (arg != 0xDEADBEEF) {
        retval = TEST_FAIL;
    }

    return retval;
}

/**
 * Structures are used to collect all test cases
 */
typedef test_return (*TEST_FUNC)(void);
struct testcase {
    const char* description;
    TEST_FUNC function;
};

struct testcase testcases[] = {{"createThread", create_thread_test},
                               {NULL, NULL}};

/*
 * Wrapper for main function arguments
 */
struct arg_list {
    int argc;
    char** argv;
};
typedef struct arg_list arg_list;

/**
 * Main entrance for all tests
 */
static void*
main_test_loop(void* arg) {
    int exitcode = 0;
    int id = 0, num_cases = 0;

    for (num_cases = 0; testcases[num_cases].description; num_cases++) {
        /* Counting the number of cases*/
    }

    printf("Testing cases 1..%d\n", num_cases);

    for (id = 0; id < num_cases; id++) {
        fflush(stdout);
        test_return ret = testcases[id].function();
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
                num_cases);
    }
    arachne_shutdown();
    return NULL;
}

/*
 * Create main thread
 */
int
main(int argc, char** argv) {
    arachne_thread_id tid;
    int retval = 0;
    arg_list main_args;

    main_args.argc = argc;
    main_args.argv = argv;

    ctest_system("../CoreArbiter/bin/coreArbiterServer > /dev/null 2>&1 &");

    /* Wait at most 10 sec for Arachne to initialize */
    for (int i = 0; i < 1000; ++i) {
        retval = arachne_init(&argc, (const char**)argv);
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

    retval = arachne_thread_create(&tid, main_test_loop, (void*)&main_args);
    if (retval == -1) {
        fprintf(stderr, "Failed to create main Arachne thread!\n");
        return retval;
    }

    arachne_wait_termination();

    ctest_system("kill $(pidof coreArbiterServer)");
    return retval;
}
