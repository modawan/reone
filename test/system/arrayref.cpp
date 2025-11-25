/*
 * Copyright (c) 2025 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <gtest/gtest.h>

#include "reone/system/arrayref.h"

using namespace reone;

void check(ArrayRef<int> a, int *begin, int *end) {
    EXPECT_EQ(a.size(), 2);
    EXPECT_FALSE(a.empty());

    EXPECT_EQ(a[0], 1);
    EXPECT_EQ(a[1], 2);
    EXPECT_EQ(a.begin(), begin);
    EXPECT_EQ(a.end(), end);
}

void checkTemporary(ArrayRef<int> a) {
    EXPECT_EQ(a.size(), 2);
    EXPECT_FALSE(a.empty());

    EXPECT_EQ(a[0], 1);
    EXPECT_EQ(a[1], 2);
}

TEST(ArrayRef, std_array) {
    std::array<int, 2> arr {1, 2};
    check(arr, &*arr.begin(), &*arr.end());
}

TEST(ArrayRef, std_vector) {
    std::vector<int> vec {1, 2};
    check(vec, &vec[0], &vec[vec.size()]);
}

TEST(ArrayRef, smallvector) {
    SmallVector<int, 2> vec;
    vec.push_back(1);
    vec.push_back(2);
    check(vec, vec.begin(), vec.end());
}

TEST(ArrayRef, smallset) {
    SmallSet<int, 2> set;
    set.insert(1);
    set.insert(2);
    check(set, set.begin(), set.end());
}

TEST(ArrayRef, begin_end) {
    SmallVector<int, 2> vec;
    vec.push_back(1);
    vec.push_back(2);
    check(ArrayRef(vec.begin(), vec.end()), vec.begin(), vec.end());
}

TEST(ArrayRef, ilist) {
    checkTemporary({1, 2});
}

TEST(ArrayRef, array) {
    int arr[] = {1, 2};
    check(arr, arr, arr + 2);
}
