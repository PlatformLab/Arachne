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

void blocker() {
    Arachne::block();
}

TEST(BlockSignalTest, Signal) {
    Arachne::ThreadContext tempContext;
    tempContext.wakeupTimeInCycles = ~0L;
    Arachne::signal(&tempContext);
    EXPECT_EQ(0, tempContext.wakeupTimeInCycles);
}

TEST(BlockSignalTest, AwakenBlockedThread) {
    Arachne::numCores = 2;
    Arachne::threadInit();
    Arachne::createThread(0, blocker);
    EXPECT_EQ(1, Arachne::occupiedAndCount[0].load().numOccupied);
    EXPECT_EQ(1, Arachne::occupiedAndCount[0].load().occupied);

    Arachne::signal(Arachne::activeLists[0]);
    while (Arachne::occupiedAndCount[0].load().numOccupied == 1);
    EXPECT_EQ(0, Arachne::occupiedAndCount[0].load().occupied);
}
