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

template <typename T, typename U>
bool isa(U *from) {
    return T::classof(from);
}

template <typename T, typename U>
bool isa(U &from) {
    return T::classof(&from);
}

template <typename T, typename U>
bool isa(std::shared_ptr<U> from) {
    return T::classof(from.get());
}

template <typename T, typename U>
T *cast(U *from) {
    assert(isa<T>(from) && "invalid cast");
    return static_cast<T>(from);
}

template <typename T, typename U>
T &cast(U &from) {
    assert(isa<T>(from) && "invalid cast");
    return static_cast<T &>(from);
}

template <typename T, typename U>
std::shared_ptr<T> cast(std::shared_ptr<U> from) {
    assert(isa<T>(from) && "invalid cast");
    return std::static_pointer_cast<T>(from);
}

template <typename T, typename U>
T *dyn_cast(U *from) {
    return T::classof(from) ? (T *)from : nullptr;
}

template <typename T, typename U>
std::shared_ptr<T> dyn_cast(const std::shared_ptr<U> &from) {
    return T::classof(from.get()) ? std::static_pointer_cast<T>(from) : nullptr;
}

} // namespace reone
