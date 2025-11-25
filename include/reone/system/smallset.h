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

#pragma once

#include "reone/system/smallvector.h"
#include <cassert>
#include <cstdint>

namespace reone {

/**
 * ISmallSet is an interface for SmallSet - a container similar to std::set,
 * but it uses a SmallVector to store elements co-allocated with the
 * object. When the number of elements exeeds capacity of co-allocated storage,
 * SmallSet makes a heap allocation and copies all elements there.
 *
 * Key differences of SmallSet from std::set:
 *
 *  - SmallSet is not always sorted. When it uses a co-allocated storage, new
 *    unique elements are appended without maintaining sort order.
 *
 *  - The underlying container is a SmallVector, so elements are stored
 *    contiguously in a single memory block.
 *
 *  - Even though a SmallSet can expand to a heap allocation, performance of
 *    insert() and erase() operations is poor for large number of elements.
 *
 * Note that SmallSmall is usually expensive to copy by value, so copy
 * constructor is deleted. Use ISmallSet to pass it by reference, or std::set if
 * copying is required.
 *
 * Note that not all functions of std::set are supported yet.
 */
template <typename T>
class ISmallSet : protected ISmallVector<T> {
public:
    ISmallSet() = delete;
    ISmallSet(const ISmallSet &) = delete;
    ~ISmallSet() = default;

    using value_type = T;
    using reference = T &;
    using const_reference = const T &;
    using iterator = T *;
    using const_iterator = const T *;
    using reverse_iterator = iterator;
    using const_reverse_iterator = const_iterator;

private:
    using Base = ISmallVector<T>;

public:
    iterator begin() { return Base::begin(); }
    iterator end() { return Base::end(); }

    const_iterator begin() const { return Base::begin(); }
    const_iterator end() const { return Base::end(); }

    size_t size() const { return Base::size(); }

    size_t capacity() const { return Base::capacity(); }

    bool empty() const { return Base::empty(); }

    reference operator[](size_t i) { return Base::operator[](i); }

    const_reference operator[](size_t i) const { return Base::operator[](i); }

    reference back() { return Base::back(); }

    const_reference back() const { return Base::back(); }

    void reserve(size_t new_cap) { return Base::reserve(new_cap); }

    void clear() { Base::resize(0); }

    /**
     * Find an element by \p value and return true.
     */
    bool contains(const T &value) {
        if (Base::isSmall()) {
            // Linear search, because co-allocated storage is not sorted.
            auto foundIt = std::find(begin(), end(), value);
            return foundIt != end();
        }
        return std::binary_search(begin(), end(), value);
    }

    /**
     * Find an element by \p value and return an iterator (pointer) to it.
     * Note that insert() and erase() operations may invalidate pointers.
     */
    iterator find(const T &value) {
        if (Base::isSmall()) {
            // Linear search, because co-allocated storage is not sorted.
            return std::find(begin(), end(), value);
        }
        auto lowerIt = std::lower_bound(begin(), end(), value);
        if (*lowerIt != value) {
            return end();
        }
        return lowerIt;
    }

    /**
     * Add a \p value to the set, and return a pair:
     *
     *  - iterator to the the inserted value
     *
     *  - bool: true if a value was actually inserted, or false if it was
     *    already in the set (in which case the iterator points to that
     *    element).
     *
     * This function may reallocate storage and invalidate pointers that are
     * saved elsewhere.
     */
    std::pair<iterator, bool> insert(const T &value) {
        if (Base::isSmall()) {
            auto foundIt = std::find(begin(), end(), value);
            if (foundIt != end()) {
                return {foundIt, false};
            }
            Base::push_back(value);
            if (Base::isSmall()) {
                return {&back(), true};
            }

            // Reallocated to heap - we have to maintain a sorted array from now
            // on.
            std::sort(begin(), end());

            // Find a matching element and return it
            auto lowerIt = std::lower_bound(begin(), end(), value);
            assert((lowerIt != end()) && (*lowerIt == value));
            return {lowerIt, true};
        }

        auto lowerIt = std::lower_bound(begin(), end(), value);
        if (*lowerIt != value) {
            return {Base::insert(lowerIt, value), true};
        }

        return {end(), false};
    }

    /**
     * Remove a \p value from the set. Return 1 when it was removed, or 0 when
     * it was not found.
     */
    size_t erase(const T &value) {
        auto foundIt = find(value);
        if (foundIt == end()) {
            return 0;
        }
        Base::erase(foundIt);
        return 1;
    }

    /**
     * Remove a element pointed by an interator, and return an iterator to the
     * next element (or end() if it was the last one).
     */
    iterator erase(iterator eraseIt) {
        Base::erase(eraseIt);

        // Base::erase is going to shift all elements left, so the "next"
        // element takes place of eraseIt.
        return eraseIt;
    }

    /**
     * Sort elements of the set, unless they are sorted by construction.
     * Note that insert() operation may not keep sorted order.
     */
    void sort() {
        // We always maintain a sorted array for a heap allocation.
        if (!Base::isSmall()) {
            return;
        }
        std::sort(begin(), end());
    }

protected:
    /**
     * Initialize from a SmalllSet: \p begin is a pointer to co-allocated memory
     * of size \p smallCapacity.
     */
    ISmallSet(T *begin, int64_t smallCapacity) :
        Base(begin, smallCapacity) {}
};

template <typename T, size_t Capacity>
class SmallSet : public ISmallSet<T> {
public:
    SmallSet() :
        ISmallSet<T>((T *)_coalloc, Capacity) {}

    SmallSet(const SmallSet<T, Capacity> &) = delete;

private:
    /**
     * Co-allocated storage.
     */
    alignas(T) char _coalloc[sizeof(T) * Capacity];
};

} // namespace reone
