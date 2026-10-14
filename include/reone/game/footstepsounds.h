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

#include <map>
#include <memory>
#include <string>
#include <unordered_map>

namespace reone {

namespace resource {

class TwoDAs;

} // namespace resource

namespace game {

/** A footstepsounds.2da row: the step sounds of one footstep type. */
struct FootstepTypeSounds {
    /**
     * The row's cells by lower-case column name. A step sound's column is a
     * surface name followed by its variant, 0 to 2: dirt0, puddles2.
     */
    std::map<std::string, std::string> cells;
    /** How far a step's pitch may stray: up by as much, or down by half as much. */
    float pitchOffset {0.0f};
};

class IFootstepSounds {
public:
    virtual ~IFootstepSounds() = default;

    virtual void clear() = 0;

    virtual std::shared_ptr<FootstepTypeSounds> get(uint32_t key) = 0;
};

class FootstepSounds : public IFootstepSounds {
public:
    FootstepSounds(resource::TwoDAs &twoDas) :
        _twoDas(twoDas) {
    }

    void clear() override {
        _objects.clear();
    }

    std::shared_ptr<FootstepTypeSounds> get(uint32_t key) override {
        auto maybeObject = _objects.find(key);
        if (maybeObject != _objects.end()) {
            return maybeObject->second;
        }
        auto object = doGet(key);
        return _objects.insert(make_pair(key, std::move(object))).first->second;
    }

private:
    resource::TwoDAs &_twoDas;

    std::unordered_map<uint32_t, std::shared_ptr<FootstepTypeSounds>> _objects;

    std::shared_ptr<FootstepTypeSounds> doGet(uint32_t type);
};

} // namespace game

} // namespace reone
