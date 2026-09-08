/*
 * Copyright (c) 2026 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace reone::game {

// Visual values only: never an object ID, Party binding, or live creature.
struct CreaturePresentation {
    int gender {0};
    int appearance {0};
    int bodyVariation {0}; // appearance.2da model/tex column: a = 0
    int textureVariation {1};
};

struct MenuPresentation {
    int selector {0};
    std::optional<CreaturePresentation> leader;

    static int validateSelector(int value) { return value >= 0 && value <= 4 ? value : 0; }
    std::string modelResRef(bool tsl) const;
    static MenuPresentation load(const std::filesystem::path &path);
    void save(const std::filesystem::path &path) const;
};

} // namespace reone::game
