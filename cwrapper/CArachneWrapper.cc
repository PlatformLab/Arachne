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

#include <stdio.h>

#include "Arachne.h"
#include "CArachneWrapper.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * This function is the wrapper for Arachne::init
 */
void
cArachneInit(int* argcp, const char** argv) {
    Arachne::init(argcp, argv);
}

/**
 * This function is the wrapper for Arachne::shutDown
 */
void
cArachneShutDown() {
    Arachne::shutDown();
}

/**
 * This function is the wrapper for Arachne::waitForTermination
 */
void
cArachneWaitForTermination() {
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
cArachneCreateThread(CArachneThreadId* id, void* (*startRoutine)(void*),
                     void* arg) {
    Arachne::ThreadId tid = Arachne::createThread(startRoutine, arg);
    if (tid == Arachne::NullThread) {
        return -1;
    }

    id->context = reinterpret_cast<CArachneThreadContext*>(tid.context);
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
cArachneJoin(CArachneThreadId* id) {
    Arachne::ThreadId tid(
        reinterpret_cast<Arachne::ThreadContext*>(id->context), id->generation);
    Arachne::join(tid);
}

#ifdef __cplusplus
}
#endif
