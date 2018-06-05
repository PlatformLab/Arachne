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

#include <errno.h>
#include <stdio.h>

#include "Arachne.h"
#include "CoreArbiter/CoreArbiterClient.h"
#include "DefaultCorePolicy.h"
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
 * This function is the wrapper for Arachne::createThreadWithClass.
 * Under Arachne's default core policy, the following thread classes are
 * available.
 *
 *    Class 0: Normal thread creation
 *    Class 1: Exclusive thread creation
 */
int
arachne_thread_create_with_class(arachne_thread_id* id, void* (*func)(void*),
                                 void* arg, int thread_class) {
    Arachne::ThreadId tid =
        Arachne::createThreadWithClass(thread_class, func, arg);
    if (tid == Arachne::NullThread) {
        return -1;
    }

    id->context = reinterpret_cast<arachne_thread_context*>(tid.context);
    id->generation = tid.generation;
    return 0;
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
    return arachne_thread_create_with_class(id, func, arg, 0);
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
 * This function is used to get thread local variable
 * Arachne::core.id
 */
int
arachne_thread_getid() {
    return Arachne::core.id;
}

/**
 * This function is used to set the maximum utilization threshold and switch
 * to use utilization only to do core load estimation.
 */
void
arachne_set_maxutil(double maxutil) {
    reinterpret_cast<Arachne::DefaultCorePolicy*>(Arachne::getCorePolicy())
        ->getEstimator()
        ->setMaxUtilization(maxutil);
}

/**
 * This function is used to set the load factor threshold and switch to use load
 * factor to do core load estimation
 */
void
arachne_set_loadfactor(double loadfactor) {
    reinterpret_cast<Arachne::DefaultCorePolicy*>(Arachne::getCorePolicy())
        ->getEstimator()
        ->setLoadFactorThreshold(loadfactor);
}

/**
 * This function is used to change the target of the error stream, allowing
 * redirection to an application's log.
 */
void
arachne_set_errorstream(FILE* ptr) {
    Arachne::setErrorStream(ptr);
}

#ifdef __cplusplus
}
#endif
