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

#include <errno.h>
#include <stdio.h>

#include "Arachne.h"
#include "CoreArbiter/CoreArbiterClient.h"
#include "arachne_wrapper.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * This function is the wrapper for Arachne::init
 *
 * \return
 *      The return value is 0 on success and -1 on error. Errno will be set
 */
int
arachne_init(int* argcp, const char** argv) {
    int retval = 0;

    // Capture the exception threw by Arachne::init
    try {
        Arachne::init(argcp, argv);
    } catch (const CoreArbiter::CoreArbiterClient::ClientException& e) {
        retval = -1;
        errno = ECONNREFUSED;  // Set errno to "Connection refused"
    }
    return retval;
}

/**
 * This function is the wrapper for Arachne::shutDown
 */
void
arachne_shutdown() {
    Arachne::shutDown();
}

/**
 * This function is the wrapper for Arachne::waitForTermination
 */
void
arachne_wait_termination() {
    Arachne::waitForTermination();
}

/**
 * This function is the wrapper for Arachne::createThread.
 * We have changed the interface due to no template support in C.
 *
 * \param id
 *      The pointer to store returned thread id
 * \param startRoutine
 *      The main function for the new thread
 * \param arg
 *      The argument list for startRoutine
 * \return
 *      The return value is 0 on success and store the thread id in *thread.
 *      It returns -1 if there are insufficient resources for creating
 *      a new thread, and the contents of *thread would be undefined.
 */
int
arachne_thread_create(arachne_thread_id* id, void* (*func)(void*), void* arg) {
    Arachne::ThreadId tid = Arachne::createThread(func, arg);
    if (tid == Arachne::NullThread) {
        return -1;
    }

    id->context = reinterpret_cast<arachne_thread_context*>(tid.context);
    id->generation = tid.generation;
    return 0;
}

/**
 * This function is the wrapper for Arachne::join.
 *
 * \param id
 *      The pointer to the id of the thread to join.
 */
void
arachne_thread_join(arachne_thread_id* id) {
    Arachne::ThreadId tid(
        reinterpret_cast<Arachne::ThreadContext*>(id->context), id->generation);
    Arachne::join(tid);
}

/**
 * This function is the wrapper for Arachne::yield.
 */
void
arachne_thread_yield() {
    Arachne::yield();
}

/**
 * This function is the wrapper for Arachne::makeExclusiveOnCore.
 */
bool
arachne_thread_exclusive_core(bool scale_down) {
    return Arachne::makeExclusiveOnCore(scale_down);
}

/**
 * This function is used to get thread local variable Arachne::core.kernelThreadId
 */
int arachne_thread_getid() {
    return Arachne::core.kernelThreadId;
}

#ifdef __cplusplus
}
#endif
