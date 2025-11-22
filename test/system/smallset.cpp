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

#include "reone/system/smallset.h"

using namespace reone;

TEST(SmallSet, basic) {
    SmallSet<int, 2> v;
    EXPECT_TRUE(v.empty());

    // Check insertion when SmallSet still uses co-allocated storage
    auto it1_true = v.insert(1);
    EXPECT_EQ(it1_true.first, &v[0]);
    EXPECT_TRUE(it1_true.second);

    auto it0_true = v.insert(0);
    EXPECT_EQ(it0_true.first, &v[1]);
    EXPECT_TRUE(it0_true.second);

    // Elements are not sorted yet.
    EXPECT_EQ(1, v[0]);
    EXPECT_EQ(0, v[1]);

    EXPECT_TRUE(v.contains(0));
    EXPECT_TRUE(v.contains(1));
    EXPECT_FALSE(v.contains(2));

    int *baseSmall = v.begin();

    // Insertion of an existing element does nothing.
    auto it0_false = v.insert(0);
    EXPECT_EQ(it0_false.first, &v[1]);
    EXPECT_FALSE(it0_false.second);

    // Insert an element to trigger reallocation.
    auto it2_true = v.insert(2);
    EXPECT_EQ(2, v[2]);
    EXPECT_TRUE(v.contains(0));
    EXPECT_TRUE(v.contains(1));
    EXPECT_TRUE(v.contains(2));

    // Check reallocation
    int *baseHeap = v.begin();
    EXPECT_NE(baseSmall, baseHeap);

    // The underlying array is now sorted.
    EXPECT_EQ(0, v[0]);
    EXPECT_EQ(1, v[1]);
    EXPECT_EQ(2, v[2]);

    // Old iterators are invalidated
    EXPECT_NE(it0_true.first, &v[1]);
    EXPECT_NE(it1_true.first, &v[0]);

    // New iterator is valid until the next insertion
    EXPECT_EQ(it2_true.first, &v[2]);
    EXPECT_TRUE(it2_true.second);

    // Insert a smaller element to trigger memmove
    auto it3_true = v.insert(-1);
    EXPECT_EQ(it3_true.first, &v[0]);
    EXPECT_TRUE(it3_true.second);

    EXPECT_EQ(-1, v[0]);
    EXPECT_EQ(0, v[1]);
    EXPECT_EQ(1, v[2]);
    EXPECT_EQ(2, v[3]);

    EXPECT_TRUE(v.contains(-1));
    EXPECT_TRUE(v.contains(0));
    EXPECT_TRUE(v.contains(1));
    EXPECT_TRUE(v.contains(2));
    EXPECT_FALSE(v.contains(3));
}

TEST(SmallSet, iterators) {
    SmallSet<int, 4> v;
    v.insert(0);
    v.insert(1);

    EXPECT_EQ(0, *v.begin());
    EXPECT_EQ(1, *(v.begin() + 1));
    EXPECT_EQ(v.end(), v.begin() + 2);

    const ISmallSet<int> &vc = v;
    EXPECT_EQ(0, *v.begin());
    EXPECT_EQ(1, *(v.begin() + 1));
    EXPECT_EQ(v.end(), v.begin() + 2);
}

