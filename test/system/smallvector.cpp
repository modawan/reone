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

#include "reone/system/smallvector.h"

using namespace reone;

TEST(SmallVector, basic) {
    SmallVector<int, 2> v;
    EXPECT_TRUE(v.empty());

    v.push_back(0);
    v.push_back(1);

    EXPECT_EQ(0, v[0]);
    EXPECT_EQ(1, v[1]);
    EXPECT_FALSE(v.empty());

    int *baseSmall = v.begin();

    v.push_back(2);
    EXPECT_EQ(2, v[2]);

    // Check realloc when SmallVector exeeds "small" (inline) capacity.
    int *baseHeap = v.begin();
    EXPECT_NE(baseSmall, baseHeap);
}

TEST(SmallVector, ctor_dtor) {
    struct S {
        int *ctor {nullptr};
        int *dtor {nullptr};
        int value {0};

        S() = default;

        S(const S &other) {
            S &o = const_cast<S &>(other);
            ctor = o.ctor;
            dtor = o.dtor;
            ++(*ctor);
        }

        explicit S(int v) :
            value(v) {}

        ~S() {
            if (dtor) {
                ++(*dtor);
            }
        }
    };

    int ctor = 0;
    int dtor = 0;
    S s;
    s.ctor = &ctor;
    s.dtor = &dtor;

    SmallVector<S, 2> v;

    // Check copy ctor on push_back
    v.push_back(s);
    EXPECT_EQ(ctor, 1);
    EXPECT_EQ(dtor, 0);

    // Check dtor on resize to zero
    v.resize(0);
    EXPECT_EQ(ctor, 1);
    EXPECT_EQ(dtor, 1);

    // Check dtor on partial resize
    v.push_back(s);
    EXPECT_EQ(ctor, 2);
    EXPECT_EQ(dtor, 1);

    v.push_back(s);
    EXPECT_EQ(ctor, 3);
    EXPECT_EQ(dtor, 1);

    v.push_back(s);
    EXPECT_EQ(ctor, 4);
    EXPECT_EQ(dtor, 1);

    v.resize(1);
    EXPECT_EQ(ctor, 4);
    EXPECT_EQ(dtor, 3);

    v.resize(0);
    EXPECT_EQ(ctor, 4);
    EXPECT_EQ(dtor, 4);

    // Check dtor after resize
    v.resize(1);
    EXPECT_EQ(ctor, 4);
    EXPECT_EQ(dtor, 4);

    v.back().dtor = &dtor;

    v.resize(0);
    EXPECT_EQ(ctor, 4);
    EXPECT_EQ(dtor, 5);

    // Check emplace back
    EXPECT_EQ(v.emplace_back(42).value, 42);
    EXPECT_EQ(v.back().value, 42);

    // Check erase
    v.emplace_back(43);
    v[0].dtor = &dtor;
    v[1].dtor = &dtor;
    EXPECT_EQ(dtor, 5);

    v.erase(&v[0]);
    EXPECT_EQ(dtor, 6);
    v.erase(&v[0]);
    EXPECT_EQ(dtor, 7);
    EXPECT_TRUE(v.empty());
}

TEST(SmallVector, reserve) {
    SmallVector<int, 4> v;
    v.push_back(0);
    EXPECT_EQ(0, v[0]);
    EXPECT_EQ(1, v.size());
    EXPECT_EQ(4, v.capacity());

    int *baseSmall = v.begin();

    // Check that shrinking reserve does nothing
    v.reserve(0);
    EXPECT_EQ(baseSmall, v.begin());
    EXPECT_EQ(1, v.size());
    EXPECT_EQ(4, v.capacity());

    v.reserve(4);
    EXPECT_EQ(baseSmall, v.begin());
    EXPECT_EQ(1, v.size());
    EXPECT_EQ(4, v.capacity());

    // Check heap allocation is exact for reserve() and resize().
    v.reserve(5);
    EXPECT_NE(baseSmall, v.begin());
    EXPECT_EQ(1, v.size());
    EXPECT_EQ(5, v.capacity());

    v.resize(6);
    EXPECT_NE(baseSmall, v.begin());
    EXPECT_EQ(6, v.size());
    EXPECT_EQ(6, v.capacity());

    // Check that heap allocation grows with 1.5 factor for push_back() and
    // emplace_back().
    v.push_back(42);
    EXPECT_NE(baseSmall, v.begin());
    EXPECT_EQ(7, v.size());
    EXPECT_EQ(9, v.capacity());

    v.push_back(44);
    v.push_back(45);
    EXPECT_NE(baseSmall, v.begin());
    EXPECT_EQ(9, v.size());
    EXPECT_EQ(9, v.capacity());

    v.emplace_back(46);
    EXPECT_NE(baseSmall, v.begin());
    EXPECT_EQ(10, v.size());
    EXPECT_EQ(13, v.capacity());
}

