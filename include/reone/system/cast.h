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

namespace reone {

/**
 * Returns true if \p from is an object of type T or a type derived from T.
 * Overloaded function for a pointer type.
 */
template <typename T, typename U>
bool isa(U *from) {
    assert(from && "isa<> used on a null pointer");
    return T::classof(from);
}

/**
 * Returns true if \p from is an object of type T or a type derived from T.
 * Overloaded function for a reference type.
 */
template <typename T, typename U>
// Use "enable_if" here to avoid a conflict with a variant for shared_ptr.
std::enable_if_t<std::is_function<decltype(U::classof)>::value, bool>
isa(U &from) {
    return T::classof(&from);
}

/**
 * Returns true if \p from is an object of type T or a type derived from T.
 * Overloaded function for a shared_ptr type.
 */
template <typename T, typename U>
bool isa(const std::shared_ptr<U> &from) {
    assert(from && "isa<> used on a null shared_ptr");
    return T::classof(from.get());
}

/**
 * Cast \p from to type T, assuming that such cast is valid. When assertions are
 * enabled, the function is going to crash if such cast is not valid.
 *
 * Overloaded function for a pointer type.
 */
template <typename T, typename U>
T *cast(U *from) {
    assert(isa<T>(from) && "invalid cast");
    return static_cast<T *>(from);
}

/**
 * Cast \p from to type T, assuming that such cast is valid. When assertions are
 * enabled, the function is going to crash if such cast is not valid.
 *
 * Overloaded function for a reference type.
 */
template <typename T, typename U>
// Use "enable_if" here to avoid a conflict with a variant for shared_ptr.
std::enable_if_t<std::is_function<decltype(U::classof)>::value, T &>
cast(U &from) {
    assert(isa<T>(from) && "invalid cast");
    return static_cast<T &>(from);
}

/**
 * Cast \p from to type T, assuming that such cast is valid. When assertions are
 * enabled, the function is going to crash if such cast is not valid.
 *
 * Overloaded function for a shared_ptr type.
 */
template <typename T, typename U>
std::shared_ptr<T> cast(const std::shared_ptr<U> &from) {
    assert(isa<T>(from) && "invalid cast");
    return std::static_pointer_cast<T>(from);
}

/**
 * Cast \p from to type T if such cast is valid, otherwise return nullptr.
 *
 * Overloaded function for a pointer type.
 */
template <typename T, typename U>
T *dyn_cast(U *from) {
    return T::classof(from) ? (T *)from : nullptr;
}

/**
 * Cast \p from to type T if such cast is valid, otherwise return nullptr.
 *
 * Overloaded function for a shared_ptr type.
 */
template <typename T, typename U>
std::shared_ptr<T> dyn_cast(const std::shared_ptr<U> &from) {
    return T::classof(from.get()) ? std::static_pointer_cast<T>(from) : nullptr;
}

} // namespace reone