TEST(SmallSet, erase) {
    SmallSet<int, 4> v;
    EXPECT_TRUE(v.empty());

    v.insert(0);
    v.insert(1);
    v.insert(2);
    v.insert(3);

    // Erase the first element.
    v.erase(0);
    EXPECT_EQ(v.size(), 3);
    EXPECT_EQ(1, v[0]);
    EXPECT_EQ(2, v[1]);
    EXPECT_EQ(3, v[2]);

    // Erase from the middle.
    v.erase(2);
    EXPECT_EQ(v.size(), 2);
    EXPECT_EQ(1, v[0]);
    EXPECT_EQ(3, v[1]);

    // Erase the last element.
    v.erase(3);
    EXPECT_EQ(v.size(), 1);
    EXPECT_EQ(1, v[0]);

    // Check that the underlying vector is not sorted.
    v.insert(2);
    v.insert(0);
    v.insert(3);
    EXPECT_EQ(v.size(), 4);
    EXPECT_EQ(1, v[0]);
    EXPECT_EQ(2, v[1]);
    EXPECT_EQ(0, v[2]);
    EXPECT_EQ(3, v[3]);

    // Now test with a heap allocation.
    int *baseSmall = v.begin();

    // The underlying vector is now sorted.
    v.insert(-1);
    EXPECT_NE(v.begin(), baseSmall);
    EXPECT_EQ(v.size(), 5);
    EXPECT_EQ(-1, v[0]);
    EXPECT_EQ(0, v[1]);
    EXPECT_EQ(1, v[2]);
    EXPECT_EQ(2, v[3]);
    EXPECT_EQ(3, v[4]);

    // Erase the first element
    v.erase(-1);
    EXPECT_EQ(v.size(), 4);
    EXPECT_EQ(0, v[0]);
    EXPECT_EQ(1, v[1]);
    EXPECT_EQ(2, v[2]);
    EXPECT_EQ(3, v[3]);

    // Erase from the middle
    v.erase(2);
    EXPECT_EQ(v.size(), 3);
    EXPECT_EQ(0, v[0]);
    EXPECT_EQ(1, v[1]);
    EXPECT_EQ(3, v[2]);

    // Erase from the end
    v.erase(3);
    EXPECT_EQ(v.size(), 2);
    EXPECT_EQ(0, v[0]);
    EXPECT_EQ(1, v[1]);
}

TEST(SmallSet, find) {
    SmallSet<int, 4> v;
    EXPECT_TRUE(v.empty());

    v.insert(0);
    v.insert(2);
    v.insert(1);
    v.insert(3);

    auto it0 = v.find(0);
    EXPECT_EQ(it0, &v[0]);

    auto it1 = v.find(1);
    EXPECT_EQ(it1, &v[2]);

    // Now test with a heap allocation.
    int *baseSmall = v.begin();

    // The underlying vector is now sorted.
    v.insert(-1);
    EXPECT_NE(v.begin(), baseSmall);
    EXPECT_EQ(v.size(), 5);
    EXPECT_EQ(-1, v[0]);
    EXPECT_EQ(0, v[1]);
    EXPECT_EQ(1, v[2]);
    EXPECT_EQ(2, v[3]);
    EXPECT_EQ(3, v[4]);

    it0 = v.find(0);
    EXPECT_EQ(it0, &v[1]);

    it1 = v.find(1);
    EXPECT_EQ(it1, &v[2]);
}

TEST(SmallSet, sort) {
    SmallSet<int, 4> v;
    EXPECT_TRUE(v.empty());

    v.insert(0);
    v.insert(2);
    v.insert(1);
    v.insert(3);

    EXPECT_EQ(0, v[0]);
    EXPECT_EQ(2, v[1]);
    EXPECT_EQ(1, v[2]);
    EXPECT_EQ(3, v[3]);

    v.sort();
    EXPECT_EQ(0, v[0]);
    EXPECT_EQ(1, v[1]);
    EXPECT_EQ(2, v[2]);
    EXPECT_EQ(3, v[3]);

    // Now test with a heap allocation.
    int *baseSmall = v.begin();
    v.insert(-1);
    EXPECT_NE(v.begin(), baseSmall);
    EXPECT_EQ(-1, v[0]);
    EXPECT_EQ(0, v[1]);
    EXPECT_EQ(1, v[2]);
    EXPECT_EQ(2, v[3]);
    EXPECT_EQ(3, v[4]);

    // Sort does nothing for a heap allocation.
    v.sort();
    EXPECT_EQ(-1, v[0]);
    EXPECT_EQ(0, v[1]);
    EXPECT_EQ(1, v[2]);
    EXPECT_EQ(2, v[3]);
    EXPECT_EQ(3, v[4]);
}
