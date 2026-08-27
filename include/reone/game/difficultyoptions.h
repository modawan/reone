/*
 * Copyright (c) 2026 The reone project contributors
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

#include <string>
#include <vector>

namespace reone {

namespace resource {

class ITwoDAs;

} // namespace resource

namespace game {

struct DifficultyOption {
    int nameStrRef {-1};
    std::string description;
    float damageMultiplier {1.0f};
};

class IDifficultyOptions {
public:
    virtual ~IDifficultyOptions() = default;

    virtual const DifficultyOption &get(int difficulty) const = 0;
};

class DifficultyOptions : public IDifficultyOptions, boost::noncopyable {
public:
    explicit DifficultyOptions(resource::ITwoDAs &twoDas) :
        _twoDas(twoDas) {
    }

    void init();

    const DifficultyOption &get(int difficulty) const override;

private:
    resource::ITwoDAs &_twoDas;
    std::vector<DifficultyOption> _options;
};

} // namespace game

} // namespace reone
