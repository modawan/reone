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

#include <array>
#include <cassert>
#include <vector>

/// ArrayRef is a temporary wrapper around a sequence of elements stored in
/// another container. Use it to pass a sequence parameter to a function
template <typename T>
class ArrayRef {
public:
    using const_iterator = const T *;
    using const_reference = const T &;

    ArrayRef() = default;

    template <std::size_t N>
    ArrayRef(const std::array<T, N> &array) :
        _begin(&array[0]), _end(_begin + N) {}

    ArrayRef(const std::vector<T> &vec) :
        _begin(&vec[0]), _end(_begin + vec.size()) {}

    ArrayRef(const T *begin, size_t size) :
        _begin(begin), _end(begin + size) {}

    ArrayRef(const T *begin, const T *end) :
        _begin(begin), _end(end) {}

    ArrayRef(std::initializer_list<T> ilist) :
        _begin(ilist.begin()), _end(ilist.end()) {}

    template <int N>
    ArrayRef(T (&array)[N]) :
        _begin(&array[0]), _end(_begin + N) {}

    const_iterator begin() const { return _begin; }
    const_iterator end() const { return _end; }

    size_t size() const { return _end - _begin; }
    bool empty() const { return size() == 0; }

    const_reference operator[](size_t i) const {
        assert(i < size() && "ArrayRef<T> out-of-bounds access");
        return _begin[i];
    }

    template <typename U>
    ArrayRef<T> &operator=(U) = delete;

private:
    const T *_begin;
    const T *_end;
};
