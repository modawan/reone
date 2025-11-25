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

#include <cassert>
#include <cstdint>

namespace reone {

/**
 * ISmallVector is an interface for SmallVector - a container similar to
 * std::vector, but it has a small number of elements co-allocated within the
 * object itself. When the number of elements of a SmallVector exceeds capacity
 * of a co-allocated memory, it makes a heap allocation, copies all elements,
 * and can continue to grow there similarly to a std::vector.
 *
 * SmallVector has an advantage over std::vector when the number of elements is
 * generally small, but may be larger than expected in some edge cases.
 *
 * For example, number of actors in a combat encounter is generally small:
 *
 *     SmallVector<Creature *, 16> actors;
 *     collectOpponents(actors);
 *     attackAll(actors);
 *
 * Here, when number of opponents is 16 or less (typical), there is no heap
 * allocation. Otherwise SmallVector functions like a std::vector.
 *
 * However, keep in mind that co-allocated storage increases SmallVector's
 * size, so use a reasonable number:
 *
 *    // 8 KB on the stack
 *    SmallVector<Creature *, 1024> actors;
 *
 * Use ISmallVector interface to pass SmallVector to a function in order to
 * avoid specifying the number of co-allocated elements:
 *
 *    void collectOpponents(ISmallVector<Creature *> &actors) {
 *        actors.push_back(...);
 *    }
 *
 * Note that SmallVector is unually expensive to copy by value, so copy
 * constructor is deleted. Use ISmallVector to pass it by reference, or
 * std::vector if copying is required.
 *
 * Note that not all functions of std::vector are supported yet.
 */
template <typename T>
class ISmallVector {
public:
    ISmallVector() = delete;
    ISmallVector(const ISmallVector &) = delete;

    ~ISmallVector() {
        destroyRange(begin(), end());
        if (!isSmall()) {
            free(_begin);
        }
    }

    using value_type = T;
    using reference = T &;
    using const_reference = const T &;
    using iterator = T *;
    using const_iterator = const T *;
    using reverse_iterator = iterator;
    using const_reverse_iterator = const_iterator;

    iterator begin() { return _begin; }
    iterator end() { return _begin + _size; }

    const_iterator begin() const { return _begin; }
    const_iterator end() const { return _begin + _size; }

    size_t size() const { return _size; }

    size_t capacity() const {
        if (isSmall()) {
            assert(_capacity < 0 && "capacity should be negated for co-allocated storage");
            return -_capacity;
        }
        return _capacity;
    }

    bool empty() const { return size() == 0; }

    reference operator[](size_t i) {
        assert(i < _size && "out-of-bounds access");
        return _begin[i];
    }

    const_reference operator[](size_t i) const {
        assert(i < _size && "out-of-bounds access");
        return _begin[i];
    }

    reference back() {
        assert(!empty() && "back() is called on an empty container");
        return _begin[_size - 1];
    }

    const_reference back() const {
        return const_cast<ISmallVector<T> &>(*this)->back();
    }

    reference front() {
        assert(!empty() && "front() is called on an empty container");
        return _begin[0];
    }

    const_reference front() const {
        return const_cast<ISmallVector<T> &>(*this)->front();
    }

    /**
     * Append \p value to the vector. This function resizes the vector by
     * factor 1.5 when it exceeds capacity().
     *
     * This function may reallocate storage and invalidate pointers that are
     * saved elsewhere.
     */
    void push_back(const T &value) {
        grow(_size + 1);
        ++_size;
        createCopy(&back(), value);
        return;
    }

    /**
     * Construct and append \p value to the vector. This function resizes the
     * vector by factor 1.5 when it exceeds capacity().
     *
     * This function may reallocate storage and invalidate pointers that are
     * saved elsewhere.
     */
    template <class... Args>
    reference emplace_back(Args &&...args) {
        grow(_size + 1);
        ++_size;
        return createEmplace(&back(), std::forward<Args>(args)...);
    }

    /**
     * Insert a new element before \p insertBefore pointer and return an
     * iterator to the inserted element. All elements in range [insertBefore,
     * end) are moved by 1 element.
     *
     * This function may reallocate storage and invalidate pointers that are
     * saved elsewhere.
     */
    iterator insert(iterator insertBefore, const T &value) {
        assert(insertBefore >= _begin && insertBefore <= end());

        size_t moveElements = end() - insertBefore;
        if (!moveElements) {
            // Append, nothing to memmove.
            push_back(value);
            return &back();
        }
        size_t moveStart = insertBefore - _begin;
        grow(_size + 1);
        ++_size;
        memmove(&_begin[moveStart + 1], &_begin[moveStart],
                sizeof(T) * moveElements);
        iterator insertedIt = &_begin[moveStart];
        createCopy(insertedIt, value);
        return insertedIt;
    }

    /**
     * Remove an element pointer by an iterator.
     */
    void erase(iterator eraseIt) {
        assert(eraseIt >= _begin && eraseIt < end() && "iterator out-of-bounds");

        iterator moveIt = eraseIt + 1;
        size_t moveElements = end() - moveIt;
        if (!moveElements) {
            // Erase last, nothing to memmove.
            resize(_size - 1);
            return;
        }
        destroy(eraseIt);
        memmove(eraseIt, moveIt, sizeof(T) * moveElements);
        --_size;
    }

