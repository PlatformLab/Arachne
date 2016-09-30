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


volatile int keepYielding;
static volatile int flag;
static void yielder() {
    while (keepYielding)
        Arachne::yield();
}

static void setFlag() {
    flag = 1;
}

static void bitSetter(int index) {
    while (keepYielding) {
        flag |= (1 << index);
        Arachne::yield();
    }
}

TEST(YieldTest, SecondThreadGotControl) {
    Arachne::numCores = 2;
    Arachne::threadInit();
    keepYielding = true;
    Arachne::createThread(0, yielder);

    flag = 0;
    Arachne::createThread(0, setFlag);
    while (Arachne::occupiedAndCount[0].load().numOccupied > 1);
    EXPECT_EQ(1, flag);
    flag = 0;
    keepYielding = false;
}

TEST(YieldTest, AllThreadsRan) {
    Arachne::numCores = 2;
    Arachne::threadInit();
    keepYielding = true;
    flag = 0;

    Arachne::createThread(0, bitSetter, 0);
    Arachne::createThread(0, bitSetter, 1);
    Arachne::createThread(0, bitSetter, 2);
    usleep(1);

    keepYielding = false;
    EXPECT_EQ(7, flag);
}
