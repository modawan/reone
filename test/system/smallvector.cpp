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
            if (ctor) {
                ++(*ctor);
            }
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

    // Reallocate forces a copy
    v.push_back(s);
    EXPECT_EQ(ctor, 6);
    EXPECT_EQ(dtor, 3);

    v.resize(1);
    EXPECT_EQ(ctor, 6);
    EXPECT_EQ(dtor, 5);

    v.resize(0);
    EXPECT_EQ(ctor, 6);
    EXPECT_EQ(dtor, 6);

    // Check dtor after resize
    v.resize(1);
    EXPECT_EQ(ctor, 6);
    EXPECT_EQ(dtor, 6);

    v.back().dtor = &dtor;

    v.resize(0);
    EXPECT_EQ(ctor, 6);
    EXPECT_EQ(dtor, 7);

    // Check emplace back
    EXPECT_EQ(v.emplace_back(42).value, 42);
    EXPECT_EQ(v.back().value, 42);

    // Check erase
    v.emplace_back(43);
    v[0].dtor = &dtor;
    v[1].dtor = &dtor;
    EXPECT_EQ(dtor, 7);

    // Erase forces dtor of the erased element, as well as dtor and ctor for
    // subsequent elements due to memmove.
    v.erase(&v[0]);
    EXPECT_EQ(dtor, 9);
    v.erase(&v[0]);
    EXPECT_EQ(dtor, 10);
    EXPECT_TRUE(v.empty());
}

TEST(SmallVector, dtor) {
    SmallVector<std::string, 2> v;
    v.push_back("1");
    v.push_back("2");
    v.push_back("3");
}

TEST(SmallVector, move) {
    struct S {
        int *move {nullptr};
        int *dtor {nullptr};

        S() = default;

        S(int *m, int *d) :
            move(m), dtor(d) {}

        S(S &&other) {
            S &o = const_cast<S &>(other);
            move = o.move;
            dtor = o.dtor;
            if (move) {
                ++(*move);
            }
        }

        S(const S &other) {
            S &o = const_cast<S &>(other);
            move = o.move;
            dtor = o.dtor;
        }

        ~S() {
            if (dtor) {
                ++(*dtor);
            }
        }
    };

    int move = 0;
    int dtor = 0;

    SmallVector<S, 2> v;
    v.emplace_back(&move, &dtor);
    v.emplace_back(&move, &dtor);
    EXPECT_EQ(move, 0);

    // Reallocation moves elements from co-allocated storage to heap.
    v.emplace_back(&move, &dtor);
    EXPECT_EQ(move, 2);
    EXPECT_EQ(dtor, 2);

    // Erase calls dtor of the erased element, and moves all elements that
    // follow it.
    v.erase(&v[0]);
    EXPECT_EQ(move, 4);
    EXPECT_EQ(dtor, 5);

    // Insert moves all elements following the inserted element.
    S ins;
    v.insert(&v[0], ins);
    EXPECT_EQ(move, 6);
    EXPECT_EQ(dtor, 7);
}

TEST(SmallVector, move_assign) {
    SmallVector<SmallVector<int, 2>, 2> v;

    SmallVector<int, 2> v0;
    v0.push_back(1);
    v0.push_back(2);
    v.emplace_back(std::move(v0));

    SmallVector<int, 2> v1;
    v1.push_back(3);
    v1.push_back(4);
    v1.push_back(5);
    v.emplace_back(std::move(v1));

    SmallVector<int, 2> v2;
    v2.push_back(6);
    v.emplace_back(std::move(v2));

    // Move operation should take content of a SmallVector, and reset it.
    EXPECT_EQ(v0.size(), 0);
    EXPECT_EQ(v1.size(), 0);
    EXPECT_EQ(v1.size(), 0);

    // Check that "moved" SmallVectors are still functional.
    v0.push_back(7);
    v1.push_back(8);
    v2.push_back(9);
    v2.push_back(10);
    v2.push_back(11);

    EXPECT_EQ(v[0][0], 1);
    EXPECT_EQ(v[0][1], 2);
    EXPECT_EQ(v[1][0], 3);
    EXPECT_EQ(v[1][1], 4);
    EXPECT_EQ(v[1][2], 5);
    EXPECT_EQ(v[2][0], 6);

    EXPECT_EQ(v0[0], 7);
    EXPECT_EQ(v1[0], 8);
    EXPECT_EQ(v2[0], 9);
    EXPECT_EQ(v2[1], 10);
    EXPECT_EQ(v2[2], 11);
}

TEST(SmallVector, string) {
    SmallVector<std::string, 2> v;
    v.push_back("a");
    v.push_back("b");
    v.push_back("c");

    EXPECT_EQ(v[0], "a");
    EXPECT_EQ(v[1], "b");
    EXPECT_EQ(v[2], "c");
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
