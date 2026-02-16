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

#include "reone/graphics/texture.h"

#include "../types.h"

namespace reone {

namespace audio {
class AudioClip;
}

namespace game {

struct Spell {
    SpellType type;
    std::string name;
    std::string description;
    std::shared_ptr<graphics::Texture> icon;
    uint32_t pips {1}; // 1-3, position in a feat chain
    uint32_t category {0};
    std::string impactScript;
    std::string castAnim;
    std::shared_ptr<audio::AudioClip> castSound;
    float conjTime {0.0f};
    float castTime {0.0f};
    uint32_t itemTargeting {0};
    bool hostile {false};
};

} // namespace game

} // namespace reone
