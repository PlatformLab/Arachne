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
#include "Condition.h"

static Arachne::SpinLock mutex;
static Arachne::ConditionVariable cv;
static volatile int awaited;

static void waiter() {
    mutex.lock();
    while (!awaited)
        cv.wait(mutex);
    awaited--;
    mutex.unlock();
}
TEST(ConditionTest, NotifyOne) {
    Arachne::numCores = 2;
    Arachne::threadInit();
    awaited = 0;
    Arachne::createThread(0, waiter);
    Arachne::createThread(0, waiter);
    EXPECT_EQ(2, Arachne::occupiedAndCount[0].load().numOccupied);
    EXPECT_EQ(3, Arachne::occupiedAndCount[0].load().occupied);
    awaited = 2;
    cv.notifyOne();
    while (awaited == 2);
    EXPECT_EQ(1, awaited);
    cv.notifyOne();
    while (awaited == 1);
    EXPECT_EQ(0, awaited);
}

TEST(ConditionTest, NotifyAll) {
    Arachne::numCores = 2;
    Arachne::threadInit();
    awaited = 0;
    for (int i = 0; i < 10; i++)
        Arachne::createThread(0, waiter);
    awaited = 5;
    cv.notifyAll();
    while (Arachne::occupiedAndCount[0].load().numOccupied > 5);
    EXPECT_EQ(0, awaited);
    awaited = 5;
    cv.notifyAll();
}
