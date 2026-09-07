/*
 * Copyright (c) 2026 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "reone/game/menupresentation.h"

#include <array>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>

namespace reone::game {
namespace {
const std::array<std::string, 5> keys {
    "k2-menu-selector", "k2-menu-gender", "k2-menu-appearance",
    "k2-menu-body", "k2-menu-texture"};

std::string trim(const std::string &s) {
    auto begin = s.find_first_not_of(" \t\r");
    return begin == std::string::npos ? "" : s.substr(begin, s.find_last_not_of(" \t\r") - begin + 1);
}

std::optional<size_t> menuKey(const std::string &line) {
    auto key = trim(line.substr(0, line.find('=')));
    for (size_t i = 0; i < keys.size(); ++i) {
        if (key == keys[i]) return i;
    }
    return std::nullopt;
}
} // namespace

std::string MenuPresentation::modelResRef(bool tsl) const {
    if (!tsl) return "mainmenu";
    return "mainmenu0" + std::to_string(validateSelector(selector) + 1);
}

MenuPresentation MenuPresentation::load(const std::filesystem::path &path) {
    MenuPresentation result;
    std::ifstream input(path);
    std::map<size_t, int> values;
    for (std::string line; std::getline(input, line);) {
        if (trim(line).rfind("[", 0) == 0) break; // These are top-level engine options.
        auto key = menuKey(line);
        if (!key || line.find('=') == std::string::npos) continue;
        std::istringstream value(line.substr(line.find('=') + 1));
        int number;
        if (value >> number) values[*key] = number;
    }
    result.selector = validateSelector(values[0]);
    if (values.count(1) && values.count(2) && values.count(3) && values.count(4) &&
        values[1] >= 0 && values[1] <= 4 && values[2] >= 0 && values[2] <= 65535 &&
        values[3] >= 0 && values[3] < 26 && values[4] >= 0 && values[4] <= 255) {
        result.leader = CreaturePresentation {values[1], values[2], values[3], values[4]};
    }
    return result;
}

void MenuPresentation::save(const std::filesystem::path &path) const {
    if (path.empty()) return;
    std::ifstream input(path, std::ios::binary);
    if (!input && std::filesystem::exists(path)) {
        throw std::runtime_error("Cannot read menu configuration: " + path.string());
    }
    std::ostringstream output;
    output << keys[0] << '=' << validateSelector(selector) << '\n';
    if (leader) {
        output << keys[1] << '=' << leader->gender << '\n'
               << keys[2] << '=' << leader->appearance << '\n'
               << keys[3] << '=' << leader->bodyVariation << '\n'
               << keys[4] << '=' << leader->textureVariation << '\n';
    }
    // Retain every unrelated byte, including comments, sections, and unknown keys.
    bool topLevel = true;
    for (std::string line; std::getline(input, line);) {
        if (trim(line).rfind("[", 0) == 0) topLevel = false;
        if (topLevel && menuKey(line)) continue;
        output << line;
        if (!input.eof()) output << '\n';
    }
    auto temporary = path;
    temporary += ".menu.tmp";
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    file << output.str();
    file.close();
    if (!file) throw std::runtime_error("Cannot write menu configuration: " + temporary.string());
    std::filesystem::rename(temporary, path);
}
} // namespace reone::game
