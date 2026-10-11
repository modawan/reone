/*
 * Copyright (c) 2020-2023 The reone project contributors
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

#include <cstdint>
#include <memory>

namespace reone {

namespace script {

class EngineType : boost::noncopyable {
public:
    virtual ~EngineType() = default;
};

/**
 * An engine structure that scripts copy and compare by value. Every effect
 * variable holds one.
 */
class CopyableEngineType : public EngineType {
public:
    /** A copy of the value for a copied script variable. */
    virtual std::shared_ptr<EngineType> cloneForScript() const = 0;

    /** The identity scripts compare values by, which a copy keeps. */
    virtual uint64_t scriptValueId() const = 0;

protected:
    CopyableEngineType() = default;
    CopyableEngineType(const CopyableEngineType &) :
        EngineType() {}
};

} // namespace script

} // namespace reone
