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

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "CoreManager.h"

namespace Arachne {

using ::testing::Eq;

TEST(CoreManagerTest, CoreList_addRemove) {
    CoreList list(8);
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

TEST(CoreManagerTest, CoreList_copy) {
    CoreList list(8, /*mustFree=*/true);
    list.add(1);
    list.add(8);
    CoreList copy(list);
    EXPECT_THAT(copy.capacity, Eq(list.capacity));
    EXPECT_THAT(copy.mustFree, Eq(list.mustFree));
    EXPECT_THAT(copy.size(), Eq(list.size()));
    EXPECT_THAT(copy[0], Eq(list[0]));
    EXPECT_THAT(copy[1], Eq(list[1]));
}

TEST(CoreManagerTest, CoreList_move) {
    CoreList list(8, /*mustFree=*/true);
    list.add(1);
    list.add(8);
    CoreList moveTarget(std::move(list));
    EXPECT_THAT(moveTarget.capacity, Eq(8));
    EXPECT_THAT(moveTarget.mustFree, Eq(true));
    EXPECT_THAT(moveTarget.size(), Eq(2U));
    EXPECT_THAT(moveTarget[0], Eq(1));
    EXPECT_THAT(moveTarget[1], Eq(8));

    EXPECT_THAT(list.capacity, Eq(0));
    EXPECT_THAT(list.mustFree, Eq(false));
    EXPECT_THAT(list.size(), Eq(0U));
    EXPECT_TRUE(list.cores == NULL);
}

}  // namespace Arachne
