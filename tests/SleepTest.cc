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
#include "Cycles.h"

using PerfUtils::Cycles;

void sleeper() {
    uint64_t before = Cycles::rdtsc();
    Arachne::sleep(1000);
    uint64_t delta = Cycles::toNanoseconds(Cycles::rdtsc() - before);
    EXPECT_LE(1000, delta);
}

static volatile int flag;
void simplesleeper() {
    Arachne::sleep(10000);
    flag = 1;
    while (flag);
}

TEST(SleepTest, MinimumDelay) {
    Arachne::numCores = 2;
    Arachne::threadInit();
    Arachne::createThread(0, sleeper);
}

TEST(SleepTest, WakeupTimeSetAndCleared) {
    Arachne::numCores = 2;
    Arachne::threadInit();
    flag = 0;
    Arachne::createThread(0, simplesleeper);
    while (!flag);
    EXPECT_EQ(0, Arachne::activeLists[0]->wakeupTimeInCycles);
    flag = 0;
}
