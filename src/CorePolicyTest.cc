/* Copyright (c) 2015-2018 Stanford University
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

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#define private public
#include "CorePolicy.h"
#undef private

namespace Arachne {

using ::testing::Eq;
using ::testing::Not;

TEST(CorePolicyTest, CoreList_addRemove) {
    CorePolicy::CoreList list(8);
    EXPECT_THAT(list.size(), Eq(0U));
    list.add(1);
    EXPECT_THAT(list.size(), Eq(1U));
    EXPECT_THAT(list[0], Eq(1));
    list.add(8);
    EXPECT_THAT(list.size(), Eq(2U));
    EXPECT_THAT(list[1], Eq(8));
    list.remove(0);
    EXPECT_THAT(list.size(), Eq(1U));
    EXPECT_THAT(list[0], Eq(8));
}
TEST(CorePolicyTest, CoreList_find) {
    CorePolicy::CoreList list(8);
    list.add(1);
    list.add(2);
    EXPECT_THAT(list.find(2), Eq(1));
    EXPECT_THAT(list.find(1), Eq(0));
}

TEST(CorePolicyTest, CoreList_copy) {
    CorePolicy::CoreList list(8, /*mustFree=*/true);
    list.add(1);
    list.add(8);
    // Copy with mustFree equal to true.
    CorePolicy::CoreList copy(list);
    EXPECT_THAT(copy.capacity, Eq(list.capacity));
    EXPECT_THAT(copy.mustFree, Eq(list.mustFree));
    EXPECT_THAT(copy.size(), Eq(list.size()));
    EXPECT_THAT(copy[0], Eq(list[0]));
    EXPECT_THAT(copy[1], Eq(list[1]));
    EXPECT_THAT(copy.cores, Not(Eq(list.cores)));

    // Copy with mustFree equal to false.
    CorePolicy::CoreList list2(8, /*mustFree=*/false);
    CorePolicy::CoreList copy2(list2);
    EXPECT_THAT(copy2.capacity, Eq(list2.capacity));
    EXPECT_THAT(copy2.mustFree, Eq(list2.mustFree));
    EXPECT_THAT(copy2.size(), Eq(list2.size()));
    EXPECT_THAT(copy2.cores, Eq(list2.cores));
}
}  // namespace Arachne
