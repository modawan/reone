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

namespace reone {

namespace resource {

class Gff;

struct NFO {
    std::string areaName;
    std::string lastModule;
    uint32_t timePlayed;
    bool cheatUsed;
    std::string savegameName;
    uint32_t gameplayHint;
    uint32_t storyHint;
    std::string live1;
    std::string live2;
    std::string live3;
    std::string live4;
    std::string live5;
    std::string live6;
    uint32_t liveContent;
    std::string portrait0;
};

NFO parseNFO(const Gff &gff);

} // namespace resource
} // namespace reone
