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

#include <array>
#include <cstddef>
#include <optional>
#include <string>

#include "effect/damage.h"

namespace reone {

namespace resource {

class IStrings;

}

namespace game {

/**
 * Localized message template produced for one mitigation-feedback record.
 * Custom tokens are stored in token-number order, beginning at zero.
 */
struct MitigationFeedbackMessage {
    int strRef {-1};
    std::array<std::string, 3> customTokens {};
    std::size_t customTokenCount {0};
};

/**
 * Builds the localized-template payload consumed by the message log.
 *
 * Immunity uses target and damage-type tokens. Ordinary mitigation uses
 * target and amount, while finite mitigation adds the remaining pool.
 *
 * @param targetName Resolved object name, or std::nullopt when object-name
 * resolution failed. Failed resolution uses dialog.tlk string 0; a resolved
 * empty name remains empty.
 */
MitigationFeedbackMessage buildMitigationFeedbackMessage(
    resource::IStrings &strings,
    std::optional<std::string> targetName,
    const MitigationFeedback &feedback);

} // namespace game

} // namespace reone