TEST(SmallVector, iterators) {
    SmallVector<int, 4> v;
    v.push_back(0);
    v.push_back(1);

    EXPECT_EQ(0, *v.begin());
    EXPECT_EQ(1, *(v.begin() + 1));
    EXPECT_EQ(v.end(), v.begin() + 2);

    const ISmallVector<int> &vc = v;
    EXPECT_EQ(0, *v.begin());
    EXPECT_EQ(1, *(v.begin() + 1));
    EXPECT_EQ(v.end(), v.begin() + 2);
}

TEST(SmallVector, insert) {
    SmallVector<int, 4> v;
    v.push_back(0);
    v.push_back(1);
    EXPECT_EQ(0, v[0]);
    EXPECT_EQ(1, v[1]);

    int *baseSmall = v.begin();

    v.insert(&v[1], 2);

    EXPECT_EQ(0, v[0]);
    EXPECT_EQ(2, v[1]);
    EXPECT_EQ(1, v[2]);

    // No reallocation yet
    EXPECT_EQ(baseSmall, v.begin());

    // Append
    v.insert(v.end(), 3);

    EXPECT_EQ(0, v[0]);
    EXPECT_EQ(2, v[1]);
    EXPECT_EQ(1, v[2]);
    EXPECT_EQ(3, v[3]);

    // Insert and cause a reallocation
    v.insert(&v[2], 4);
    EXPECT_EQ(0, v[0]);
    EXPECT_EQ(2, v[1]);
    EXPECT_EQ(4, v[2]);
    EXPECT_EQ(1, v[3]);
    EXPECT_EQ(3, v[4]);
    EXPECT_NE(baseSmall, v.begin());

    v.insert(&v[2], 5);
    EXPECT_EQ(0, v[0]);
    EXPECT_EQ(2, v[1]);
    EXPECT_EQ(5, v[2]);
    EXPECT_EQ(4, v[3]);
    EXPECT_EQ(1, v[4]);
    EXPECT_EQ(3, v[5]);
}

TEST(SmallVector, front_back) {
    SmallVector<int, 4> v;
    v.push_back(0);
    v.push_back(1);

    EXPECT_EQ(0, v.front());
    EXPECT_EQ(1, v.back());
}

TEST(SmallVector, erase) {
    SmallVector<int, 4> v;
    v.push_back(0);
    v.push_back(1);
    v.push_back(2);
    v.push_back(3);

    // Erase with a co-allocated storage.
    v.erase(&v[2]);
    EXPECT_EQ(0, v[0]);
    EXPECT_EQ(1, v[1]);
    EXPECT_EQ(3, v[2]);

    // Reallocate to heap.
    v.push_back(4);
    v.push_back(5);

    EXPECT_EQ(0, v[0]);
    EXPECT_EQ(1, v[1]);
    EXPECT_EQ(3, v[2]);
    EXPECT_EQ(4, v[3]);
    EXPECT_EQ(5, v[4]);

    // Erase with a heap storage.
    v.erase(&v[2]);
    EXPECT_EQ(0, v[0]);
    EXPECT_EQ(1, v[1]);
    EXPECT_EQ(4, v[2]);
    EXPECT_EQ(5, v[3]);
}
