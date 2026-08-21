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

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "reone/resource/parser/gff/nfo.h"
#include "reone/resource/saveworkingstate.h"
#include "reone/system/types.h"

namespace reone {

namespace game {

struct SavedGame {
    uint32_t slot {0};
    uint32_t displayNumber {0};
    resource::SaveSlotDescriptor descriptor;
    resource::NFO metadata;
    std::optional<ByteBuffer> screenshot;
};

/** Discover structurally usable durable slots below one installation root. */
std::vector<SavedGame> discoverSavedGames(const std::filesystem::path &gamePath);

/** Presentation label only; never use it to target durable storage. */
std::string saveGameNumberLabel(const SavedGame &save);

/** Numeric slot identity is authoritative; return the next unused manual slot. */
uint32_t nextManualSaveSlot(const std::vector<SavedGame> &saves);

/** Delete one exact validated durable slot. Active working state is detached. */
bool deleteSavedGame(
    const std::filesystem::path &gamePath,
    const resource::SaveSlotDescriptor &slot);

} // namespace game

} // namespace reone
