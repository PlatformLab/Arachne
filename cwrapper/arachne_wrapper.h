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

#ifndef CARACHNEWRAPPER_H_
#define CARACHNEWRAPPER_H_

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \addtogroup api Arachne C Wrapper
 * In order to use Arachne in applications written in C, we provide C wrappers
 * for Arachne public API functions
 * @{
 */
/**
 * This structure is the C wrapper for Arachne::ThreadContext
 * and can only be used as pointers.
 */
struct arachne_thread_context;
typedef struct arachne_thread_context arachne_thread_context;
/**
 * This structure is the C wrapper for Arachne::ThreadId.
 */
struct arachne_thread_id {
    arachne_thread_context* context;
    uint32_t generation;
};
typedef struct arachne_thread_id arachne_thread_id;

int arachne_init(int* argcp, const char** argv);
void arachne_shutdown();
void arachne_wait_termination();
int arachne_thread_create(arachne_thread_id* id, void* (*func)(void*),
                          void* arg);
void arachne_thread_join(arachne_thread_id* id);
void arachne_thread_yield();
int arachne_thread_getid();
int arachne_thread_create_with_class(arachne_thread_id* id,
                                     void* (*func)(void*), void* arg,
                                     int thread_class);
void arachne_set_maxutil(double maxutil);
void arachne_set_loadfactor(double loadfactor);
void arachne_set_errorstream(FILE* ptr);
#ifdef __cplusplus
}
#endif

#endif  // CARACHNEWRAPPER_H_
