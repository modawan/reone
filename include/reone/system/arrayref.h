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

#include "reone/system/smallset.h"
#include "reone/system/smallvector.h"

#include <array>
#include <cassert>
#include <vector>

namespace reone {

/**
 * ArrayRef is a temporary wrapper around a sequence of elements stored in
 * another container. Use it to pass a sequence argument to a function.
 */
template <typename T>
class ArrayRef {
public:
    using const_iterator = const T *;
    using const_reference = const T &;

    ArrayRef() = delete;

    ArrayRef(const ArrayRef<T> &other) :
        _begin(other.begin()), _size(other.size()) {}

    template <std::size_t N>
    ArrayRef(const std::array<T, N> &array) :
        _begin(&array[0]), _size(N) {}

    ArrayRef(const std::vector<T> &vec) :
        _begin(&vec[0]), _size(vec.size()) {}

    ArrayRef(const ISmallVector<T> &vec) :
        _begin(&vec[0]), _size(vec.size()) {}

    ArrayRef(const ISmallSet<T> &set) :
        _begin(&set[0]), _size(set.size()) {}

    ArrayRef(const T *begin, size_t size) :
        _begin(begin), _size(size) {}

    ArrayRef(const T *begin, const T *end) :
        _begin(begin), _size(end - begin) {
        assert(end >= begin);
    }

// Disable -Winit-list-lifetime for ArrayRef. The warning is about the captured
// address (begin) to a temporary. When ArrayRef is used as a function parameter
// and initialized with a temporary, this temporary is guaranteed to live until
// the function returns. However, make sure to not store this ArrayRef anywhere.
#if defined(__GNUC__) && __GNUC__ >= 9
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winit-list-lifetime"
#endif
    ArrayRef(std::initializer_list<T> ilist) :
        _begin(ilist.begin()), _size(ilist.size()) {}
#if defined(__GNUC__) && __GNUC__ >= 9
#pragma GCC diagnostic pop
#endif

    template <int N>
    ArrayRef(T (&array)[N]) :
        _begin(&array[0]), _size(N) {}

    const_iterator begin() const { return _begin; }
    const_iterator end() const { return _begin + _size; }

    size_t size() const { return _size; }
    bool empty() const { return _size == 0; }

    const_reference operator[](size_t i) const {
        assert(i < size() && "ArrayRef<T> out-of-bounds access");
        return _begin[i];
    }

    template <typename U>
    ArrayRef<T> &operator=(U) = delete;

private:
    const T *_begin;
    size_t _size;
};

} // namespace reone
