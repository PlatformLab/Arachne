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

#include "gtest/gtest.h"
#include "Arachne.h"

static volatile Arachne::ThreadContext* joineeId;
static volatile Arachne::ThreadContext* joinerId;

void joinee() {
    joineeId = Arachne::getThreadId();
}

void joiner() {
    joinerId = Arachne::getThreadId();
    while (!joineeId);
    EXPECT_EQ(true,
            Arachne::join(const_cast<Arachne::ThreadContext*>(joineeId)));
}

void joinee2() {
    joineeId = Arachne::getThreadId();
    Arachne::yield();
    EXPECT_EQ(joinerId, Arachne::running->waiter);
}

// This test verifies that only one thread can join at a time
TEST(JoinTest, DoubleJoin) {
    Arachne::ThreadContext tempContext;
    tempContext.waiter = reinterpret_cast<Arachne::ThreadContext*>(1);;
    EXPECT_EQ(false, Arachne::join(&tempContext));
}

TEST(JoinTest, JoinAfterTermination) {
    Arachne::numCores = 2;
    Arachne::threadInit();

    // Since the joinee does not yield, we know that it terminated before the
    // jointer got a chance to run.
    Arachne::createThread(0, joinee);
    Arachne::createThread(0, joiner);
}

TEST(JoinTest, JoinDuringRun) {
    Arachne::numCores = 2;
    Arachne::threadInit();

    Arachne::createThread(0, joinee2);
    Arachne::createThread(0, joiner);
}