    /**
     * Resize the vector. When \p new_size is greater than size(), new elements
     * are initialized by a default constructor. When \p new_size is less that
     * size(), exceeding elements at the end of the vector are destroyed.
     *
     * This function may reallocate storage and invalidate pointers that are
     * saved elsewhere.
     */
    void resize(size_t new_size) {
        size_t orig_size = _size;
        if (new_size > orig_size) {
            reserve(new_size);
            createRangeDefault(_begin + orig_size, _begin + new_size);
            _size = new_size;
            return;
        }
        _size = new_size;
        destroyRange(_begin + new_size, _begin + orig_size);
    }

    /**
     * Reserve underlying storage without changing elements or size.
     *
     * This function may reallocate storage and invalidate pointers that are
     * saved elsewhere.
     */
    void reserve(size_t new_cap) {
        if (new_cap <= capacity()) {
            return;
        }

        if (isSmall()) {
            allocHeap(new_cap);
            return;
        }

        reallocHeap(new_cap);
    }

protected:
    /**
     * Initialize from a SmallVector: \p begin is a pointer to co-allocated
     * memory of size \p smallCapacity.
     *
     * Note that we negate \p smallCapacity here to indicate that _begin is not
     * a heap allocation yet.
     */
    ISmallVector(T *begin, int64_t smallCapacity) :
        _begin(begin), _size(0), _capacity(-smallCapacity) {}

    /**
     * Returns true when _begin points to a co-allocated storage. This is
     * indicated by negative _capacity field. When storage is a heap
     * allocation, capacity is positive.
     */
    bool isSmall() const { return _capacity <= 0; }

    T *_begin;
    size_t _size;
    int64_t _capacity;

private:
    /**
     * Placement new (default constructor).
     */
    void createDefault(T *ptr) {
        new (ptr) T();
    }

    /**
     * Placement new (default constructor) for elements from \p begin to \p end.
     */
    void createRangeDefault(iterator begin, iterator end) {
        while (begin != end) {
            createDefault(begin++);
        }
    }

    /**
     * Placement new (copy constructor).
     */
    void createCopy(T *ptr, const T &value) {
        new (ptr) T(value);
    }

    /**
     * Placement new (emplace constructor).
     */
    template <class... Args>
    reference createEmplace(T *ptr, Args &&...args) {
        return *(new (ptr) T(std::forward<Args>(args)...));
    }

    /**
     * Placement delete.
     */
    void destroy(T *ptr) {
        ptr->~T();
    }

    /**
     * Placement delete for a range from \p begin to \p end.
     */
    void destroyRange(T *begin, T *end) {
        while (begin != end) {
            destroy(begin++);
        }
    }

    /**
     * Reserve up to \p new_cap or by a factor of 1.5, whatever is higher.
     */
    void grow(size_t new_cap) {
        if (capacity() >= new_cap) {
            return;
        }
        size_t grow_cap = capacity() * 1.5f;
        reserve(grow_cap > new_cap ? grow_cap : new_cap);
    }

    /**
     * Make a heap allocation and copy data from co-allocated storage.
     */
    void allocHeap(size_t new_cap) {
        assert(isSmall());
        size_t size_bytes = sizeof(T) * new_cap;
        T *heap = (T *)malloc(size_bytes);
        memcpyElements(heap, _begin, _size);
        _begin = heap;
        _capacity = new_cap;
    }

    /**
     * Reallocate a heap allocation to a larger size.
     */
    void reallocHeap(size_t new_cap) {
        assert(!isSmall() && new_cap != 0);
        _begin = (T *)realloc(_begin, sizeof(T) * new_cap);
        _capacity = new_cap;
    }

    void memcpyElements(T *dest, T *source, size_t count) {
        assert(source >= _begin && "memcpy invalid source");
        assert((source + count) <= end() && "memcpy source overflow");
        assert((dest + count) <= end() && "memcpy dest overflow");
        assert(!((dest >= _begin && dest < end())
                 || ((dest + count) >= _begin && (dest + count) < end()))
               && "memcpy regions must not overlap");

        size_t bytes = sizeof(T) * count;
        memcpy(dest, source, bytes);
    }

    void memmoveElements(T *dest, T *source, size_t count) {
        assert(source >= _begin && "memcpy invalid source");
        assert((source + count) <= end() && "memcpy source overflow");
        assert((dest + count) <= end() && "memcpy dest overflow");

        size_t bytes = sizeof(T) * count;
        memmove(dest, source, bytes);
    }
};

/**
 * SmallVector is a container similar to std::vector, which it has a small
 * number of elements co-allocated within the object itself. See documentation
 * of ISmallVector for more details.
 */
template <typename T, size_t Capacity>
class SmallVector : public ISmallVector<T> {
public:
    SmallVector() :
        ISmallVector<T>((T *)_coalloc, Capacity) {}

    SmallVector(const SmallVector<T, Capacity> &) = delete;

private:
    /**
     * Co-allocated storage.
     */
    alignas(T) char _coalloc[sizeof(T) * Capacity];
};

} // namespace reone
