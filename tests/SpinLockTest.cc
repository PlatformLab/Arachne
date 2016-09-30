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
#include "SpinLock.h"

static volatile int flag;
static Arachne::SpinLock mutex;

static void lockTaker() {
    flag = 1;
    mutex.lock();
    mutex.unlock();
    flag = 0;
}

TEST(SpinLockTest, Exclusion) {
    Arachne::numCores = 2;
    Arachne::threadInit();
    flag = 0;
    mutex.lock();
    Arachne::createThread(0, lockTaker);
    while (!flag);
    EXPECT_EQ(1, flag);
    mutex.unlock();
    while (flag);
    EXPECT_EQ(0, flag);
}

TEST(SpinLockTest, TryLock) {
    mutex.lock();
    EXPECT_FALSE(mutex.try_lock());
    mutex.unlock();
    EXPECT_TRUE(mutex.try_lock());
    mutex.unlock();
}
